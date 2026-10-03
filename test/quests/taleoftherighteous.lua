-- Tale of the Righteous -- full client-driven run, guide steps 1.1-1.30 (Quest Helper TaleOfTheRighteous).
-- Setup stages kit only: ::taleoftherighteous resets the varb, writes the prerequisite vars the debugproc names
-- and stands the player in Phileas's house (tor_bmp.rs2:46). Every leg of the quest is played through clicks.
return {
    id = "taleoftherighteous",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel hitpoints 80",
        "::setlevel attack 60",
        "::setlevel strength 70",
        "::setlevel defence 60",
        "::setlevel ranged 70",
        "::setlevel magic 40",
        "::setlevel mining 45",
        "::give rune_scimitar 1",
        "::give magic_shortbow 1",
        "::give rune_arrow 200",
        "::give air_rune 200",
        "::give mind_rune 200",
        "::give rope 1",
        "::give rune_pickaxe 1",
        "::give shark 12",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::wield rune_full_helm",
        "::wield rune_chainbody",
        "::wield rune_platelegs",
        "::wield rune_scimitar",
        "::taleoftherighteous",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb6358_shayzienquest",
            constants = {
                not_started = 0, started = 1, prison = 2, gate_open = 4, skeleton = 5, shiro = 6, duffy = 7,
                rope = 8, cavern = 9, altar = 10, surface = 11, cave2 = 12, gnosi = 13, shiro2 = 14,
                house = 15, finish = 16, complete = 17,
            },
            display = "Tale of the Righteous",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local _, sharks0 = t.inv.count("shark")
        t.check("setup.kit", true, "staged: rune scimitar wielded, shortbow + arrows + wind strike runes + rope + pickaxe in the pack, rune armour worn, " .. tostring(sharks0) .. " sharks")

        -- guide 1.1 talkToPhileas (stage 0 -> 1). The door of his house is the only thing between, the cheat stands us inside.
        t.exec("goto-talkToPhileas", t.player.goto_tile, 1542, 3570, 0)
        t.exec("talkToPhileas", t.player.talk_to, "phileas_rimor_visible", 1)
        t.exec("talkToPhileas-dialog", t.chat.play, {
            "player:Good day.",
            "npc:Hello there",
            "choose:Do you need help with anything?",
            "player:Do you need help with anything?",
            "npc:Well let me think",
            "npc:You know, there is something",
            "player:Great! What is it?",
            "npc:When I was a boy",
            "npc:One day, Magnus",
            "npc:Alas, according",
            "player:That's very interesting",
            "npc:A few days ago",
            "player:What was it?",
            "npc:There was an old journal",
            "npc:But that's not all",
            "player:Again, really great",
            "npc:Most of the journal",
            "npc:'Quidamortem's creatures",
            "player:So?",
            "npc:That extract had",
            "player:Maybe there was another expedition.",
            "npc:Maybe. Or maybe",
            "choose:Yes.",
            "player:What do you need?",
            "npc:We need to find out",
            "player:That doesn't sound too hard",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- guide 1.2 teleportToArchive
        t.exec("goto-teleportToArchive", t.player.goto_tile, 1624, 3808, 0)
        t.exec("teleportToArchive", t.player.talk_to, "raidquest_library_archive_guardian", 1)
        t.exec("teleportToArchive-dialog", t.chat.play, {
            "npc:Hello. Would you like to visit",
            "choose:Yes please!",
            "player:Yes please!",
        })
        t.ticks(4)
        do local r, h = t.world.tile(); t.check("teleportToArchive.arrived", r == "ok" and h.z > 10000, tostring(r) .. " " .. (r == "ok" and (h.x .. "," .. h.z .. "," .. h.level) or tostring(h)) .. " (want the archive, z > 10000)") end

        -- guide 1.3 talkToPagida (stage 1 -> 2, the prison copy)
        t.exec("talkToPagida", t.player.talk_to, "raidquest_library_traphunter", 1)
        t.exec("talkToPagida-dialog", t.chat.play, {
            "npc:Hello there, what can I do",
            "choose:I have a question about King Shayzien VII.",
            "player:I have a question about King Shayzien VII.",
            "npc:What do you wish to know?",
            "player:I'm trying to find out more",
            "npc:I'm afraid I don't have any information",
            "npc:I could tell you about",
            "player:Well it's either this",
            "npc:During the time of King Shayzien VII",
            "npc:In fact, he oversaw",
            "player:When was this?",
            "npc:Well now that I think",
            "player:I see. Would it be possible",
            "npc:Well the prison is abandoned",
            "npc:Be aware that the magic",
            "choose:Yes please.",
            "player:Yes please.",
            "npc:Very well, prepare yourself.",
        })
        t.ticks(6)
        t.expect("quest.stage.prison", t.quest.expect_stage("prison"))
        local _, ptile = t.world.tile()
        local ox = ptile.x - 1588
        local oz = ptile.z - 10199
        t.check("prison.copy", ptile ~= nil, "private prison copy; entry tile " .. ptile.x .. "," .. ptile.z .. " (world-equivalent 1588,10199), offset " .. ox .. "," .. oz)

        -- guide 1.4 pushStrangeDeviceWest: push from the east side, the device slides to x 1575
        t.exec("pushStrangeDeviceWest", t.player.press, "shayzienquest_puzzle_piece", 1, 20)
        t.ticks(8)
        do local r, row = t.npc.nearest("shayzienquest_puzzle_piece", 20); t.check("pushStrangeDeviceWest.read", r == "ok", tostring(r) .. " " .. (r == "ok" and (tostring(row.x) .. "," .. tostring(row.z)) or tostring(row)) .. " (want x 1575 world => " .. (1575 + ox) .. ")") end

        -- guide 1.5 attackWithMagic: from the north side of the device at the west end
        do local wr = t.player.walk_to(1577 + ox, 10203 + oz, 30); local _, wt = t.world.tile(); t.check("goto-attackWithMagic", wr == "ok", "walk_to 1577 + ox,10203 + oz (instance tile) -> " .. tostring(wr) .. ", standing at " .. tostring(wt.x) .. "," .. tostring(wt.z)) end
        t.exec("attackWithMagic", t.player.cast, "wind_strike", "shayzienquest_puzzle_piece", 14)
        t.ticks(4)
        t.exec("attackWithMagic.cleansed", t.msg.expect, "crystal turns white")

        -- guide 1.6 attackWithMelee: from the south side, scimitar wielded
        do local wr = t.player.walk_to(1576 + ox, 10199 + oz, 30); local _, wt = t.world.tile(); t.check("goto-attackWithMelee", wr == "ok", "walk_to 1576 + ox,10199 + oz (instance tile) -> " .. tostring(wr) .. ", standing at " .. tostring(wt.x) .. "," .. tostring(wt.z)) end
        local melee_target = t.player.by_symbol("npc", "shayzienquest_puzzle_piece")
        t.exec("attackWithMelee", t.drive.click_minimenu, melee_target, 2)
        t.ticks(6)
        t.exec("attackWithMelee.cleansed", t.msg.expect, "crystal turns white")

        -- guide 1.7 pushStrangeDeviceEast: push from the west side
        do local wr = t.player.walk_to(1573 + ox, 10200 + oz, 30); local _, wt = t.world.tile(); t.check("goto-pushStrangeDeviceEast", wr == "ok", "walk_to 1573 + ox,10200 + oz (instance tile) -> " .. tostring(wr) .. ", standing at " .. tostring(wt.x) .. "," .. tostring(wt.z)) end
        t.exec("pushStrangeDeviceEast", t.player.press, "shayzienquest_puzzle_piece", 1, 20)
        t.ticks(8)

        -- guide 1.8 attackWithRanged: from the south side of the east end, bow and arrows wielded
        t.exec("wield-shortbow", t.player.equip, "magic_shortbow")
        t.exec("wield-arrows", t.player.equip, "rune_arrow")
        do local wr = t.player.walk_to(1581 + ox, 10198 + oz, 30); local _, wt = t.world.tile(); t.check("goto-attackWithRanged", wr == "ok", "walk_to 1581 + ox,10198 + oz (instance tile) -> " .. tostring(wr) .. ", standing at " .. tostring(wt.x) .. "," .. tostring(wt.z)) end
        local ranged_target = t.player.by_symbol("npc", "shayzienquest_puzzle_piece")
        t.exec("attackWithRanged", t.drive.click_minimenu, ranged_target, 2)
        t.ticks(6)
        t.exec("attackWithRanged.cleansed", t.msg.expect, "With the last crystal cleansed")
        t.expect("quest.stage.gate_open", t.quest.expect_stage("gate_open"))

        -- guide 1.9 investigateSkeleton
        t.exec("wield-scimitar", t.player.equip, "rune_scimitar")
        t.exec("investigateSkeleton", t.player.click_loc, "shayzienquest_prison_skeleton_main", 1)
        t.exec("investigateSkeleton-dialog", t.chat.play, {
            "mesbox:You notice some writing",
            "player:Hmm. I should tell Phileas",
        })
        t.expect("quest.stage.skeleton", t.quest.expect_stage("skeleton"))
        t.exec("leavePrison", t.player.click_loc, "shayzienquest_prison_portal", 1)
        t.ticks(4)

        -- guide 1.10 talkToPhileasAgain (stage 5 -> 6)
        t.exec("goto-talkToPhileasAgain", t.player.goto_tile, 1542, 3570, 0)
        t.exec("talkToPhileasAgain", t.player.talk_to, "phileas_rimor_visible", 1)
        t.exec("talkToPhileasAgain-dialog", t.chat.play, {
            "player:Good day.",
            "npc:Hello again. How is your quest",
            "player:So I went to the Library",
            "player:I investigated the prison",
            "npc:What did it say?",
            "player:'Lizards on the mountain",
            "npc:Lizards on the mountain?",
            "player:Impossible to say",
            "npc:That's a shame.",
            "npc:That's one to dwell on",
            "player:Next steps?",
            "npc:Absolutely.",
            "player:But why does it matter?",
            "npc:That doesn't mean it doesn't matter",
            "player:Okay, I see you feel strongly",
            "npc:If we're to find out",
            "player:Yeah, I had a feeling",
            "npc:First things first",
            "npc:north.",
            "player:I guess I'll be on my way",
        })
        t.ticks(2)
        t.expect("quest.stage.shiro", t.quest.expect_stage("shiro"))

        -- guide 1.11 goUpToShiro, 1.12 talkToShiro (stage 6 -> 7)
        t.exec("goto-goUpToShiro", t.player.goto_tile, 1492, 3635, 0)
        t.exec("goUpToShiro", t.player.click_loc, "shayzien_ladder", 1)
        t.ticks(4)
        t.exec("talkToShiro", t.player.talk_to, "shiro_shayzien_vis", 1)
        t.exec("talkToShiro-dialog", t.chat.play, {
            "player:Hello.",
            "npc:What do you need citizen?",
            "player:I'd like to perform some research",
            "npc:Quite right.",
            "npc:So, tell me about your research.",
            "player:Many years ago",
            "npc:But?",
            "player:But I have reason to believe",
            "npc:Fair enough.",
            "player:What is it?",
            "npc:If you discover anything",
            "player:But surely the library",
            "npc:They will be made aware.",
            "player:Hmm. Fair enough.",
            "npc:Excellent. Consider your research",
            "npc:Good luck citizen.",
        })
        t.ticks(2)
        t.expect("quest.stage.duffy", t.quest.expect_stage("duffy"))

        -- guide 1.13 talkToDuffy (stage 7 -> 8)
        t.exec("goto-talkToDuffy", t.player.goto_tile, 1277, 3560, 0)
        t.exec("talkToDuffy", t.player.talk_to, "raids_temple_duffy_visible", 1)
        t.exec("talkToDuffy-dialog", t.chat.play, {
            "player:Hello. Are you Duffy?",
            "npc:That's me.",
            "player:Lord Shayzien told me",
            "npc:I'm rather busy",
            "player:Please. It's important.",
            "npc:Well what is it?",
            "player:I'm trying to retrace",
            "npc:Interesting.",
            "player:I have a quote",
            "player:'Lizards on the mountain",
            "npc:Fascinating.",
            "npc:But this quote of yours",
            "player:So will you help me?",
            "npc:Yes. This is definitely",
            "player:Where?",
            "npc:We recently found a small crevice",
            "player:Alright, I'll start there.",
        })
        t.ticks(2)
        t.expect("quest.stage.rope", t.quest.expect_stage("rope"))

        -- guide 1.14 useRopeOnCrevice (stage 8 -> 9): the prompt after the rope is answered No, 1.15 enters.
        t.exec("goto-useRopeOnCrevice", t.player.goto_tile, 1213, 3559, 0)
        local crevice, crevice_ok = t.player.by_symbol("loc", "shayzienquest_cave")
        t.exec("useRopeOnCrevice", t.player.use_on, "rope", crevice)
        t.exec("useRopeOnCrevice-dialog", t.chat.play, {
            "mesbox:You attach a rope",
            "options",
            "choose:No.",
        })
        t.ticks(2)
        t.expect("quest.stage.cavern", t.quest.expect_stage("cavern"))

        -- guide 1.15 enterCrevice
        t.exec("enterCrevice", t.player.click_loc, "shayzienquest_cave", 1)
        t.exec("enterCrevice-dialog", t.chat.play, {
            "options",
            "choose:Yes.",
        })
        t.ticks(6)
        local _, ctile = t.world.tile()
        local cx = ctile.x - 1170
        local cz = ctile.z - 9972
        t.check("enterCrevice.copy", ctile.x ~= 1170, "private cave copy; entry tile " .. ctile.x .. "," .. ctile.z .. " (world-equivalent 1170,9972), offset " .. cx .. "," .. cz)

        -- guide 1.16 mineRockfall, 1.17 pushBoulder
        t.exec("mineRockfall", t.player.click_loc, "shayzienquest_blockage", 1)
        t.exec("mineRockfall.msg", t.msg.await, "You clear the rockfall", 200)
        t.exec("pushBoulder", t.player.click_loc, "shayzienquest_boulder", 1)
        t.ticks(10)
        t.exec("pushBoulder.msg", t.msg.expect, "You push the boulder")

        -- guide 1.19 tryToEnterBarrier (the lizardman appears), 1.18 killLizardman
        t.exec("tryToEnterBarrier", t.player.click_loc, "shayzienquest_cave_door", 1)
        t.ticks(4)
        t.exec("tryToEnterBarrier.msg", t.msg.expect, "a corrupt lizardman appears")
        t.exec("killLizardman", t.player.attack, "shayzienquest_lizardman_boss", 2, 30)
        local _, food0 = t.inv.count("shark")
        local _, kill_detail = t.exec("killLizardman.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local lowest = tonumber(tostring(kill_detail):match("lowest hp (%d+)/"))
        local _, food1 = t.inv.count("shark")
        t.check("killLizardman.dead.margin", (food1 or 0) >= 2 or (lowest or 0) > 25, "sharks before " .. tostring(food0) .. ", left " .. tostring(food1) .. ", lowest hp " .. tostring(lowest) .. "/80 (margin: left >= 2 or lowest > 25)")
        t.ticks(4)
        t.expect("quest.stage.altar", t.quest.expect_stage("altar"))

        -- guide 1.20 inspectUnstableAltar (stage 10 -> 11): through the gate, then the altar
        t.exec("passMagicGate", t.player.click_loc, "shayzienquest_cave_door", 1)
        t.ticks(4)
        t.exec("inspectUnstableAltar", t.player.click_loc, "shayzienquest_lab_altar", 1)
        t.exec("inspectUnstableAltar-dialog", t.chat.play, {
            "mesbox:As you approach the altar",
            "npc:Rickard! Turn away!",
            "player:Hello? Is anyone there?",
            "player:Hmm, I wonder what that was about.",
            "player:Oh well, I may as well go",
        })
        t.ticks(2)
        t.expect("quest.stage.surface", t.quest.expect_stage("surface"))

        -- guide 1.22 leaveCave, 1.21 returnToDuffy (stage 11 -> 12)
        t.exec("passMagicGate-out", t.player.click_loc, "shayzienquest_cave_door", 1)
        t.ticks(4)
        t.exec("leaveCave", t.player.click_loc, "shayzienquest_lab_exit", 1)
        t.ticks(4)
        t.exec("goto-returnToDuffy", t.player.goto_tile, 1277, 3560, 0)
        t.exec("returnToDuffy", t.player.talk_to, "raids_temple_duffy_visible", 1)
        t.exec("returnToDuffy-dialog", t.chat.play, {
            "player:Hello again.",
            "npc:Hello. Did you find anything",
            "player:I did. There was a weird",
            "npc:A lizardman? Is it safe?",
            "player:Don't worry, I took care of it.",
            "npc:Well in that case",
        })
        t.ticks(2)
        t.expect("quest.stage.cave2", t.quest.expect_stage("cave2"))

        -- guide 1.23 enterCreviceAgain, 1.24 talkToDuffyInCrevice (12 -> 13)
        t.exec("goto-enterCreviceAgain", t.player.goto_tile, 1213, 3559, 0)
        t.exec("enterCreviceAgain", t.player.click_loc, "shayzienquest_cave", 1)
        t.exec("enterCreviceAgain-dialog", t.chat.play, {
            "options",
            "choose:Yes.",
        })
        t.ticks(6)
        t.exec("passMagicGate-again", t.player.click_loc, "shayzienquest_cave_door", 1)
        t.ticks(4)
        t.exec("talkToDuffyInCrevice", t.player.talk_to, "shayzienquest_duffy", 1)
        t.exec("talkToDuffyInCrevice-dialog", t.chat.play, {
            "npc:This cave is quite fascinating.",
            "player:What have you discovered?",
            "npc:We've never come across lizards",
            "player:Interesting.",
            "npc:That's nothing.",
            "player:So this is where the lizardmen came from?",
            "npc:It would appear so.",
            "player:So who did create them?",
            "npc:Impossible to say for sure",
            "player:But if King Shayzien VII did create",
            "npc:There are some who believe",
            "npc:Anyway, I'm going to carry on",
        })
        t.ticks(2)
        t.expect("quest.stage.gnosi", t.quest.expect_stage("gnosi"))

        -- guide 1.25 talkToGnosi (13 -> 14)
        t.exec("talkToGnosi", t.player.talk_to, "shayzienquest_gnosi", 1)
        t.exec("talkToGnosi-dialog", t.chat.play, {
            "player:Hello. Duffy said",
            "npc:The Dark Altar was the key",
            "npc:Once he was finally defeated",
            "npc:For many years",
            "player:What is it?",
            "npc:This altar, it seems",
            "player:So this altar was how Xeric",
            "npc:I believe so.",
            "player:But why is it here?",
            "npc:It appears to be manmade.",
            "npc:This discovery does bring",
            "player:What is it?",
            "npc:The altar here is still giving out power.",
            "player:That's a worrying thought.",
            "npc:Indeed. I'm going to stay here",
            "player:Actually, Lord Shayzien asked",
            "npc:Hmm, I can see why",
            "player:Will do, see you later.",
        })
        t.ticks(2)
        t.expect("quest.stage.shiro2", t.quest.expect_stage("shiro2"))
        t.exec("passMagicGate-leave", t.player.click_loc, "shayzienquest_cave_door", 1)
        t.ticks(4)
        t.exec("leaveCave-again", t.player.click_loc, "shayzienquest_lab_exit", 1)
        t.ticks(4)

        -- guide 1.26 returnUpToShiro, 1.27 returnToShiro (14 -> 15)
        t.exec("goto-returnUpToShiro", t.player.goto_tile, 1492, 3635, 0)
        t.exec("returnUpToShiro", t.player.click_loc, "shayzien_ladder", 1)
        t.ticks(4)
        t.exec("returnToShiro", t.player.talk_to, "shiro_shayzien_vis", 1)
        t.exec("returnToShiro-dialog", t.chat.play, {
            "player:Hello.",
            "npc:How is your research going citizen?",
            "player:We've made some important discoveries.",
            "npc:Well?",
            "player:We've discovered a small cave",
            "player:Inside, we found an altar.",
            "player:We also discovered a unique lizard",
            "npc:What does that mean?",
            "player:It seems that this cave",
            "npc:You believe they were the creation",
            "player:Hard to say for sure.",
            "npc:How so?",
            "player:From the evidence we have",
            "player:Once the expedition found one",
            "npc:But we don't have proof?",
            "player:Not definitive",
            "npc:So you claim.",
            "player:It started with a parcel",
            "npc:A parcel?",
            "player:I don't know.",
            "npc:Well you need to find out.",
            "player:Eugh, so much walking.",
        })
        t.ticks(2)
        t.expect("quest.stage.house", t.quest.expect_stage("house"))

        -- guide 1.28 returnToPhileasTent: walking into the house is the trigger (a timer sees the tile)
        t.exec("returnToPhileasTent", t.player.goto_tile, 1542, 3570, 0)
        t.exec("returnToPhileasTent.await", t.var.await, "varb6358_shayzienquest", 16, 30)
        t.exec("returnToPhileasTent-dialog", t.chat.play, {
            "player:Err, this doesn't look good.",
        })
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))

        -- guide 1.29 goUpToShrioToFinish, 1.30 finishQuest
        local _, coins0 = t.inv.count("coins")
        local snap = t.skill.snapshot()
        t.exec("goto-goUpToShrioToFinish", t.player.goto_tile, 1492, 3635, 0)
        t.exec("goUpToShrioToFinish", t.player.click_loc, "shayzien_ladder", 1)
        t.ticks(4)
        t.exec("finishQuest", t.player.talk_to, "shiro_shayzien_vis", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Do you have the parcel citizen?",
            "player:I'm afraid not.",
            "npc:Gone? That's not good.",
            "player:What do we do now?",
            "npc:I'll open an investigation",
            "player:What about the research?",
            "npc:I'm afraid that without that parcel",
            "npc:However, the rest of your research",
            "player:Anything I can do?",
            "npc:You've done enough for now citizen.",
        })
        t.ticks(4)
        t.quest.expect_complete()
        local _, coins1 = t.inv.count("coins")
        t.check("reward.coins", (coins1 or 0) - (coins0 or 0) == 8000, "coins " .. tostring(coins0) .. " -> " .. tostring(coins1) .. " (want +8000)")
        t.expect("reward.page", t.inv.expect_has("veos_memoirs_shay_page", 1))
        t.finish(0)
    end,
}
