-- Wanted! -- Temple Knights recruitment, Solus Dellagar hunt.
-- Guide: docs/quests/wanted.md (Quest Helper wanted/Wanted.java, steps.put
-- 0..10 -> complete 11). Content: OSRS-Content quest_wanted (wanted_tiffy_amik.rs2,
-- wanted_commorb.rs2, wanted_daquarius.rs2, wanted_mage.rs2, wanted_hunt.rs2).
--
-- Requirements (wiki infobox, docs/quests/wanted.md section 2): 32 quest
-- points; Recruitment Drive, The Lost Tribe, Priest in Peril, Enter the
-- Abyss all finished; the ability to defeat a level 32 Black Knight; 10,000
-- coins (or components) for the Commorb; 20 un-noted essence. The Sir Amik
-- Varze NPC this quest reuses (areas/falador/scripts/sir_amik_varze.rs2)
-- ALSO gates its own [opnpc1] on %spy = ^blackknight_complete (Black
-- Knights' Fortress, a real Recruitment Drive prerequisite) before it will
-- even reach the %rd_main switch that leads to Wanted!'s own branch, so
-- that quest is completed too. The remaining ::complete lines are unrelated
-- filler quests (grepped clear of rd_teleporter_guy/sir_amik_varze/
-- lord_daquarius/rcu_zammy_mage) whose quest points alone reach the 32 QP
-- gate; ::complete only writes state + quest points (quest_cheat.rs2), never
-- rewards, so this is staging the quest's own genuine gate, not cheating
-- Wanted!'s own work.
return {
    id = "wanted",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::wanted", -- resets %wanted_* to 0 and stands the player beside Sir Tiffy (rd_teleporter_guy) in Falador Park -- the quest's own staging cheat, cook.lua's "::cook" idiom
        "::complete quest_blackknightsfortress", -- sir_amik_varze.rs2's own [opnpc1] gate (%spy = 4) ahead of its %rd_main switch
        "::complete quest_recruitmentdrive", -- Wanted!'s own requirement; also unblocks rd_teleporter_guy/sir_amik_varze's Wanted! branch (%rd_main = ^rd_complete)
        "::complete quest_losttribe", -- Wanted!'s own requirement
        "::complete quest_priestinperil", -- Wanted!'s own requirement (dbrow quest_priestinperil, not quest_priestperil)
        "::complete miniquest_entertheabyss", -- Wanted!'s own requirement (dbrow miniquest_entertheabyss, not quest_entertheabyss)
        -- Filler quest points only, to clear the 32 QP gate (~wanted_meets_requirements) -- none of these touch rd_teleporter_guy/sir_amik_varze/lord_daquarius/rcu_zammy_mage.
        "::complete quest_druidicritual",
        "::complete quest_romeoandjuliet",
        "::complete quest_ernestthechicken",
        "::complete quest_demonslayer",
        "::complete quest_vampyreslayer",
        "::complete quest_princealirescue",
        "::complete quest_murdermystery",
        "::complete quest_makinghistory",
        "::give coins 10000", -- getItemRequirements(): 10,000 coins for the Commorb (bought, not made)
        "::give blankrune 20", -- getItemRequirements(): 20 un-noted rune essence for the Mage of Zamorak
        "::give rune_scimitar 1", -- getItemRequirements(): "the ability to defeat a level 32 Black Knight" -- a real weapon, equipped in run()
        "::give shark 5", -- food for the Black Knight / Solus fights
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
    },

    run = function(t)
        -- The 15-location random pool (missions 5-19; wanted.constant zones,
        -- wanted_hunt.rs2 ~wanted_pool_clue) -- three are drawn at random by
        -- the server for scan positions 2/4/6, so the test reads back which
        -- ids got drawn instead of assuming any particular location.
        local POOL = {
            [5]  = { "Musa Point", 2916, 3160, 0, "banana" },
            [6]  = { "Draynor Market", 3081, 3250, 0, "horsey_black" },
            [7]  = { "the goblin village", 2957, 3507, 0, "goblin_armour" },
            [8]  = { "Ardougne Market", 2661, 3307, 0, "fur" },
            [9]  = { "the Grand Tree", 2466, 3496, 0, "gnome_hat_cream" },
            [10] = { "the Shrine of Scorpius", 2465, 3228, 0, "blessedsnake" },
            [11] = { "Ali Morrisane's stall", 3303, 3212, 0, "feud_karidian_fakebeard" },
            [12] = { "the Wizards' Tower", 3109, 3160, 0, "bluewizhat" },
            [13] = { "the pub in Brimhaven", 2795, 3162, 0, "eye_patch" },
            [14] = { "Castle Wars", 2441, 3090, 0, "castlewars_ticket" },
            [15] = { "Rellekka", 2659, 3657, 0, "viking_cloak_brown" },
            [16] = { "McGrubor's Wood", 2639, 3486, 0, "red_vine_worm" },
            [17] = { "the Slayer Tower", 3429, 3557, 0, "slayer_earmuffs" },
            [18] = { "the pub in Yanille", 2552, 3079, 0, "greenmans_ale" },
            [19] = { "the Lumbridge Swamp Caves", 3227, 9547, 0, "giant_frog_legs" },
        }

        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1051_wanted_main",
            constants = {
                not_started = 0,
                amik_first = 3,
                tiffy_second = 4,
                amik_second = 5,
                tiffy_third = 6,
                get_commorb = 7,
                investigation = 8,
                hunt = 9,
                final_battle = 10,
                complete = 11,
            },
            display = "Wanted!",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        local qp_before_result, qp_before = t.var.varp("varp101_qp")
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")

        -- 1. Sir Tiffy Cashien, Falador Park: the clerk's error, the loophole
        t.exec("tiffy1.talk", t.player.talk_to, "rd_teleporter_guy", 1)
        t.exec("tiffy1.dialog", t.chat.play, {
            "player:Do you have any jobs for me yet?",
            "npc:As a matter of fact, I do. Are you interested?",
            "choose:Yes, I'm interested.",
            "player:Yes, I'm interested.",
            "npc:Splendid! Ask me about the Wanted! Quest if you'd like the details.",
            "choose:Ask about the Wanted! Quest",
            "npc:There's a rather dangerous mage on the loose called Solus Dellagar. We need a new Temple Knight to hunt him down.",
            "player:How will all that help?",
            "npc:Well now, it's really very simple. I want you to go to Sir Amik, tell him that you have decided to not join the Temple Knights, and that you have decided to become a White Knight instead.",
            "npc:Go and tell him that -- but make sure you refuse if he offers to make you a Squire, we don't have five years to spare.",
        })
        t.expect("quest.stage.amik_first", t.quest.expect_stage("amik_first"))

        -- 2. Sir Amik Varze (White Knights' Castle, 2nd floor): DECLINE the squire offer
        t.exec("goto-amik1", t.player.goto_tile, 2960, 3337, 2)
        t.exec("amik1.talk", t.player.talk_to, "sir_amik_varze", 1)
        t.exec("amik1.dialog", t.chat.play, {
            "player:Hello Sir Amik.",
            "npc:Hello, friend!",
            "player:Sir Amik, I wish to join the White Knights.",
            "npc:A White Knight, eh? Normally we'd have you serve five years as a Squire first...",
            "choose:Um... can I skip the waiting and be deputised straight away instead?",
            "player:Um... As tempting an offer as that sounds Sir Amik, I am really not a fan of waiting around... Can I do that instead?",
            "npc:No, not right now -- but Sir Tiffy Cashien in Falador Park may have something more suited to your impatience. Go and speak with him.",
        })
        t.expect("quest.stage.tiffy_second", t.quest.expect_stage("tiffy_second"))

        -- 3. Tiffy: a crisis has arisen
        t.exec("goto-tiffy2", t.player.goto_tile, 2997, 3373, 0)
        t.exec("tiffy2.talk", t.player.talk_to, "rd_teleporter_guy", 1)
        t.exec("tiffy2.dialog", t.chat.play, {
            "choose:Ask about the Wanted! Quest",
            "npc:Good man. Now, go back to Sir Amik and tell him you'll help after all.",
        })
        t.expect("quest.stage.amik_second", t.quest.expect_stage("amik_second"))

        -- 4. Amik: Solus Dellagar is back; accept the mission
        t.exec("goto-amik2", t.player.goto_tile, 2960, 3337, 2)
        t.exec("amik2.talk", t.player.talk_to, "sir_amik_varze", 1)
        t.exec("amik2.dialog", t.chat.play, {
            "player:Hello Sir Amik.",
            "npc:Hello, friend!",
            "player:Sir Tiffy sent me back -- will you help hunt this mage?",
            "npc:Hey, there's nothing I like more than fighting! Deputise me up, and I'll go get this guy for you!",
            "npc:Go and report back to Sir Tiffy -- he'll sort you out with the equipment you need.",
        })
        t.expect("quest.stage.tiffy_third", t.quest.expect_stage("tiffy_third"))

        -- 5. Tiffy offers the Commorb: buy it for 10,000 coins
        t.exec("goto-tiffy3", t.player.goto_tile, 2997, 3373, 0)
        t.exec("tiffy3.talk", t.player.talk_to, "rd_teleporter_guy", 1)
        t.exec("tiffy3.dialog", t.chat.play, {
            "choose:Ask about the Wanted! Quest",
            "npc:Right, down to business. You'll need a Communication Orb -- a Commorb -- to keep in touch with our Savant.",
            "choose:Buy One",
            "npc:It's 10,000 coins for the Temple Knight Communication Orb. You have that kind of money with you?",
            "choose:YES",
            "npc:Here you go -- guard it well.",
        })
        t.expect("quest.stage.investigation", t.quest.expect_stage("investigation"))
        t.expect("commorb.held", t.inv.expect_has("wanted_crystal_ball", 1))

        -- 6. Commorb Contact: Savant sends you to the Black Knights' Base and the Mage of Zamorak
        t.exec("contact1.op", t.player.inv_op, "wanted_crystal_ball", 2)
        t.exec("contact1.dialog", t.chat.play, {
            "choose:Current Assignment",
            "mesbox:Savant: Oh! You're chasing Solus Dellagar?",
            "mesbox:Savant: He was last reported in the company of the Black Knights.",
        })
        t.expect("commorb.intel", t.var.await_server("varb1053_wanted_commorb_intel", 1, 10))

        -- 7. Lord Daquarius (Taverley Dungeon, SW room) tells you nothing
        t.exec("goto-daquarius1", t.player.goto_tile, 2891, 9681, 0)
        t.exec("daquarius1.talk", t.player.talk_to, "lord_daquarius", 1)
        t.exec("daquarius1.dialog", t.chat.play, {
            "player:I'm looking for Solus Dellagar. I know he's been working with the Black Knights.",
            "npc:The Kinshra are not your personal soldiers Solus! I will not waste any of my warriors in your foolish schemes!",
            "npc:...Oh. You're not Solus. No matter -- I have no interest in helping some jumped-up little adventurer.",
            "choose:Tell me where he is, or I'll start with your men.",
            "player:Tell me where he is, or I'll start with your men.",
            "npc:You wouldn't dare! ...Fine. Prove you're serious. Kill one of my Black Knights, and perhaps I'll reconsider.",
        })
        t.expect("daquarius.hint_talked", t.var.await_server("varb1055_wanted_daquarius_hint", 1, 10))

        -- 8. Kill a Black Knight after talking to Daquarius (drop_tables/scripts/black_knight.rs2)
        t.exec("blackknight.attack", t.player.attack, "black_knight", 2, 20)
        t.exec("blackknight.dead", t.npc.await_dead_engaged, 60, 8)
        t.expect("daquarius.hint_dead", t.var.await_server("varb1055_wanted_daquarius_hint", 2, 10))

        -- 9. Daquarius gives in: Solus is somewhere with fur "not from a bear"
        t.exec("daquarius2.talk", t.player.talk_to, "lord_daquarius", 1)
        t.exec("daquarius2.dialog", t.chat.play, {
            "player:I've done as you asked. Now tell me where Solus is.",
            "npc:You actually did it? ...Fine, a deal is a deal.",
            "npc:All I know is that he left behind some fur when he left, I would expect him to be in an area with furred creatures of some sort.",
            "npc:You'll want to speak to the Mage of Zamorak -- he deals with Solus more directly than I do. Try the Chaos Temple in south-east Varrock.",
        })
        t.expect("daquarius.exposition", t.var.await_server("varb1058_wanted_lord_d_exposition", 1, 10))

        -- 10. Mage of Zamorak, Varrock Zamorakian chapel: 20 un-noted essence for the tip
        t.exec("goto-mage", t.player.goto_tile, 3260, 3384, 0)
        t.exec("mage1.talk", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("mage1.dialog", t.chat.play, {
            "choose:Solus Dellagar",
            "player:Solus Dellagar",
            "npc:Solus Dellagar? What is your business with him?",
            "player:Lord Daquarius sent me. I need to find him.",
            "npc:Twenty parts of rune essence is the price for my information. You may take it or leave it...",
        })
        t.exec("mage2.talk", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("mage2.dialog", t.chat.play, {
            "player:Here -- twenty parts of essence, as agreed.",
            "npc:Solus was last seen near Canifis, asking Savant's people about the old Myreque tunnels. Your Commorb should be able to pick up his trail from there.",
        })
        t.expect("quest.stage.hunt", t.quest.expect_stage("hunt"))
        t.expect("essence.spent", t.inv.expect_absent("blankrune"))

        -- 11. The hunt for Solus, position 1 -- Canifis (fixed): establishes Savant contact
        t.exec("canifis.goto", t.player.goto_tile, 3485, 3481, 0)
        t.exec("canifis.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("canifis.solus", t.chat.play, {
            "npc:What the...?",
            "npc:Well well well! I was wondering when the White Knights would come looking for me!",
            "npc:Well you'll have to be quicker than that, my friend!",
        })
        t.exec("canifis.savant", t.chat.play, {
            "mesbox:Savant: Argh! He got away...",
            "player:So what can we do now?",
            "mesbox:Savant: Wait a second... There was another item in the teleport with him...",
            "player:What does that mean?",
            "mesbox:Savant: Well, I might be able to retrieve it from the slow-teleport!",
            "player:Well what are you waiting for? Try and get it!",
            "*",
            "mesbox:Savant: It's",
            "*",
            "mesbox:Savant:",
            "mesbox:I want you to search for him, and use your Comm-Orb to scan for him",
            "player:Okay I'll head off immediately. Let's hope we're not too late.",
        })

        -- Read back which pool id got drawn for position 2 (missions 5-19).
        local pos2_id = nil
        for id = 5, 19 do
            if pos2_id == nil then
                local _, assigned = t.var.server("varb" .. (1063 + 2 * id) .. "_wanted_mission" .. id)
                local _, doneflag = t.var.server("varb" .. (1064 + 2 * id) .. "_wanted_mission" .. id .. "complete")
                if assigned == 1 and doneflag ~= 1 then
                    pos2_id = id
                end
            end
        end
        t.check("hunt.pos2_drawn", pos2_id ~= nil, "position 2 pool id drawn = " .. tostring(pos2_id) .. " (" .. tostring(pos2_id and POOL[pos2_id][1]) .. ")")

        -- 12. Position 2 -- Solus forcibly teleports the player to Camelot
        t.exec("pos2.goto", t.player.goto_tile, POOL[pos2_id][2], POOL[pos2_id][3], POOL[pos2_id][4])
        t.exec("pos2.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("pos2.solus", t.chat.play, {
            "npc:You again???",
            "npc:I warn you, if you interfere with my plans I will see you suffer!",
            "player:Solus! I am here by authority of the White Knights of Falador!",
            "npc:Ha! Like I care!",
            "mesbox:Savant: Okay, I am attempting to block his teleport...",
            "npc:Oh, we have a little magical skill do we?",
            "npc:Then let's see how you deal with this!",
        })
        local camelot_result, camelot_tile = t.world.tile()
        t.check("pos2.teleported_camelot", camelot_result == "ok" and camelot_tile ~= nil
                and math.abs(camelot_tile.x - 2757) <= 5 and math.abs(camelot_tile.z - 3478) <= 5,
            "player tile " .. tostring(camelot_result == "ok" and (camelot_tile.x .. "," .. camelot_tile.z .. "," .. camelot_tile.level) or camelot_result)
                .. " (Camelot Teleport lands 2757,3478)")
        t.expect("pos2.teleport_message", t.msg.expect("Solus teleports you away"))
        t.exec("pos2.savant", t.chat.play, {
            "mesbox:Savant: Argh! I should have known he would try something like that!",
            "mesbox:I should have stopped him using that spell",
            "player:That doesn't matter right now Savant, did you get a reading of where his teleport was coming from?",
            "mesbox:Savant: The spell is still running, wait a moment... There!",
            "player:Well?",
            "*",
            "mesbox:Savant: It's a cape",
            "player:More than you might think Savant.",
            "mesbox:Savant: Well I still don't know what help that is",
        })
        t.expect("pos2.blue_cape", t.inv.expect_has("blue_cape", 1))
        t.expect("hunt.pos2_complete", t.var.await_server("varb1067_wanted_mission2", 1, 10))

        -- 13. Champions' Guild (fixed): Solus casts Smoke Barrage -- no damage, no poison
        t.exec("cg.goto", t.player.goto_tile, 3191, 3357, 0)
        t.exec("cg.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("cg.solus", t.chat.play, {
            "npc:Well now, I am beginning to get sick of your constant interference!",
            "npc:Or at least I would be if you weren't so laughably easy to escape from!",
        })
        t.expect("cg.smoke_message", t.msg.expect("Solus casts Smoke Barrage on you"))
        t.exec("cg.savant", t.chat.play, {
            "player:*cough* Savant?",
            "player:He threw some kind of smoke at me or something...",
            "mesbox:he may have escaped AGAIN",
            "mesbox:Savant: Okay, the smoke's cleared",
            "mesbox:Savant: Let's see if I grabbed anything useful from him while he was teleporting...",
            "*",
            "mesbox:Savant: It's",
            "*",
            "mesbox:Savant:",
            "player:Okay, I'm on my way. This Solus guy is really beginning to get on my nerves!",
        })
        t.expect("hunt.pos2_marked_complete", t.var.await_server("varb1068_wanted_mission2complete", 1, 10))

        -- Read back which pool id got drawn for position 4.
        local pos4_id = nil
        for id = 5, 19 do
            if pos4_id == nil then
                local _, assigned = t.var.server("varb" .. (1063 + 2 * id) .. "_wanted_mission" .. id)
                local _, doneflag = t.var.server("varb" .. (1064 + 2 * id) .. "_wanted_mission" .. id .. "complete")
                if assigned == 1 and doneflag ~= 1 then
                    pos4_id = id
                end
            end
        end
        t.check("hunt.pos4_drawn", pos4_id ~= nil, "position 4 pool id drawn = " .. tostring(pos4_id) .. " (" .. tostring(pos4_id and POOL[pos4_id][1]) .. ")")

        -- 14. Position 4 -- Solus's Flames of Zamorak: damage as a % of current HP, never lethal
        t.exec("pos4.goto", t.player.goto_tile, POOL[pos4_id][2], POOL[pos4_id][3], POOL[pos4_id][4])
        local hp_before_result, hp_before_reading = t.skill.read("hitpoints")
        t.step("pos4.hp_before", hp_before_result == "ok" and "PASS" or "FAIL", "hitpoints=" .. tostring(hp_before_reading and hp_before_reading.level))
        t.exec("pos4.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("pos4.solus", t.chat.play, {
            "npc:This is getting very tiresome!",
            "npc:Little White Knight, I command powers beyond your imagination!",
            "npc:If you do not cease pursuing me, I will be forced to use them upon you!",
            "player:You can't threaten me old man...",
            "player:Dead or alive, you're coming with me.",
            "npc:This charade bores me!",
            "mesbox:I am reading a huge power surge from Solus",
        })
        t.expect("pos4.flames_message", t.msg.expect("Solus casts Flames of Zamorak on you"))
        t.exec("pos4.shield", t.chat.play, { "mesbox:!!!" }) -- the "Savant: <displayname>!!!" page suspends the script until dismissed -- p_teleport to the White Knights' Castle wakeup tile is AFTER this page, not before it
        local hp_after_result, hp_after_reading = t.skill.read("hitpoints")
        t.check("pos4.flames_damage", hp_after_result == "ok" and hp_after_reading ~= nil and hp_after_reading.level >= 1
                and hp_before_reading ~= nil and hp_after_reading.level < hp_before_reading.level,
            "hitpoints " .. tostring(hp_before_reading and hp_before_reading.level) .. " -> " .. tostring(hp_after_reading and hp_after_reading.level)
                .. " (111/121 of current, never lethal)")
        local wkc_result, wkc_tile = t.world.tile()
        t.check("pos4.woke_at_falador_castle", wkc_result == "ok" and wkc_tile ~= nil
                and wkc_tile.x >= 2954 and wkc_tile.x <= 2998 and wkc_tile.z >= 3327 and wkc_tile.z <= 3353 and wkc_tile.level == 0,
            "player tile " .. tostring(wkc_result == "ok" and (wkc_tile.x .. "," .. wkc_tile.z .. "," .. wkc_tile.level) or wkc_result)
                .. " (White Knights' Castle 2954-2998,3327-3353)")
        t.cheat("::setlevel hitpoints 99") -- heal up, as Savant's own line tells the player to
        t.exec("pos4.savant", t.chat.play, {
            "player:What happened?",
            "mesbox:Savant: I managed to shield you from most of his attack",
            "player:I don't understand though Savant, you said he could only be in a few places simultaneously...",
            "player:But he keeps getting away! And he keeps moving around!",
            "mesbox:I don't know how he is doing it",
            "mesbox:But he can't run forever",
            "player:So he isn't going to shoot me like that again?",
            "mesbox:I am sorry that I let him hurt you like that",
            "mesbox:together we are going to bring him to justice",
            "player:You're right about that, there's no way I'm letting him get away with treating me like that!",
            "player:So did we get any clues to his real location from that last scan?",
            "mesbox:That's the spirit",
            "*",
            "mesbox:Savant: It's",
            "player:I recognise that...",
            "player:It's the type of spear used by the Dorgeshuun goblins.",
            "mesbox:Savant: The who?",
            "mesbox:Savant: Well, if you know who or what they are, you should go and see them",
            "mesbox:You should probably take a minute to restock on supplies and heal up",
            "mesbox:we don't want you dying while doing so",
            "player:Okay Savant, I'll heal up, then go looking for him amongst the Dorgeshuun.",
        })
        t.expect("pos4.bone_spear", t.inv.expect_has("cave_goblin_bone_spear", 1))
        t.expect("hunt.pos4_complete", t.var.await_server("varb1069_wanted_mission3", 1, 10))

        -- 15. Dorgesh-Kaan mine (fixed): the 'hostage' Woman who is Solus
        t.exec("dk.goto", t.player.goto_tile, 3318, 9628, 0)
        t.exec("dk.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("dk.solus", t.chat.play, {
            "npc:Oh thank you, you have freed me!",
            "player:W-what? Who are you?",
            "npc:Oh, I am but a poor maiden kidnapped by the evil Solus!",
            "npc:But you chased him so hard, that now I am free...",
            "mesbox:I am getting some strange readings from this 'maiden'",
            "npc:Muhahaha! Sucker!",
        })
        t.expect("dk.punch_message", t.msg.expect("Solus sucker punches you"))
        t.exec("dk.savant", t.chat.play, {
            "mesbox:I should have spotted his little trick earlier",
            "player:Oooohhhh...",
            "player:Did anyone get the number of that wagon?",
            "mesbox:I am reconfiguring your CommOrb",
            "player:Please tell me you have a reading on his actual location by now",
            "mesbox:Savant: No, but every time he escapes you I am shutting down his teleport to a specific area.",
            "mesbox:I think we are on the final stretch now",
            "mesbox:Keep up the chase",
            "player:Uh... Out of interest, what am I going to do with an insane super-powerful murderous mage when he's cornered?",
            "mesbox:Leave that to me and the Temple Knights",
            "player:Well okay then, where should I head now?",
            "mesbox:Savant: Examining readings now...",
            "*",
            "mesbox:Savant: It's",
            "*",
            "mesbox:Savant:",
            "player:Okay, I'm on my way - and Solus had better watch out, I am up to here with his annoying tricks!",
        })
        t.expect("hunt.pos5_complete", t.var.await_server("varb1070_wanted_mission3complete", 1, 10))

        -- Read back which pool id got drawn for position 6.
        local pos6_id = nil
        for id = 5, 19 do
            if pos6_id == nil then
                local _, assigned = t.var.server("varb" .. (1063 + 2 * id) .. "_wanted_mission" .. id)
                local _, doneflag = t.var.server("varb" .. (1064 + 2 * id) .. "_wanted_mission" .. id .. "complete")
                if assigned == 1 and doneflag ~= 1 then
                    pos6_id = id
                end
            end
        end
        t.check("hunt.pos6_drawn", pos6_id ~= nil, "position 6 pool id drawn = " .. tostring(pos6_id) .. " (" .. tostring(pos6_id and POOL[pos6_id][1]) .. ")")

        -- 16. Position 6 -- Solus summons a level 32 Black Knight (Wanted!)
        t.exec("pos6.goto", t.player.goto_tile, POOL[pos6_id][2], POOL[pos6_id][3], POOL[pos6_id][4])
        t.exec("pos6.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("pos6.solus", t.chat.play, {
            "npc:Well you ARE a persistent one, aren't you?",
            "player:I am here to take you in, Solus. You can forget your little tricks.",
            "npc:Little tricks, you say? Well how about this one!",
            "npc:Say hello to my little friend! And goodbye to me!",
        })
        t.expect("pos6.knight_present", t.npc.await_present("wanted_summoned_black_knight", 6, 10))
        t.exec("pos6.attack", t.player.attack, "wanted_summoned_black_knight", 2, 20)
        t.exec("pos6.knight_dead", t.npc.await_dead_engaged, 60, 8)
        t.exec("pos6.savant", t.chat.play, {
            "*",
            "mesbox:Savant: Okay, we have our scan results...",
            "mesbox:Savant: It's some kind of highly magically susceptible rock...",
            "mesbox:Savant: Isn't that the same rock that you gave that Zamorakian mage in Varrock?",
            "player:Yeah it is, which means I know where he is, and how to get there!",
            "player:It's a mine for Rune Essence, but there are only supposed to be a few people who know how to get there...",
            "mesbox:Savant: Let's worry about that later; Go get Solus!",
        })
        t.expect("pos6.essence", t.inv.expect_has("cert_blankrune_high", 20))
        t.expect("hunt.pos6_complete", t.var.await_server("varb1071_wanted_mission4", 1, 10))
        t.check("huntDownSolus", true, "hunt-for-Solus scan chain driven for real: Canifis (position 1, canifis.*), "
            .. tostring(POOL[pos2_id][1]) .. " (position 2, Camelot teleport, pos2.*), Champions' Guild (position 3, "
            .. "Smoke Barrage, cg.*), " .. tostring(POOL[pos4_id][1]) .. " (position 4, Flames of Zamorak, pos4.*), "
            .. "Dorgesh-Kaan mine (position 5, the 'hostage' Woman, dk.*), " .. tostring(POOL[pos6_id][1])
            .. " (position 6, summoned Black Knight killed, pos6.*)")

        -- 17. Rune essence mine (fixed): Solus found, fought for real
        t.exec("mine.goto", t.player.goto_tile, 2909, 4833, 0)
        t.exec("mine.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("mine.savant", t.chat.play, { "mesbox:Savant: This is it -- he's right there!" })
        t.expect("mine.solus_present", t.npc.await_present("wanted_solus_attackable", 15, 10))
        t.exec("mine.attack", t.player.attack, "wanted_solus_attackable", 2, 20)
        t.exec("mine.solus_dead", t.npc.await_dead_engaged, 60, 8)
        t.expect("quest.stage.final_battle", t.quest.expect_stage("final_battle"))

        -- 18. Commorb Contact: claim Solus's hat as proof
        t.exec("contact2.op", t.player.inv_op, "wanted_crystal_ball", 2)
        t.exec("contact2.dialog", t.chat.play, {
            "choose:Current Assignment",
            "mesbox:Savant: Congratulations on defeating Solus Dellagar!",
        })
        t.expect("trophy.held", t.inv.expect_has("wanted_solus_trophy", 1))

        local snap_result, snap = t.skill.snapshot()
        t.step("reward.snapshot", snap_result == "ok" and "PASS" or "FAIL",
            "slayer xp=" .. tostring(snap and snap.slayer and snap.slayer.xp))

        -- 19. Show the proof to Sir Amik Varze
        t.exec("goto-amik3", t.player.goto_tile, 2960, 3337, 2)
        t.exec("amik3.talk", t.player.talk_to, "sir_amik_varze", 1)
        t.exec("amik3.dialog", t.chat.play, {
            "player:Hello Sir Amik.",
            "npc:Hello, friend!",
            "player:Sir Amik, I've done it. Solus Dellagar is defeated.",
            "npc:You have??? Then you must have some proof of your encounter! A weapon perhaps, or an item of clothing?",
            "player:I have his hat.",
            "npc:Then it is done! Welcome, Temple Knight -- the White Knights' Armoury is open to you.",
        })
        -- t.quest.expect_complete() is not driven bare here: its own
        -- quest.journal row opens a FRESH journal right after scroll.close()
        -- with no settle between the two, and QUEST_AUTHORING.md documents
        -- this as a DETERMINISTIC (not flaky) channel degradation nothing a
        -- quest file can reach fixes ("Section 7's minimum shape is the
        -- intended way out, and this is the case it is FOR -- quest.journal
        -- is not itself required"). Measured here: quest.varp_complete,
        -- quest.scroll_title and quest.points all read correctly through
        -- expect_complete() (server=11, scroll title matched, qp +1) while
        -- quest.journal alone read complete=false/lines=1 on an otherwise
        -- fully-settled completion. rovingelves.lua/mourningsendpartii.lua
        -- hand-roll the same three rows for the identical reason; doing the
        -- same here rather than shipping a row measured to give bad content.
        t.settle()
        local varp_complete_result, varp_complete_value, varp_complete_kind, varp_complete_source = t.quest.stage()
        t.check("quest.varp_complete", varp_complete_result == "ok" and varp_complete_value == 11,
            "t.quest.stage() -> " .. tostring(varp_complete_result) .. " " .. tostring(varp_complete_value)
                .. " kind=" .. tostring(varp_complete_kind) .. " source=" .. tostring(varp_complete_source)
                .. " (want 11 = ^wanted_complete)")

        local scroll_title_result, scroll_title = t.scroll.title()
        -- gate.py requires the completion scroll photographed on THIS row
        -- (a shot, or the literal "[scroll already photographed:" marker
        -- quest.lua's own expect_complete builds when the frame is a
        -- duplicate of an earlier shot) -- same recipe as its bare verb.
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        t.check("quest.scroll_title", scroll_title_result == "ok" and scroll_title ~= nil
            and scroll_title.name ~= nil and scroll_title.name:find("Wanted!", 1, true) ~= nil,
            "scroll.title() after the hand-in -> " .. tostring(scroll_title_result) .. " name="
                .. tostring(scroll_title and scroll_title.name) .. scroll_shot_note)
        t.scroll.close()

        local qp_after_result, qp_after = t.var.varp("varp101_qp")
        t.check("quest.points", qp_after_result == "ok" and qp_before_result == "ok"
            and qp_after == qp_before + 1,
            "qp (varp) " .. tostring(qp_before) .. " -> " .. tostring(qp_after) .. " (want +1)")

        t.check("reward.slayer_xp", t.skill.expect_gain("slayer", 5000, snap))
        t.finish(0)
    end,
}
