-- The Grand Tree (varp grandtree). Client-driven end to end: every guide step is a real row.
-- Guide source: Quest Helper helpers/quests/thegrandtree/. Requirement: Agility 25 (::setlevel).
-- Combat gear and sharks are the guide's recommended kit (::give); the foreman and the black demon
-- are fought for real. Quest state is only ever advanced by the quest's own dialogue and locs.

return {
    id = "grandtree",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the quest's items fit
        "::setlevel agility 25",
        "::setlevel hitpoints 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::give rune_scimitar 1",
        "::give mithril_platebody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 10",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "grandtree",
            constants = {
                not_started = 0,
                started = 10,
                spoken_hazelmere = 20,
                relayed_message_narnode = 30,
                spoken_glough = 40,
                found_prisoner = 50,
                spoken_prisoner = 60,
                found_journal = 70,
                released_prison = 80,
                obtained_lumber_order = 90,
                clue_charlie = 100,
                found_invasion_plans = 110,
                given_twigs = 120,
                unlocked_trapdoor = 130,
                defeated_black_demon = 140,
                searching_daconia = 150,
                complete = 160,
            },
            display = "The Grand Tree",
            points = 5,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- the recommended combat kit, worn for the two fights
        for _, item in ipairs({ "rune_scimitar", "mithril_platebody", "rune_platelegs", "rune_full_helm", "rune_kiteshield" }) do
            t.exec("wear." .. item, t.player.equip, item)
        end

        -- talkToKingNarnode: (make sure to have two empty inventory slots to start the quest)
        t.exec("goto-talkToKingNarnode", t.player.goto_tile, 2467, 3498, 0)
        t.exec("talkToKingNarnode", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("talkToKingNarnode-dialog-1", t.chat.play, {
            "npc:Welcome Traveller", "player:Hi! It seems", "npc:For now", "options",
            "choose:You seem worried, what's up?", "player:You seem worried", "npc:Traveller, Can I speak",
            "player:Of course sire", "npc:Not here, follow me" })
        t.expect("narnode.caves_page", t.await({ level = function() return t.chat.kind() == "player" end, note = "caves page" }, 60))
        t.exec("talkToKingNarnodeCaves-dialog", t.chat.play, {
            "player:So what is this place", "npc:These, my friend", "player:They look like roots", "npc:Not just any roots",
            "player:Impressive", "npc:In the last two months", "player:You mean the tree is ill", "npc:In effect yes", "options",
            "choose:I'd be happy to help!", "player:I'd be happy to help", "npc:Thank Guthix", "npc:The first task",
            "player:Do you have an idea", "npc:My top tree guardian", "player:Who's Hazelmere", "npc:Hazelmere is one of the mages",
            "mesbox:The king has given you a sample of bark", "npc:The mage only talks",
            "mesbox:The king has given you a translation book", "player:What is it", "npc:It's a translation book",
            "npc:I'll show you the way back up" })
        t.ticks(4)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- climbUpToHazelmere: the island east of Yanille
        t.exec("goto-climbUpToHazelmere", t.player.goto_tile, 2677, 3086, 0)
        t.exec("climbUpToHazelmere", t.player.click_loc, "ladder", 1)
        t.ticks(3)
        t.exec("talkToHazelmere", t.player.talk_to, "grandtree_hazelmere", 1)
        t.exec("talkToHazelmere-dialog", t.chat.play, {
            "mesbox:The mage starts to speak", "npc:Blah. Blah, blah", "mesbox:You give the bark sample",
            "mesbox:The mage carefully examines", "npc:Blah, blah...Daconia", "player:Can you write this down",
            "npc:Blah, blah?", "mesbox:You make a writing motion", "mesbox:Hazelmere has given you the scroll" })
        t.ticks(2)
        t.expect("quest.stage.spoken_hazelmere", t.quest.expect_stage("spoken_hazelmere"))

        -- bringScrollToKingNarnode
        t.exec("goto-bringScrollToKingNarnode", t.player.goto_tile, 2467, 3498, 0)
        t.exec("bringScrollToKingNarnode", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("bringScrollToKingNarnode-dialog", t.chat.play, {
            "player:Hello again, your highness", "npc:Hello Traveller, did you speak", "player:Yes! I managed",
            "npc:Do you understand", "options", "choose:I think so!", "player:I think so", "npc:So what did he say",
            "options", "choose:None of the above.", "options", "choose:None of the above.",
            "options", "choose:A man came to me with the King's seal.", "player:A man came to me",
            "options", "choose:I gave the man Daconia rocks.", "player:I gave the man Daconia rocks",
            "options", "choose:And Daconia rocks will kill the tree!", "player:And Daconia rocks will kill the tree",
            "npc:Of course! I should've known", "player:What are Daconia stones", "npc:Hazelmere created",
            "npc:This is terrible", "player:Can I help", "npc:First I must warn", "npc:If he's not there",
            "player:OK! I'll be back soon" })
        t.ticks(2)
        t.expect("quest.stage.relayed_message_narnode", t.quest.expect_stage("relayed_message_narnode"))

        -- climbUpToGlough / talkToGlough
        t.exec("goto-climbUpToGlough", t.player.goto_tile, 2476, 3462, 0)
        t.exec("climbUpToGlough", t.player.click_loc, "ladder", 1)
        t.ticks(3)
        t.exec("talkToGlough", t.player.talk_to, "grandtree_glough", 1)
        t.exec("talkToGlough-dialog", t.chat.play, {
            "player:Hello", "mesbox:The gnome is munching", "npc:Can I help human", "mesbox:The gnome continues to eat",
            "player:The King asked me to inform you", "npc:Surely not", "player:Apparently a human took them",
            "npc:I should've known", "player:Never", "npc:Your type can't be trusted" })
        t.ticks(2)
        t.expect("quest.stage.spoken_glough", t.quest.expect_stage("spoken_glough"))

        -- talkToKingNarnodeAfterGlough
        t.exec("goto-talkToKingNarnodeAfterGlough", t.player.goto_tile, 2467, 3498, 0)
        t.exec("talkToKingNarnodeAfterGlough", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("talkToKingNarnodeAfterGlough-dialog", t.chat.play, {
            "player:Hello, your highness", "npc:It's OK Traveller, thanks to Glough", "player:Wow! That was quick",
            "npc:Yes Glough really knows", "npc:Maybe Glough was right", "player:I doubt it, can I speak to the prisoner",
            "npc:Certainly" })
        t.ticks(2)
        t.expect("quest.stage.found_prisoner", t.quest.expect_stage("found_prisoner"))

        -- the Grand Tree's ladders, floor 0 to floor 3
        t.exec("goto-climbGrandTreeF0ToF1", t.player.goto_tile, 2466, 3494, 0)
        t.exec("climbGrandTreeF0ToF1", t.player.click_loc, "grandtree_ladderbottom", 1)
        t.ticks(3)
        t.exec("climbGrandTreeF1ToF2", t.player.click_loc, "grandtree_laddermiddle_bottom", 2)
        t.ticks(3)
        t.exec("climbGrandTreeF2ToF3", t.player.click_loc, "grandtree_laddermiddle_top", 2)
        t.ticks(3)
        t.expect("talkToCharlie.present", t.npc.await_present("grandtree_charlie", 8, 10))
        t.exec("talkToCharlie", t.player.talk_to, "grandtree_charlie", 1)
        t.exec("talkToCharlie-dialog", t.chat.play, {
            "player:Tell me. Why would you want to kill", "npc:What do you mean", "player:Don't tell me",
            "npc:All I know is that I did what I was asked", "player:I don't understand", "npc:Glough paid me",
            "npc:I've been doing it for weeks", "player:Sounds like Glough is hiding something",
            "npc:I don't know what he's up to", "player:OK. Thanks Charlie", "npc:Good luck" })
        t.ticks(2)
        t.expect("quest.stage.spoken_prisoner", t.quest.expect_stage("spoken_prisoner"))

        -- back down the tree
        t.exec("climbGrandTreeF3ToF2", t.player.click_loc, "grandtree_laddertop", 1)
        t.ticks(3)
        t.exec("climbGrandTreeF2ToF1", t.player.click_loc, "grandtree_laddermiddle_top", 3)
        t.ticks(3)
        t.exec("climbGrandTreeF1ToF0", t.player.click_loc, "grandtree_laddermiddle_bottom", 3)
        t.ticks(3)

        -- returnToGlough / findGloughJournal
        t.exec("goto-returnToGlough", t.player.goto_tile, 2476, 3462, 0)
        t.exec("returnToGlough", t.player.click_loc, "ladder", 1)
        t.ticks(3)
        t.exec("findGloughJournal.open", t.player.click_loc, "grandtree_cupboardclosed", 1)
        t.ticks(2)
        t.exec("findGloughJournal", t.player.click_loc, "grandtree_cupboardopen", 2)
        t.exec("findGloughJournal-dialog", t.chat.play, { "mesbox:You've found Glough's Journal" })
        t.ticks(2)
        t.expect("quest.stage.found_journal", t.quest.expect_stage("found_journal"))

        -- talkToGloughAgain: he has you arrested
        t.exec("talkToGloughAgain", t.player.talk_to, "grandtree_glough", 1)
        t.exec("talkToGloughAgain-dialog", t.chat.play, {
            "player:I don't know what you're up to", "npc:You're a fool human", "player:Grand Tree's dying",
            "npc:How dare you accuse", "npc:Guards! Guards!" })
        t.expect("glough.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "Come with me page" }, 40))
        t.exec("glough.commands", t.chat.play, { "npc:Come with me" })
        t.expect("charlie.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "cell page" }, 60))
        t.exec("talkToCharlieFromCell-dialog", t.chat.play, {
            "npc:So they got you as well", "player:It's Glough", "npc:I shouldn't tell you", "npc:But if you want",
            "player:Why?", "npc:Glough sent me to Karamja", "npc:Karamja Shipyard", "player:Thanks Charlie" })
        t.expect("narnode.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "narnode page" }, 60))
        t.exec("talkToKingNarnodeBeforeEscape-dialog-1", t.chat.play, { "npc:Traveller please accept" })
        t.expect("narnode.wait_trust", t.await({ level = function() return t.chat.kind() == "player" end, note = "trust page" }, 60))
        t.exec("talkToKingNarnodeBeforeEscape-dialog-2", t.chat.play, {
            "player:I don't think you can trust Glough", "npc:I know he can be a bit extreme",
            "npc:I'm afraid Glough has placed guards", "player:Well, OK", "npc:I'm sorry again", "end" })
        t.ticks(3)
        t.expect("quest.stage.released_prison", t.quest.expect_stage("released_prison"))

        -- escapeByGlider: the pilot on top of the Grand Tree
        t.exec("escapeByGlider", t.player.talk_to, "pilot_grand_tree", 1)
        t.exec("escapeByGlider-dialog", t.chat.play, {
            "npc:Hi, the King said", "player:Apparently humans are invading", "npc:I find that hard to believe",
            "player:I don't understand it either", "npc:So where to", "options", "choose:Take me to Karamja please!",
            "player:Take me to Karamja please", "npc:OK! You're the boss" })
        t.ticks(30)

        -- enterTheShipyard
        t.exec("goto-enterTheShipyard", t.player.goto_tile, 2942, 3041, 0)
        t.exec("enterTheShipyard", t.player.click_loc, "grandtree_fencegate_l", 1)
        t.exec("enterTheShipyard-dialog", t.chat.play, {
            "npc:What are you up to", "player:trying to open the gate", "npc:I can see that", "options",
            "choose:Glough sent me.", "player:Glough sent me", "npc:really", "player:wasting my time", "npc:Password",
            "options", "choose:Ka.", "player:Ka.", "options", "choose:Lu.", "player:Lu.", "options", "choose:Min.", "player:Min.",
            "npc:Sorry to have kept you", "end" })
        t.ticks(6)

        -- talkToForeman: the wrong answer makes him attack; kill him for the lumber order
        t.exec("goto-talkToForeman", t.player.goto_tile, 3001, 3043, 0)
        t.exec("talkToForeman", t.player.talk_to, "grandtree_foreman", 1)
        t.exec("talkToForeman-dialog-1", t.chat.play, {
            "player:Hello, are you in charge", "npc:That's right", "player:Glough sent me", "npc:Right. Glough sent a human",
            "player:His gnomes are busy", "npc:Hmm", "npc:Follow me", "end" })
        t.expect("foreman.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "office page" }, 80))
        t.exec("talkToForeman-dialog-2", t.chat.play, {
            "npc:Tell me again", "player:Er", "npc:By the way how is Glough", "options",
            "choose:Yes, they're getting on great.", "player:Yes, they're getting on great", "npc:Really? That's odd", "end" })
        t.exec("talkToForeman.fight", t.npc.await_dead, "grandtree_foreman", 200, 12, 8, { eat = { item = "shark", below = 50 } })
        t.ticks(3)
        t.exec("talkToForeman.order", t.player.click_obj, "grandtree_order", 3)
        t.expect("quest.stage.obtained_lumber_order", t.quest.expect_stage("obtained_lumber_order"))

        -- goTalkToCharlie3: Femi sneaks you past the stronghold gate
        t.exec("goto-gnomeGate", t.player.goto_tile, 2460, 3379, 0)
        t.exec("gnomeGate", t.player.click_loc, "gnome_areagate", 1)
        t.exec("gnomeGate-dialog", t.chat.play, {
            "npc:I'm afraid that we have orders", "player:Orders from who", "npc:The head tree guardian",
            "player:Glough!", "npc:I'm sorry but you'll have to leave", "end" })
        t.exec("goto-femi", t.player.goto_tile, 2459, 3381, 0)
        t.exec("femi", t.player.talk_to, "grandtree_femi", 1)
        t.exec("femi-dialog", t.chat.play, {
            "player:I can't believe they won't let me in", "npc:I don't believe all this rubbish",
            "player:I really need to see King Narnode", "npc:Well, as you helped me", "player:OK, what should I do",
            "npc:Jump in the back of the cart" })
        t.ticks(20)

        -- climb to Charlie again
        t.exec("goto-climbGrandTreeF0ToF1-again", t.player.goto_tile, 2466, 3494, 0)
        t.exec("climbGrandTreeF0ToF1-again", t.player.click_loc, "grandtree_ladderbottom", 1)
        t.ticks(3)
        t.exec("climbGrandTreeF1ToF2-again", t.player.click_loc, "grandtree_laddermiddle_bottom", 2)
        t.ticks(3)
        t.exec("climbGrandTreeF2ToF3-again", t.player.click_loc, "grandtree_laddermiddle_top", 2)
        t.ticks(3)
        t.expect("talkToCharlie3.present", t.npc.await_present("grandtree_charlie", 8, 10))
        t.exec("talkToCharlie3", t.player.talk_to, "grandtree_charlie", 1)
        t.exec("talkToCharlie3-dialog", t.chat.play, {
            "player:How are you doing Charlie", "npc:I've been better", "player:Glough has some plan",
            "npc:wouldn't put it past him", "player:need some proof", "npc:you could be in luck",
            "player:Where does she live", "npc:west of the toad swamp", "player:see what I can find", "end" })
        t.ticks(2)
        t.expect("quest.stage.clue_charlie", t.quest.expect_stage("clue_charlie"))

        -- climbUpToAnita / talkToAnita
        t.exec("goto-climbUpToAnita", t.player.goto_tile, 2390, 3512, 0)
        t.exec("climbUpToAnita", t.player.click_loc, "spiralstairs_wooden", 1)
        t.ticks(3)
        t.exec("goto-talkToAnita", t.player.goto_tile, 2391, 3514, 1)
        t.exec("talkToAnita", t.player.talk_to, "grandtree_anita", 1)
        t.exec("talkToAnita-dialog", t.chat.play, {
            "player:Hello there", "npc:Oh hello, I've seen you with the King", "player:Yes, I'm helping him",
            "npc:You must know my boyfriend Glough", "player:Indeed", "npc:Could you do me a favour",
            "player:I suppose so", "npc:Please give this key", "mesbox:Anita gives you a key", "npc:Thanks a lot",
            "player:No...thank you" })

        -- findInvasionPlans: Glough's key opens his chest
        t.exec("goto-climbUpToGloughAgain", t.player.goto_tile, 2476, 3462, 0)
        t.exec("climbUpToGloughAgain", t.player.click_loc, "ladder", 1)
        t.ticks(3)
        t.exec("findInvasionPlans", t.player.use_on, "grandtree_gloughskey", t.player.by_symbol("loc", "grandtree_chestclosed"))
        t.exec("findInvasionPlans-dialog", t.chat.play, { "mesbox:You have found a scroll" })
        t.ticks(2)
        t.expect("quest.stage.found_invasion_plans", t.quest.expect_stage("found_invasion_plans"))

        -- takeInvasionPlansToKing
        t.exec("goto-takeInvasionPlansToKing", t.player.goto_tile, 2467, 3498, 0)
        t.exec("takeInvasionPlansToKing", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("takeInvasionPlansToKing-dialog", t.chat.play, {
            "player:Hi, your highness, did you think", "npc:Look, if you're right about Glough",
            "player:Look, I found this at Glough's home", "mesbox:You give the King the invasion plans",
            "npc:If these are to be believed", "npc:But it's not proof", "mesbox:The King has given you some twigs",
            "npc:On the other hand", "npc:The Grand Tree's still slowly dying" })
        t.ticks(2)
        t.expect("quest.stage.given_twigs", t.quest.expect_stage("given_twigs"))

        -- climbUpToGloughForWatchtower / climbUpToWatchtower / placeTwigs
        t.exec("goto-climbUpToGloughForWatchtower", t.player.goto_tile, 2476, 3462, 0)
        t.exec("climbUpToGloughForWatchtower", t.player.click_loc, "ladder", 1)
        t.ticks(3)
        t.exec("climbUpToWatchtower", t.player.click_loc, "grandtree_climbtree", 1)
        t.ticks(4)
        t.exec("placeTwigsT", t.player.use_on, "grandtree_twigt", t.player.by_symbol("loc", "grandtree_pillart"))
        t.exec("placeTwigsU", t.player.use_on, "grandtree_twigu", t.player.by_symbol("loc", "grandtree_pillaru"))
        t.exec("placeTwigsZ", t.player.use_on, "grandtree_twigz", t.player.by_symbol("loc", "grandtree_pillarz"))
        t.exec("placeTwigsO", t.player.use_on, "grandtree_twigo", t.player.by_symbol("loc", "grandtree_pillaro"))
        t.ticks(2)
        t.expect("quest.stage.unlocked_trapdoor", t.quest.expect_stage("unlocked_trapdoor"))

        -- climbDownTrapDoor: Glough's black demon
        t.exec("climbDownTrapDoor", t.player.click_loc, "grandtree_trapdoortoweropen", 1)
        t.expect("glough.page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "Glough headache page" }, 60))
        t.exec("climbDownTrapDoor-dialog", t.chat.play, {
            "npc:You really are becoming a headache", "player:You're crazy Glough", "npc:Bah! Well, soon you'll see",
            "player:What makes you think", "npc:Fool...meet my little friend", "end" })
        t.expect("demon.present", t.npc.await_present("grandtree_blackdemon", 20, 40))
        t.exec("killBlackDemon", t.npc.await_dead, "grandtree_blackdemon", 400, 20, 10, { eat = { item = "shark", below = 45 } })
        t.ticks(3)
        t.expect("quest.stage.defeated_black_demon", t.quest.expect_stage("defeated_black_demon"))

        -- climbDownTrapDoorAfterFight / talkToKingAfterFight
        t.exec("goto-talkToKingAfterFight", t.player.goto_tile, 2465, 9896, 0)
        t.exec("talkToKingAfterFight", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("talkToKingAfterFight-dialog-1", t.chat.play, {
            "npc:Traveller you're wounded", "player:It's Glough", "npc:What?! Glough", "player:Glough has a store",
            "npc:Never! Not Glough" })
        t.expect("king.guard_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "guard page" }, 40))
        t.exec("talkToKingAfterFight-dialog-2", t.chat.play, { "npc:Guard!" })
        t.expect("king.sire_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "guard Sire page" }, 60))
        t.exec("talkToKingAfterFight-dialog-3", t.chat.play, { "npc:Sire!", "npc:Go and check" })
        t.expect("king.found_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "guard found Glough page" }, 60))
        t.exec("talkToKingAfterFight-dialog-4", t.chat.play, {
            "npc:We found Glough", "player:That's what I've been trying to tell you", "npc:I..I don't know what to say",
            "npc:Guard! Call off", "npc:The humans are not attacking", "npc:Yes sir", "npc:You have my full apologies",
            "npc:And my gratitude", "npc:A reward will have to wait", "npc:Help us search, we have little time!", "end" })
        t.ticks(2)
        t.expect("quest.stage.searching_daconia", t.quest.expect_stage("searching_daconia"))

        -- findDaconiaStone: the root the quest picked (daconia_coords), absolute x = 2432 + X, z = 9856 + Z
        local roots = {
            { "largeroot_gnome", 2456, 9886 }, { "largeroot_gnome", 2456, 9886 }, { "largeroot2_gnome", 2457, 9881 },
            { "largeroot2_gnome", 2455, 9874 }, { "largeroot_gnome", 2443, 9878 }, { "largeroot2_gnome", 2439, 9881 },
            { "largeroot2_gnome", 2444, 9893 }, { "largeroot_gnome", 2452, 9893 }, { "largeroot2_gnome", 2465, 9891 },
            { "largeroot2_gnome", 2468, 9890 }, { "largeroot_gnome", 2467, 9896 }, { "largeroot_gnome", 2473, 9897 },
            { "largeroot2_gnome", 2481, 9904 }, { "largeroot_gnome", 2485, 9885 }, { "largeroot_gnome", 2490, 9889 },
            { "largeroot2_gnome", 2467, 9872 },
        }
        local root_result, root_index = t.var.server("daconia_rock_root")
        t.check("findDaconiaStone.root", root_result == "ok" and root_index ~= nil and roots[root_index + 1] ~= nil,
            "daconia_rock_root = " .. tostring(root_index))
        local root = roots[(root_index or 0) + 1]
        t.exec("goto-findDaconiaStone", t.player.goto_tile, root[2], root[3] - 1, 0)
        t.exec("findDaconiaStone", t.player.click_loc, root[1], 1, { at = { root[2], root[3], 0 } })
        t.exec("findDaconiaStone-dialog", t.chat.play, { "mesbox:You've found a Daconia Rock" })
        t.exec("findDaconiaStone.have", t.inv.expect_has, "grandtree_daconiarock", 1)

        -- giveDaconiaStoneToKingNarnode
        t.exec("goto-giveDaconiaStoneToKingNarnode", t.player.goto_tile, 2465, 9896, 0)
        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("giveDaconiaStone.snapshot", snapshot_result == "ok", "snapshot " .. tostring(snapshot_result))
        t.exec("giveDaconiaStoneToKingNarnode", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("giveDaconiaStoneToKingNarnode-dialog", t.chat.play, {
            "npc:Traveller, have you managed to find the Daconia", "player:Is this it", "npc:Yes! Excellent, well done",
            "mesbox:You give the King the Daconia rock", "npc:It's incredible", "npc:To think Glough had me fooled",
            "player:All that matters now", "npc:I'll drink to that", "npc:From now on I vow", "player:Thanks!",
            "player:I think!", "npc:It should make your stay", "player:Mine?", "npc:Very few know", "player:Strange!",
            "npc:That's magic trees for you", "npc:All the best Traveller", "player:You too, your highness", "end" })
        t.ticks(4)
        t.quest.expect_complete()

        -- rewards: 18400 Attack, 7900 Agility, 2150 Magic experience, 5 quest points
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 18400, snapshot))
        t.expect("reward.agility_xp", t.skill.expect_gain("agility", 7900, snapshot))
        t.expect("reward.magic_xp", t.skill.expect_gain("magic", 2150, snapshot))
        t.finish(0)
    end,
}
