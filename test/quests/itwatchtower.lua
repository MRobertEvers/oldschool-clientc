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
        "::give shark 10",        -- Gorad + enclave shaman food (s25rc_wt2 died at the second shaman on 5)
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
        -- The first floor's Climb-down is qip_watchtower_ladder_top (17122,
        -- maps/m39_48.jl2 `1 48 39: 17122 10`; ladders.loc category
        -- climb_down_ladder, maplink.dbrow maplink_1_39_48_*_down), NOT
        -- towerladder -- that is the ground floor's Climb-up on the same
        -- square (`0 48 39: 2833 10`), under the floor and unpickable from
        -- here (seam26 towerladder_press, build/quest_gate/s26tl_before).
        t.exec("goDownFromFirstFloor", t.player.click_loc, "qip_watchtower_ladder_top", 1)
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
        t.exec("enterHoleSouthOfGuTanoth", t.player.click_loc, "tobancave", 1)

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
        -- ogre_trader2 wanders beside this counter (m39_47.spawn:18: 2513,3034)
        -- and the steal is refused -- "Grr! Get your hands off those cakes!",
        -- and he attacks -- while he is within 3 tiles of the thief by line of
        -- walk (yanille_shop_stubs.rs2 [oploc2,rockcounter_withcakes], LC
        -- ogre_trader.rs2:26-30). A player steals from the side of the counter
        -- the trader is not on, so the counter blocks his line: try the north
        -- side, then the south; after a refusal step away until he loses
        -- interest and wanders. seam25: from 2514,3036 with him at his spawn
        -- the press was caught 18/18 (s25rc_away1-3), from 2513,3037 it stole
        -- 3/3 (s25rc_north1-3); late in this run he had wandered north
        -- (s25rc_wt1 shot 180). The counter's only op is op2 Steal-From
        -- (all.loc:24228). Thieving 15 comes from setup.
        local steal_sides = {{2513, 3037}, {2514, 3035}}
        local stolen = false
        for attempt = 1, 8 do
            if not stolen then
                local side = steal_sides[(attempt - 1) % 2 + 1]
                t.exec("goto-stealRockCake-" .. attempt, t.player.goto_tile, side[1], side[2], 0)
                local r, d = t.player.click_loc("rockcounter_withcakes", 2)
                local _, page = t.chat.text()
                t.step("stealRockCake-" .. attempt, r == "ok" and "PASS" or "FAIL",
                    tostring(d) .. " / page: " .. tostring(page))
                t.chat.drain({stop_at = "none", max_pages = 3})
                t.ticks(2)
                local _, cakes = t.inv.count("rockcake")
                if cakes and cakes > 0 then
                    stolen = true
                else
                    t.exec("goto-stealRockCake-away-" .. attempt, t.player.goto_tile, 2514, 3050, 0)
                    t.ticks(15)
                end
            end
        end
        t.exec("inv.rockcake", t.inv.await, "rockcake", 1, 5)

        t.exec("goto-talkToGuardBattlement", t.player.goto_tile, 2503, 3012, 0)
        t.exec("talkToGuardBattlement", t.player.talk_to, "ogre_guard3", 1)
        t.exec("talkToGuardBattlement-dialog", t.chat.play, {
            "npc:Oi! Where do you think you are",
            "choose:But I am a friend to ogres...",
            "player:But I am a friend to ogres",
            "npc:Prove it to us with a gift",
            "player:Like what?",
            "npc:Surprise us",
        })

        local guard3_t = t.player.by_symbol("npc", "ogre_guard3")
        t.exec("talkToGuardWithRockCake", t.player.use_on, "rockcake", guard3_t)
        -- ogre_guard.rs2:114-118 (battlements_rockcake) deletes rockcake and
        -- sets the market bits BEFORE its chat lines, so use_on's own
        -- settle already proves the state changed.
        t.exec("inv.rockcake_spent", t.inv.expect_absent, "rockcake")

        -- ================= Jump the broken bridge =================
        -- all.loc.compack 2830=tanothjump1; maps/m39_47.jl2:1372 "1 34 18:
        -- 2830 10" -> world 2530,3010,LEVEL 1, not the ground floor QH's
        -- WorldPoint implied (run 2's goto at 2530,3026,0 never even framed
        -- it -- "pose 2/1 would not re-frame" is the giveaway of a wrong
        -- plane, not a wrong pixel).
        -- QH jumpGap: WorldPoint(2530, 3026, 0). The jl2 row is level 1 on a
        -- bridge-flagged square, which the game plays at level 0 (seam24
        -- projects bridge-deck locs at World_TerrainWalkLevel). s25rc_wt1:
        -- standing at 2530,3024,1 no pixel held the gap.
        t.exec("goto-jumpGap", t.player.goto_tile, 2530, 3024, 0)
        t.exec("jumpGap", t.player.click_loc, "tanothjump1", 1)
        -- quest_itwatchtower.rs2 [oploc1,tanothjump1]: ogre_guard4's toll pages
        -- come before the choice, then the jump and "Phew! I just made it."
        t.exec("jumpGap-dialog", t.chat.play, {
            "npc:Oi! Little thing",
            "player:20 gold pieces to jump",
            "npc:That's what I said",
            "choose:Okay, I'll pay it.",
            "player:Okay, I'll pay it.",
            "npc:A wise choice",
            "player:Phew! I just made it.",
        })
        local _, coins_after_jump = t.inv.count("coins")
        t.check("jumpGap.toll_paid", coins_after_jump == 30, "coins " .. tostring(coins_after_jump) .. " (50 - 20 toll)")

        -- ================= City guard's riddle =================
        t.exec("goto-talkToCityGuard", t.player.goto_tile, 2543, 3032, 0)
        t.exec("talkToCityGuard", t.player.talk_to, "city_guard", 1)
        t.exec("talkToCityGuard-dialog", t.chat.play, {
            "npc:Grrrr, what business you got",
            "player:I am on an errand.",
            "npc:So what you want with me?",
            "choose:I seek passage into the skavid caves.",
            "player:I seek passage into the skavid",
            "npc:Is that so",
            "npc:I want you to bring me an item",
            "npc:My first is in days",
            "npc:My fifth is in heaven",
            "npc:My eighth is in nine",
            "npc:My whole is an object",
        })
        t.check("quest.stage.given_riddle", t.quest.expect_stage("itwatchtower_given_riddle"))

        local guard_t = t.player.by_symbol("npc", "city_guard")
        t.exec("talkToCityGuardAgain", t.player.use_on, "deathrune", guard_t)
        t.exec("inv.skavidmap", t.inv.await, "skavidmap", 1, 5)
        t.check("quest.stage.solved_riddle", t.quest.expect_stage("itwatchtower_solved_riddle"))

        -- ================= Skavid caves: the scared skavid =================
        t.exec("goto-enterScaredSkavidCave", t.player.goto_tile, 2554, 3035, 0)
        t.exec("enterScaredSkavidCave", t.player.click_loc, "skavid_cave5", 1)
        t.ticks(3) -- settle the scene after the underground teleport (trap 21)

        t.exec("talkToScaredSkavid", t.player.talk_to, "scared_skavid", 1)
        t.exec("talkToScaredSkavid-dialog", t.chat.play, {
            "npc:Tanath cur, tanath cur",
            "player:???",
            "npc:Don't hurt me, don't hurt me",
            "player:Stop moaning, creature",
            "npc:Please don't touch me",
            "player:You have something that belong",
            "npc:I don't have anything",
            "player:Somehow, I find your words",
            "npc:I'm begging your kindness",
            "choose:Okay, okay, I'm not going to hurt you.",
            "player:Okay, okay, I'm not going to h",
            "npc:Thank you, kind one",
            "npc:I'll tells you where",
            "npc:You will have to learn skavid",
            "npc:Let me tells you the most comm",
            "npc:Ar, nod, gor, ig, cur",
            "npc:Those will gets you started",
        })

        t.exec("leaveScaredSkavidRoom", t.player.click_loc, "cave5exit", 1)

        -- ================= Room 1 (skavid_cave4): skavidtalker3, "Cur." =================
        t.exec("goto-enterSkavid1Cave", t.player.goto_tile, 2554, 3053, 0)
        t.exec("enterSkavid1Cave", t.player.click_loc, "skavid_cave4", 1)
        t.ticks(3) -- settle the scene after the underground teleport (trap 21)
        t.exec("talkToSkavid1", t.player.talk_to, "skavidtalker3", 1)
        t.exec("talkToSkavid1-dialog", t.chat.play, {
            "npc:Bidith tanath",
            "choose:Cur.",
            "player:Cur.",
            "npc:Cur",
        })
        t.exec("leaveSkavid1", t.player.click_loc, "cave4exit", 1)

        -- ================= Room 2 (skavid_cave3): skavidtalker2, "Ar." =================
        t.exec("goto-enterSkavid2Cave", t.player.goto_tile, 2541, 3053, 0)
        t.exec("enterSkavid2Cave", t.player.click_loc, "skavid_cave3", 1)
        t.ticks(3) -- settle the scene after the underground teleport (trap 21)
        t.exec("talkToSkavid2", t.player.talk_to, "skavidtalker2", 1)
        t.exec("talkToSkavid2-dialog", t.chat.play, {
            "npc:Gor cur",
            "choose:Ar.",
            "player:Ar.",
            "npc:Ar",
        })
        t.exec("leaveSkavid2", t.player.click_loc, "cave3exit", 1)

        -- ================= Room 3 (skavid_cave2): skavidtalker1, "Ig." + nightshade #1 =================
        t.exec("goto-enterSkavid3Cave", t.player.goto_tile, 2524, 3069, 0)
        t.exec("enterSkavid3Cave", t.player.click_loc, "skavid_cave2", 1)
        t.ticks(3) -- settle the scene after the underground teleport (trap 21)
        t.exec("talkToSkavid3", t.player.talk_to, "skavidtalker1", 1)
        t.exec("talkToSkavid3-dialog", t.chat.play, {
            "npc:Cur bidith",
            "choose:Ig.",
            "player:Ig.",
            "npc:Ig",
        })
        -- m39_147.spawn:48 -- a nightshade ground spawn sits in this room.
        -- Take is op 3 on a ground object (click_obj's default); op 1/2 have
        -- no row on the menu. Stand one square off the stack, not on it.
        t.exec("goto-nightshade1", t.player.goto_tile, 2530, 9461, 0)
        -- click_obj answers a bare ok (t.exec calls that hollow); the
        -- inv.await below is the proof the pick landed.
        local ns1_r, ns1_d = t.player.click_obj("nightshade", 3)
        t.check("pickUp2Nightshade-1", ns1_r == "ok", "click_obj nightshade op3 -> " .. tostring(ns1_r) .. " " .. tostring(ns1_d))
        t.exec("inv.nightshade1", t.inv.await, "nightshade", 1, 5)
        t.exec("leaveSkavid3", t.player.click_loc, "cave2exit", 1)

        -- ================= Room 4 (skavid_cave1): skavidtalker4, "Nod." =================
        t.exec("goto-enterSkavid4Cave", t.player.goto_tile, 2561, 3024, 0)
        t.exec("enterSkavid4Cave", t.player.click_loc, "skavid_cave1", 1)
        t.ticks(3) -- settle the scene after the underground teleport (trap 21)
        t.exec("talkToSkavid4", t.player.talk_to, "skavidtalker4", 1)
        t.exec("talkToSkavid4-dialog", t.chat.play, {
            "npc:Tanath gor",
            "choose:Nod.",
            "player:Nod.",
            "npc:Nod",
        })
        t.exec("leaveSkavid4", t.player.click_loc, "cave1exit", 1)

        -- ================= The SE gate: ogre_guard1, gold bar toll =================
        -- open_gutanoth_gate (ogre_guard.rs2:187-202) re-derives ogre_guard1_
        -- dialogue on EVERY click while %gutanoth_gold < found_gold(2): the
        -- first click (state 0) only sets looking_gold(1) and kicks the
        -- player out to ^gutanoth_hill; the SECOND click (state 1, gold_bar
        -- already held) consumes it and teleports past. Two real clicks.
        -- Run 2: both clicks settled on a bare map_flag (no dialogue line
        -- at all) -- open_gutanoth_gate's oploc1 body does NOTHING when
        -- ogre_guard1 is not within npc_find's 6 tiles of `coord` at that
        -- instant (ogre_guard.rs2:187-197), so a wandering guard can make a
        -- click land as a plain walk. Loop the click+drain until the gold
        -- bar is actually spent, not a fixed two presses.
        t.exec("goto-tryToGoThroughToInsaneSkavid", t.player.goto_tile, 2550, 3028, 0)
        local gate_open = false
        for attempt = 1, 6 do
            if not gate_open then
                local r, d = t.player.click_loc("ogreguardgate1", 1)
                t.step("tryToGoThroughToInsaneSkavid-" .. attempt, r == "ok" and "PASS" or "FAIL", d)
                t.chat.drain({stop_at = "none", max_pages = 5})
                local _, bar_count = t.inv.count("gold_bar")
                if bar_count == 0 then
                    gate_open = true
                else
                    t.ticks(3)
                end
            end
        end
        t.exec("inv.gold_bar_spent", t.inv.expect_absent, "gold_bar")

        -- ================= The mad skavid's riddle-guess =================
        t.exec("goto-enterInsaneSkavidCave", t.player.goto_tile, 2528, 3013, 0)
        t.exec("enterInsaneSkavidCave", t.player.click_loc, "skavid_cave6", 1)
        t.ticks(3) -- settle the scene after the underground teleport (trap 21)

        -- skavid.rs2:196-237 -- the mad skavid picks $mes_type = random(3)
        -- each visit and the correct word depends on it (0->"Gor.",
        -- 1->"Cur.", 2->"Bidith."); read the spoken line back and answer it,
        -- retrying (a wrong guess just reopens the same choice) until the
        -- crystal lands.
        local mad_answered = false
        for attempt = 1, 9 do
            if not mad_answered then
                t.exec("talkToInsaneSkavid-" .. attempt, t.player.talk_to, "mad_skavid", 1)
                local text_result, npc_line = t.chat.text()
                local answer = nil
                if text_result == "ok" and npc_line then
                    if string.find(npc_line, "Ar cur", 1, true) then
                        answer = "Gor."
                    elseif string.find(npc_line, "Bidith ig", 1, true) then
                        answer = "Cur."
                    elseif string.find(npc_line, "Cur tanath", 1, true) then
                        answer = "Bidith."
                    end
                end
                if answer then
                    -- talk_to's own settle only proves the "Ar cur..." npc
                    -- page opened, not that it's been continued to the
                    -- options page yet (run 4: "expected options, got npc
                    -- text='Ar cur...'") -- play the npc page first.
                    t.exec("talkToInsaneSkavid-answer-" .. attempt, t.chat.play, {"npc:*", "choose:" .. answer})
                    -- Run 5: every one of 9 correct-per-the-branch-logic
                    -- answers still failed to grant a crystal. Root cause:
                    -- mad_skavid_correct (skavid.rs2) opens a chatnpc page
                    -- THEN a ~mesbox BEFORE inv_add -- trap 22 again, this
                    -- time missed because the choose: entry was the list's
                    -- last one. Drain whatever follows (the correct chain
                    -- or the "response was wrong" one) before re-reading.
                    t.exec("talkToInsaneSkavid-drain-" .. attempt, t.chat.drain, {stop_at = "none", max_pages = 5})
                else
                    t.check("talkToInsaneSkavid-close-" .. attempt, t.chat.close(), npc_line or "no page text read")
                end
                local _, crystal2_count = t.inv.count("powering_crystal2")
                if crystal2_count and crystal2_count > 0 then
                    mad_answered = true
                end
            end
        end
        t.exec("inv.powering_crystal2", t.inv.await, "powering_crystal2", 1, 10)
        t.check("quest.stage.skavid_crystal", t.quest.expect_stage("itwatchtower_skavid_crystal"))

        -- m39_147.spawn:49 -- the second nightshade ground spawn is in this room.
        t.exec("goto-nightshade2", t.player.goto_tile, 2528, 9414, 0)
        -- click_obj answers a bare ok (t.exec calls that hollow); the
        -- inv.await below is the proof the pick landed.
        local ns2_r, ns2_d = t.player.click_obj("nightshade", 3)
        t.check("pickUp2Nightshade-2", ns2_r == "ok", "click_obj nightshade op3 -> " .. tostring(ns2_r) .. " " .. tostring(ns2_d))
        t.exec("inv.nightshade2", t.inv.await, "nightshade", 2, 5)

        t.exec("leaveMadSkavid", t.player.click_loc, "cave6exit", 1)

        -- ================= Infiltrate the enclave (nightshade #1) =================
        t.exec("goto-useNightshadeOnGuard", t.player.goto_tile, 2507, 3036, 0)
        local guard_encl_t = t.player.by_symbol("npc", "enclave_guard")
        t.exec("useNightshadeOnGuard", t.player.use_on, "nightshade", guard_encl_t)
        -- enclave_guard.rs2 [opnpcu]: the chatnpc page comes first, then
        -- enter_skavid_cave waits p_delay(2) and p_teleport's into the enclave.
        t.exec("useNightshadeOnGuard-drain", t.chat.drain, {stop_at = "none", max_pages = 5})
        t.ticks(4)
        local _, enclave_tile = t.world.tile()
        t.check("enclave.entered", enclave_tile.z > 9000, "player at " .. enclave_tile.x .. "," .. enclave_tile.z .. "," .. enclave_tile.level)
        t.check("quest.stage.fed_nightshade", t.quest.expect_stage("itwatchtower_fed_nightshade"))

        -- ================= Back to the wizard: learn the potion recipe =================
        -- enter_skavid_cave lands at 2588,9410 (enclave_guard.rs2:47); the
        -- exit cave is m40_147.jl2:4389 (2598,9468), out of view from there
        -- (s25rc_wt1: "no loc 2813 in the client's entity pool"). Walk to it.
        t.exec("goto-leaveEnclave", t.player.goto_tile, 2598, 9466, 0)
        t.exec("leaveEnclave", t.player.click_loc, "enclavecave", 1)
        t.exec("goto-goBackUpToFirstFloorAfterEnclave", t.player.goto_tile, 2544, 3111, 0)
        t.exec("goBackUpToFirstFloorAfterEnclave", t.player.click_loc, "towerladder", 1)
        t.exec("goto-goBackUpToWizardAfterEnclave", t.player.goto_tile, 2549, 3111, 1)
        t.exec("goBackUpToWizardAfterEnclave", t.player.click_loc, "watchladderup", 1)

        t.exec("goto-talkToWizardAgainEnclave", t.player.goto_tile, 2549, 3116, 2)
        t.exec("talkToWizardAgainEnclave", t.player.talk_to, "watchtower_wizard", 1)
        -- Run 7: "expected kind=npc, got player text='I have found the
        -- cave...'" -- trap 18 again, on a row I'd already misjudged once.
        -- watchtower_wizard.rs2's itwatchtower_fed_nightshade branch opens
        -- with ~chatplayer_anim, not ~chatnpc_anim.
        t.exec("talkToWizardAgainEnclave-dialog", t.chat.play, {
            "player:I have found the cave of ogre",
            "npc:That is because of their magic",
            "npc:Collect a guam leaf, add janger",
            "npc:Be very careful how you mix",
            "npc:I hope you've been brushing up",
        })
        t.check("quest.stage.learned_potion", t.quest.expect_stage("itwatchtower_learned_potion"))

        -- ================= Make the ogre potion =================
        -- Runs 2/4/6/7 all read "Nothing interesting happens." from this
        -- exact call (arm jangerberries, click guamvial) despite matching
        -- brew_potion.rs2:207-214's own [opheldu,jangerberries] guard by
        -- every reading of the source -- never chased to ground truth in
        -- the budget available. Try the reverse arm/target order as a
        -- fallback before giving up: if the pair really does only work one
        -- way (trap 271's own precedent, Current Affairs' charcoal/form),
        -- this is the other candidate.
        local jj_r, jj_d = t.player.use_item_on_item("jangerberries", "guamvial")
        local _, jj_count = t.inv.count("guamjangervial")
        if not (jj_count and jj_count > 0) then
            t.note("useJangerberriesOnGuam (jangerberries on guamvial): " .. tostring(jj_d))
            jj_r, jj_d = t.player.use_item_on_item("guamvial", "jangerberries")
        end
        t.step("useJangerberriesOnGuam", jj_r == "ok" and "PASS" or "FAIL", jj_d)
        t.exec("inv.guamjangervial", t.inv.await, "guamjangervial", 1, 5)
        t.exec("grindBatBones", t.player.use_item_on_item, "pestle_and_mortar", "bat_bones")
        t.exec("inv.ground_bat_bones", t.inv.await, "ground_bat_bones", 1, 5)
        -- Same dual-order safety net -- ogre_potion.rs2:35 declares
        -- [opheldu,guamjangervial] (arm the vial, click the bones), but
        -- give the reverse a chance too rather than lose the last run to a
        -- second copy of jangerberries' mystery.
        local bp_r, bp_d = t.player.use_item_on_item("guamjangervial", "ground_bat_bones")
        local _, bp_count = t.inv.count("ogre_potion")
        if not (bp_count and bp_count > 0) then
            t.note("useBonesOnPotion (guamjangervial on ground_bat_bones): " .. tostring(bp_d))
            bp_r, bp_d = t.player.use_item_on_item("ground_bat_bones", "guamjangervial")
        end
        t.step("useBonesOnPotion", bp_r == "ok" and "PASS" or "FAIL", bp_d)
        t.exec("inv.ogre_potion", t.inv.await, "ogre_potion", 1, 5)

        -- ================= Return with the potion; the wizard enchants it =================
        t.exec("talkToWizardWithPotion", t.player.talk_to, "watchtower_wizard", 1)
        -- make_magic_ogre_potion (watchtower_wizard.rs2:285-294): inv_del/
        -- inv_add/stage all happen BEFORE any of these pages, so the
        -- reward lands regardless -- but the page chain itself is broken
        -- by an if_close then a bare mes() (not a page, trap: "a bare
        -- mes() is a chat-LOG line") then a p_delay(3) before a REOPENED
        -- page, so it can't be one continuous chat.play list.
        t.exec("talkToWizardWithPotion-dialog", t.chat.play, {
            "npc:Any more news",
            "player:I have made the potion",
            "npc:That's great news",
            "end",
        })
        t.exec("msg.wizard_mutters", t.msg.expect, "wizard mutters strange words")
        t.ticks(3)
        t.exec("talkToWizardWithPotion-enchant", t.chat.play, {"npc:Here it is - a dangerous"})
        t.exec("inv.magic_ogre_potion", t.inv.await, "magic_ogre_potion", 1, 5)
        t.check("quest.stage.made_potion", t.quest.expect_stage("itwatchtower_made_potion"))

        -- ================= Re-infiltrate the enclave (nightshade #2) =================
        t.exec("goto-useNightshadeOnGuardAgain-down1", t.player.goto_tile, 2549, 3111, 2)
        t.exec("useNightshadeOnGuardAgain-down1", t.player.click_loc, "watchladderdown", 1)
        t.exec("goto-useNightshadeOnGuardAgain-down2", t.player.goto_tile, 2544, 3111, 1)
        -- The first floor's Climb-down, as goDownFromFirstFloor above.
        t.exec("useNightshadeOnGuardAgain-down2", t.player.click_loc, "qip_watchtower_ladder_top", 1)
        t.exec("goto-useNightshadeOnGuardAgain", t.player.goto_tile, 2507, 3036, 0)
        local guard_encl_t2 = t.player.by_symbol("npc", "enclave_guard")
        t.exec("useNightshadeOnGuardAgain", t.player.use_on, "nightshade", guard_encl_t2)
        t.exec("useNightshadeOnGuardAgain-drain", t.chat.drain, {stop_at = "none", max_pages = 5})
        t.ticks(4)
        local _, enclave_tile2 = t.world.tile()
        t.check("enclave.entered_again", enclave_tile2.z > 9000, "player at " .. enclave_tile2.x .. "," .. enclave_tile2.z .. "," .. enclave_tile2.level)

        -- ================= Kill the six ogre shamans with the potion =================
        -- Base spawn symbols (areas/world/configs/m40_147.spawn); the
        -- "_normal" child QH names has no spawn row in this pack.
        -- The six spawned shamans answer the potion since seam23 (Watchtower
        -- shamans, content 33c8b2ac6a); the sixth grants powering_crystal3.
        local shamans = {
            {sym = "qip_watchtower_ogre_shaman_01", x = 2592, z = 9436},
            {sym = "qip_watchtower_ogre_shaman_02", x = 2582, z = 9437},
            {sym = "qip_watchtower_ogre_shaman_03", x = 2577, z = 9451},
            {sym = "qip_watchtower_ogre_shaman_04", x = 2599, z = 9461},
            {sym = "qip_watchtower_ogre_shaman_05", x = 2607, z = 9451},
            {sym = "qip_watchtower_ogre_shaman_06", x = 2606, z = 9438},
        }
        for i, shaman in ipairs(shamans) do
            -- The shamans hit back; eat before each (s25rc_wt2 died at the second).
            for _ = 1, 3 do
                local hp_r, hp = t.skill.read("hitpoints")
                local _, sharks = t.inv.count("shark")
                if hp_r == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < 45 and sharks and sharks > 0 then
                    t.player.inv_op("shark", 1) -- shark's own ifop1=Eat
                    t.ticks(3)
                    local _, hp_after = t.skill.read("hitpoints")
                    t.note("ate a shark before shaman " .. i .. ": hitpoints " .. tostring(hp.level) .. " -> " .. tostring(hp_after and hp_after.level))
                end
            end
            t.exec("goto-usePotionOnOgre" .. i, t.player.goto_tile, shaman.x, shaman.z, 0)
            local shaman_t = t.player.by_symbol("npc", shaman.sym)
            t.exec("usePotionOnOgre" .. i, t.player.use_on, "magic_ogre_potion", shaman_t)
            -- seam23 copy: the handler pages after its own p_delay(1); a
            -- page left up here holds the script and drops the next click.
            t.ticks(2)
            t.exec("usePotionOnOgre" .. i .. "-drain", t.chat.drain, {})
        end
        t.exec("inv.powering_crystal3", t.inv.await, "powering_crystal3", 1, 10)

        -- ================= Mine the Rock of Dalgroth for the fourth crystal =================
        t.exec("goto-mineRock", t.player.goto_tile, 2591, 9450, 0)
        -- [oploc2,rock_of_dalgroth] is the mining trigger; op1 is only the
        -- flavour "examine the rock" text (quest_itwatchtower.rs2).
        t.exec("mineRock", t.player.click_loc, "rock_of_dalgroth", 2)
        t.exec("inv.powering_crystal4", t.inv.await, "powering_crystal4", 1, 10)

        -- ================= Leave the enclave with all four crystals =================
        t.exec("leaveEnclaveWithCrystals", t.player.click_loc, "enclavecave", 1)
        t.exec("goto-goUpToFirstFloorWithCrystals", t.player.goto_tile, 2544, 3111, 0)
        t.exec("goUpToFirstFloorWithCrystals", t.player.click_loc, "towerladder", 1)
        t.exec("goto-goUpToWizardWithCrystals", t.player.goto_tile, 2549, 3111, 1)
        t.exec("goUpToWizardWithCrystals", t.player.click_loc, "watchladderup", 1)

        t.exec("goto-talkToWizardWithCrystals", t.player.goto_tile, 2549, 3116, 2)
        t.exec("talkToWizardWithCrystals", t.player.talk_to, "watchtower_wizard", 1)
        -- watchtower_wizard.rs2:29-37 (itwatchtower_made_potion branch,
        -- which is what we should be in by now with all 6 shamans down):
        -- "Hello again.|Did the potion work?" -> since shaman_kills=6,
        -- "Indeed it did!..." -> "Wonderful! Bring me the crystals..." and
        -- returns. (Earlier runs never reached this branch for real -- the
        -- text this list originally had was copy-pasted from the WRONG,
        -- earlier skavid_crystal branch by mistake.)
        -- seam24: the made_potion branch now hands over (watchtower_wizard.rs2;
        -- wiki Transcript:Watchtower oldid 15263268).
        t.exec("talkToWizardWithCrystals-dialog", t.chat.play, {
            "npc:Hello again",
            "player:Indeed it did",
            "npc:Magnificent! At last you've brought all the crystals.",
            "npc:Now the shield generator can be activated",
            "npc:Put the crystals on the pillars there and throw the lever",
        })
        t.exec("inv.all_four_crystals", t.inv.await_all, {
            powering_crystal1 = 1,
            powering_crystal2 = 1,
            powering_crystal3 = 1,
            powering_crystal4 = 1,
        })
        t.check("quest.stage.found_all_crystals", t.quest.expect_stage("itwatchtower_found_all_crystals"))
        local pillars = {
            { n = 1, loc = "qip_watchtower_pillar_nocrystal_multi_yellow", item = "powering_crystal1", varbit = "watchtower_pillar_2" },
            { n = 2, loc = "qip_watchtower_pillar_nocrystal_multi_magenta", item = "powering_crystal2", varbit = "watchtower_pillar_3" },
            { n = 3, loc = "qip_watchtower_pillar_nocrystal_multi_cyan", item = "powering_crystal3", varbit = "watchtower_pillar_1" },
            { n = 4, loc = "qip_watchtower_pillar_nocrystal_multi_white", item = "powering_crystal4", varbit = "watchtower_pillar_4" },
        }
        for _, p in ipairs(pillars) do
            local target = t.player.by_symbol("loc", p.loc)
            t.exec("useCrystal" .. p.n, t.player.use_on, p.item, target)
            t.exec("var." .. p.varbit, t.var.await_server, p.varbit, 1, 5)
        end
        t.exec("pullLever", t.player.click_loc, "watchleverup", 1)
        t.exec("msg.force_field", t.msg.await, "The magic force field activates.", 10)
        t.ticks(10)
        t.exec("pullLever-dialog", t.chat.play, {
            "npc:Marvellous! It works!",
            "npc:Take this payment",
            "npc:improve your Magic level",
            "npc:Here is a special item",
        })
        t.quest.expect_complete()
        t.finish(0)
        return
    end,
}
