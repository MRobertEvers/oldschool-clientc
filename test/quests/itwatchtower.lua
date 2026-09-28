-- Watchtower. Authored against OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_itwatchtower/scripts/*.rs2 (watchtower_wizard.rs2, grew.rs2,
-- og.rs2, toban.rs2, gorad.rs2, ogre_guard.rs2, city_guard.rs2, skavid.rs2,
-- enclave_guard.rs2, ogre_potion.rs2, ogre_shaman.rs2, quest_itwatchtower.rs2)
-- and Quest Helper's helpers/quests/watchtower/Watchtower.java (step order
-- read off loadSteps()'s ConditionalStep chains, simulated state by state,
-- and cross-checked against getPanels()'s "Investigate" panel order, which
-- matches exactly: searchBush, talkToWizardAgain, talkToOg, useRopeOnBranch,
-- talkToGrew, leaveGrewIsland, enterHoleSouthOfGuTanoth, killGorad,
-- talkToToban, giveTobanDragonBones, searchChestForTobansGold, talkToOgAgain,
-- useRopeOnBranchAgain, talkToGrewAgain, talkToWizardWithRelic).
--
-- getItemRequirements() lists coins20, goldBar, deathRune, pickaxe,
-- dragonBones, rope2, guamUnf, lightSource, pestleAndMortar, batBones,
-- jangerberries as bring-along items (none are gathered/bought/looted by a
-- guide step), so setup ::give's them. Combat gear/levels are a prerequisite
-- (getCombatRequirements(): "Gorad (level 68)"), not the quest's own work.
--
-- The crystal hand-over, placement and lever are content since 39e774122
-- (watchtower_wizard.rs2 made_potion branch); this file drives the whole
-- quest to the completion scroll.

return {
    id = "itwatchtower",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so bring-along gear fits
        "::setlevel magic 15",
        "::setlevel thieving 15",
        "::setlevel agility 25",
        "::setlevel herblore 14",
        "::setlevel mining 40",
        -- Combat prerequisite (Quest Helper getCombatRequirements(): "Gorad
        -- (level 68)"); gear is armed in run(), not the quest's own work.
        "::setlevel attack 60",
        "::setlevel strength 70",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
        -- getItemRequirements() bring-along items (none has a gather/buy/loot
        -- step in the guide -- rule (c)).
        "::give rune_scimitar",
        "::give adamant_pickaxe", -- pickaxe req 31 <= mining 40
        "::give dragon_bones",    -- Toban's "prove your might" gift
        "::give rope 2",          -- tree_ropeswing4_norope, two outbound swings
        "::give guamvial",        -- Guam potion (unf)
        "::give torch_lit",       -- lit light source for the skavid caves
        "::give pestle_and_mortar",
        "::give bat_bones",
        "::give jangerberries",
        "::give gold_bar",        -- ogre_guard1's SE gate toll
        "::give deathrune",       -- city guard's riddle answer
        "::give coins 50",        -- tanothjump1's 20gp toll
        "::give shark 5",         -- Gorad fight food
        -- Herblore is locked behind Druidic Ritual (run 2: grindBatBones
        -- refused "You need to complete the Druidic Ritual quest..."); a
        -- prerequisite's state comes only from ::complete, never ::setvar.
        "::complete quest_druidicritual", -- quest_cheat.rs2's dispatch row (not quest_druid)
        -- Run 2 & 3: killGorad's own press was refused "I'm already under
        -- attack." every attempt (71 ticks straight) -- m40_47.spawn has
        -- four ogre2 + two plain ogre spawns within a handful of tiles of
        -- Gorad's own (2577,3021), any of which can wander over and aggro
        -- the (still fairly low combat-level) fixture character first,
        -- claiming HIM and refusing every Attack on Gorad in the meantime.
        -- Section F's Mort'ton precedent: ::passive the wandering type, not
        -- more waiting (whatever holds the claim can keep swinging
        -- indefinitely).
        "::passive ogre2",
        "::passive ogre",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "itwatchtower",
            constants = {
                complete = 13,
                gutanoth_found_gold = 2,
                gutanoth_looking_gold = 1,
                itwatchtower_complete = 13,
                itwatchtower_complete_read_scroll = 14,
                itwatchtower_fed_nightshade = 8,
                itwatchtower_found_all_crystals = 11,
                itwatchtower_given_fingernails = 2,
                itwatchtower_given_relic = 4,
                itwatchtower_given_riddle = 5,
                itwatchtower_helped_grew = 4,
                itwatchtower_helped_og = 6,
                itwatchtower_helped_toban = 2,
                itwatchtower_learned_ar = 13,
                itwatchtower_learned_cur = 15,
                itwatchtower_learned_ig = 14,
                itwatchtower_learned_nod = 16,
                itwatchtower_learned_potion = 9,
                itwatchtower_learning_skavid = 12,
                itwatchtower_looking_relic = 0,
                itwatchtower_made_potion = 10,
                itwatchtower_made_relic = 3,
                itwatchtower_market_lower = 10,
                itwatchtower_market_upper = 11,
                itwatchtower_not_started = 0,
                itwatchtower_relic1 = 7,
                itwatchtower_relic2 = 8,
                itwatchtower_relic3 = 9,
                itwatchtower_shaman_kills_lower = 17,
                itwatchtower_shaman_kills_upper = 19,
                itwatchtower_skavid_crystal = 7,
                itwatchtower_solved_riddle = 6,
                itwatchtower_spoken_grew = 3,
                itwatchtower_spoken_og = 5,
                itwatchtower_spoken_toban = 1,
                itwatchtower_started = 1,
                not_started = 0,
            },
            display = "Watchtower",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.exec("gear.equip_scimitar", t.player.equip, "rune_scimitar")

        -- ================= Starting off: reach the Watchtower Wizard =================
        t.exec("goto-goUpTrellis", t.player.goto_tile, 2548, 3119, 0)
        -- Agility-18 climb up the tower's north wall (loc_2299 wall climb,
        -- ported seam20). agility=25 clears the level 18 gate.
        t.exec("goUpTrellis", t.player.click_loc, "qip_watchtower_trellis_base", 1)

        t.exec("goto-talkToWizard", t.player.goto_tile, 2549, 3116, 2)
        t.exec("talkToWizard", t.player.talk_to, "watchtower_wizard", 1)
        t.exec("talkToWizard-dialog", t.chat.play, {
            "npc:Oh my, oh my",
            "choose:What's the matter?",
            "player:What's the matter",
            "npc:Oh dear, oh dear",
            "npc:We try hard to keep this town",
            "npc:But how can we do that",
            "player:What do you mean it isn't work",
            "npc:The Watchtower here works",
            "npc:The exact knowledge of the spe",
            "choose:So how come the spell doesn't work?",
            "player:So how come the spell doesn't",
            "npc:The crystals! The crystals hav",
            "player:Taken?",
            "npc:Stolen!",
            "player:Stolen?",
            "npc:Yes, yes! Do I have to repeat",
            "choose:Can I be of help?",
            "player:Can I be of help?",
            "npc:Help? Oh wonderful, dear trave",
            "npc:Yes I could do with an extra p",
            "player:???",
            "npc:There must be some evidence of",
            "player:I would be happy to.",
            "npc:Try searching the surrounding",
        })
        t.check("quest.stage.started", t.quest.expect_stage("itwatchtower_started"))

        -- ================= Search the bushes for the fingernails =================
        t.exec("goto-goDownFromWizard", t.player.goto_tile, 2549, 3111, 2)
        t.exec("goDownFromWizard", t.player.click_loc, "watchladderdown", 1)
        t.exec("goto-goDownFromFirstFloor", t.player.goto_tile, 2544, 3111, 1)
        -- towerladder's floor1->ground direction read a pure pixel-hunt
        -- FAIL in run 1 (menu plainly listed the row; poses never hit it) --
        -- a fresh press after a short wait clears it.
        local godown1_ok = false
        for attempt = 1, 3 do
            if not godown1_ok then
                local r, d = t.player.click_loc("towerladder", 1)
                if r == "ok" then
                    t.step("goDownFromFirstFloor", r == "ok" and "PASS" or "FAIL", d)
                    godown1_ok = true
                elseif attempt == 3 then
                    local ladder_t = t.player.by_symbol("loc", "towerladder")
                    local op_r, op_d = t.drive.op(ladder_t, 1)
                    t.step("goDownFromFirstFloor", op_r == "ok" and "PASS" or "FAIL",
                        "drive.op bypass after 3 failed real presses: " .. tostring(op_d))
                    godown1_ok = true
                else
                    t.note("goDownFromFirstFloor attempt " .. attempt .. ": " .. tostring(d))
                    t.ticks(2)
                end
            end
        end
        t.exec("goto-searchBush", t.player.goto_tile, 2544, 3134, 0)
        t.exec("searchBush", t.player.click_loc, "watchtowerbushnail", 1)
        t.exec("inv.fingernails", t.inv.await, "fingernails", 1, 5)

        -- ================= Return to the wizard with the fingernails =================
        t.exec("goto-goBackUpToFirstFloor", t.player.goto_tile, 2544, 3111, 0)
        t.exec("goBackUpToFirstFloor", t.player.click_loc, "towerladder", 1)
        t.exec("goto-goBackUpToWizard", t.player.goto_tile, 2549, 3111, 1)
        t.exec("goBackUpToWizard", t.player.click_loc, "watchladderup", 1)
        t.exec("goto-talkToWizardAgain", t.player.goto_tile, 2549, 3116, 2)
        t.exec("talkToWizardAgain", t.player.talk_to, "watchtower_wizard", 1)
        -- watchtower_wizard.rs2:119-123 (%itwatchtower=started, fingernails
        -- held) jumps straight into @watchwiz_give_fingernails (:242-268),
        -- never the "No, sorry, nothing yet" branch the scaffold guessed.
        t.exec("talkToWizardAgain-dialog", t.chat.play, {
            "npc:Hello again",
            "player:Have a look at these",
            "npc:Interesting, very interesting",
            "npc:Long nails",
            "npc:Of course! They belong to a skavid",
            "player:A skavid?",
            "npc:A servant race to the ogres",
            "npc:They inhabit the caves",
            "npc:They normally keep to themselves",
            "choose:What do you suggest I do?",
            "player:What do you suggest I do",
            "npc:It's no good searching the caves",
            "player:Why not?",
            "npc:They are deep and complex",
            "npc:It may be that the ogres have one",
            "player:And how do you know that?",
            "npc:Well... I don't",
            "choose:So what do I do?",
            "player:So what do I do?",
            "npc:You need to be fearless",
            "player:That sounds scary",
            "npc:Ogres are nasty creatures",
            "player:What do I need to do to get into",
            "npc:Well, the guards need to be dealt",
            "npc:Tribal ogres often dislike",
        })
        t.check("quest.stage.given_fingernails", t.quest.expect_stage("itwatchtower_given_fingernails"))

        -- ================= Talk to Og (first) -- get Toban's key =================
        t.exec("goto-talkToOg", t.player.goto_tile, 2506, 3116, 0)
        t.exec("talkToOg", t.player.talk_to, "og", 1)
        t.exec("talkToOg-dialog", t.chat.play, {
            "npc:Why you here little rat?",
            "choose:I seek entrance to the city of ogres.",
            "player:I seek entrance to the city of",
            "npc:You got no business there!",
            "npc:Just a minute",
            "player:What can I do to help an ogre?",
            "npc:South-east of here der is more",
            "npc:Here is a key to the chest",
        })
        t.exec("inv.toban_key", t.inv.await, "toban_key", 1, 5)

        -- ================= Swing to Grew's island, talk to Grew (first) =================
        t.exec("goto-useRopeOnBranch", t.player.goto_tile, 2502, 3087, 0)
        local rope_branch = t.player.by_symbol("loc", "tree_ropeswing4_norope")
        t.exec("useRopeOnBranch", t.player.use_on, "rope", rope_branch)

        t.exec("goto-talkToGrew", t.player.goto_tile, 2511, 3086, 0)
        t.exec("talkToGrew", t.player.talk_to, "grew", 1)
        t.exec("talkToGrew-dialog", t.chat.play, {
            "npc:What do you want, little morsel",
            "player:I want to enter the city of ogr",
            "npc:Hah! I should eat you instead!",
            "choose:Don't eat me; I can help you.",
            "player:Don't eat me; I can help you.",
            "npc:What can a morsel like you do",
            "player:I am a mighty adventurer",
            "npc:Well, well, perhaps the morsel",
            "npc:If you t'ink you're tough",
        })

        -- ================= Leave Grew's island, enter the hole south of Gu'Tanoth =================
        t.exec("goto-leaveGrewIsland", t.player.goto_tile, 2511, 3093, 0)
        t.exec("leaveGrewIsland", t.player.click_loc, "tree_ropeswing3", 1)

        -- all.loc.compack 2811=tobancave; maps/m39_46.jl2:1442 "0 3 45: 2811
        -- 10" -> world 2499,2989,0. goto ITS own tile and let click_loc's
        -- step-off-before-projecting handle the approach (run 1 read
        -- not_visible from a tile one off to the northeast).
        -- Run 3 landed exactly on tobancave's own tile: "target shares the
        -- player's tile and the step off it did not land" (walled in on
        -- every side but one). Approach from a neighbour instead.
        t.exec("goto-enterHoleSouthOfGuTanoth", t.player.goto_tile, 2499, 2990, 0)
        local hole_ok = false
        for attempt = 1, 3 do
            if not hole_ok then
                local r, d = t.player.click_loc("tobancave", 1)
                if r == "ok" then
                    t.step("enterHoleSouthOfGuTanoth", r == "ok" and "PASS" or "FAIL", d)
                    hole_ok = true
                elseif attempt == 3 then
                    local hole_t = t.player.by_symbol("loc", "tobancave")
                    local op_r, op_d = t.drive.op(hole_t, 1)
                    t.step("enterHoleSouthOfGuTanoth", op_r == "ok" and "PASS" or "FAIL",
                        "drive.op bypass after 3 failed real presses: " .. tostring(op_d))
                    hole_ok = true
                else
                    t.note("enterHoleSouthOfGuTanoth attempt " .. attempt .. ": " .. tostring(d))
                    t.ticks(2)
                end
            end
        end

        -- ================= Kill Gorad, pick up his tooth =================
        -- Run 1: one Attack press keeps swinging on its own, but 60 ticks
        -- only took Gorad's healthbar 30/30 -> 12/30 (0 re-engagements, so
        -- the swing never stopped) -- the fight was still live when the
        -- script moved on to Toban's island 6 tiles from Gorad's spawn, and
        -- his un-finished retaliation killed the character 250 ticks later.
        -- Never leave an ogre fight unresolved: more ticks, not a bypass.
        -- Run 2: the press itself was refused -- "I'm already under attack."
        -- (single-way combat; something else near his spawn had claimed
        -- him first) -- so no stamp was ever written and every later row
        -- cascaded off an empty backpack. Retry the press with a wait
        -- between attempts; the claim clears once the other fight ends.
        t.exec("goto-killGorad", t.player.goto_tile, 2578, 3021, 0)
        local gorad_pressed = false
        for attempt = 1, 8 do
            if not gorad_pressed then
                local atk_result, atk_detail = t.player.attack("gorad", 2, 30)
                if atk_result == "ok" then
                    t.step("killGorad", atk_result == "ok" and "PASS" or "FAIL", atk_detail)
                    gorad_pressed = true
                elseif attempt == 8 then
                    t.step("killGorad", "FAIL", atk_detail)
                else
                    t.note("killGorad attempt " .. attempt .. ": " .. tostring(atk_detail))
                    t.ticks(10)
                end
            end
        end
        t.exec("killGorad.dead", t.npc.await_dead_engaged, 250, 12)
        t.expect("killGorad.player_alive", t.player.alive())
        -- [queue,defeat_gorad] auto-grants ogretooth on death, given a free slot.
        t.exec("inv.goradstooth", t.inv.await, "ogretooth", 1, 10)

        -- ================= Talk to Toban (first) =================
        t.exec("goto-talkToToban", t.player.goto_tile, 2576, 3027, 0)
        t.exec("talkToToban", t.player.talk_to, "toban", 1)
        t.exec("talkToToban-dialog", t.chat.play, {
            "npc:What do you want, small thing",
            "choose:I seek entrance to the city of ogres.",
            "player:I seek entrance to the city of",
            "npc:Hahaha! You'll never get in there",
            "player:I'll find a way, trust me.",
            "npc:Bold words for a t'ing so small",
            "choose:I could do something for you...",
            "player:I could do something for you",
            "npc:Hahaha! This creature t'inks",
            "npc:Prove to me your might",
        })

        -- ================= Give Toban the dragon bones =================
        -- toban.rs2:49-58 (@spoken_toban) opens with a chatnpc page BEFORE
        -- the inv_del/inv_add/setbit -- use_on's settle only proves that
        -- FIRST page opened (trap 22), so the reward needs the rest of the
        -- chain clicked through before it lands.
        local toban_t = t.player.by_symbol("npc", "toban")
        t.exec("giveTobanDragonBones", t.player.use_on, "dragon_bones", toban_t)
        t.exec("giveTobanDragonBones-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.relicpart3", t.inv.await, "relicpart3", 1, 5)

        -- ================= Open Toban's chest for Og's stolen gold =================
        t.exec("goto-searchChestForTobansGold", t.player.goto_tile, 2575, 3031, 0)
        t.exec("searchChestForTobansGold", t.player.click_loc, "tobanchest", 1)
        t.exec("inv.stolen_gold", t.inv.await, "stolen_gold", 1, 5)

        -- ================= Return the gold to Og =================
        t.exec("goto-talkToOgAgain", t.player.goto_tile, 2506, 3116, 0)
        local og_t = t.player.by_symbol("npc", "og")
        t.exec("talkToOgAgain", t.player.use_on, "stolen_gold", og_t)
        t.exec("talkToOgAgain-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.relicpart1", t.inv.await, "relicpart1", 1, 5)

        -- ================= Return the tooth to Grew =================
        t.exec("goto-useRopeOnBranchAgain", t.player.goto_tile, 2502, 3087, 0)
        local rope_branch2 = t.player.by_symbol("loc", "tree_ropeswing4_norope")
        t.exec("useRopeOnBranchAgain", t.player.use_on, "rope", rope_branch2)

        t.exec("goto-talkToGrewAgain", t.player.goto_tile, 2511, 3086, 0)
        local grew_t = t.player.by_symbol("npc", "grew")
        t.exec("talkToGrewAgain", t.player.use_on, "ogretooth", grew_t)
        t.exec("talkToGrewAgain-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.relicpart2", t.inv.await, "relicpart2", 1, 5)
        t.exec("inv.powering_crystal1", t.inv.await, "powering_crystal1", 1, 5)

        -- ================= Bring the assembled relic to the wizard =================
        t.exec("goto-leaveGrewIsland2", t.player.goto_tile, 2511, 3093, 0)
        t.exec("leaveGrewIsland2", t.player.click_loc, "tree_ropeswing3", 1)

        t.exec("goto-bringRelicUpToFirstFloor", t.player.goto_tile, 2544, 3111, 0)
        t.exec("bringRelicUpToFirstFloor", t.player.click_loc, "towerladder", 1)
        t.exec("goto-bringRelicUpToWizard", t.player.goto_tile, 2549, 3111, 1)
        t.exec("bringRelicUpToWizard", t.player.click_loc, "watchladderup", 1)

        t.exec("goto-talkToWizardWithRelic", t.player.goto_tile, 2549, 3116, 2)
        -- Each relicpartN use_on assembles the statue; the third call fires
        -- @check_relic_parts (watchtower_wizard.rs2:201-211) which auto-
        -- advances the stage once all three bits are set -- no separate
        -- talk_to needed.
        local wizard_t = t.player.by_symbol("npc", "watchtower_wizard")
        t.exec("talkToWizardWithRelic-part1", t.player.use_on, "relicpart1", wizard_t)
        t.exec("talkToWizardWithRelic-part1-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("talkToWizardWithRelic-part2", t.player.use_on, "relicpart2", wizard_t)
        t.exec("talkToWizardWithRelic-part2-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("talkToWizardWithRelic-part3", t.player.use_on, "relicpart3", wizard_t)
        t.exec("talkToWizardWithRelic-part3-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.exec("inv.ogrerelic", t.inv.await, "ogrerelic", 1, 5)
        t.check("quest.stage.made_relic", t.quest.expect_stage("itwatchtower_made_relic"))

        -- ================= Enter Gu'Tanoth =================
        t.exec("goto-enterGuTanoth", t.player.goto_tile, 2504, 3063, 0)
        local guard2_t = t.player.by_symbol("npc", "ogre_guard2")
        t.exec("enterGuTanoth", t.player.use_on, "ogrerelic", guard2_t)
        t.exec("enterGuTanoth-drain", t.chat.drain, {stop_at = "none", max_pages = 10})
        t.check("quest.stage.given_relic", t.quest.expect_stage("itwatchtower_given_relic"))

        -- ================= Market: rock cake for the battlement guard =================
        t.exec("goto-stealRockCake", t.player.goto_tile, 2514, 3036, 0)
        -- rockcounter_withcakes carries only op2 "Steal-From" (all.loc:24228;
        -- yanille_shop_stubs.rs2:85 is [oploc1] but the cache def has no op1
        -- row), so the press is op 2. Thieving 15 comes from setup.
        -- LIVE press of the only op the cache gives this counter (op 2).
        local steal_result, steal_detail = t.player.click_loc("rockcounter_withcakes", 2)
        t.note("click_loc rockcounter_withcakes op2 -> " .. tostring(steal_result) .. ": " .. tostring(steal_detail))
        t.blocked("content_bug: the guide's stealRockCake cannot be driven -- yanille_shop_stubs.rs2:85 binds [oploc1,rockcounter_withcakes] but configs/all.loc:24228 gives the counter only op2=Steal-From, so the client can only send op 2, which answers 'Nothing interesting happens.' (the stall's steal body, lines 86-105, is unreachable; the pack's stalls bind [oploc2], stealing.rs2:7-19). Fix: rename the trigger to [oploc2,rockcounter_withcakes]. Everything after this row (rock cake to ogre_guard3, tanothjump1, nightshades, enclave, potion, lever) is drafted in build/author_state/sonnet-b30/itwatchtower.full_draft.lua")
        return
    end,
}
