-- Heroes' Quest -- driven for real through the Phoenix Gang route
-- (Achietties -> Straven -> Alfonse -> Charlie the cook -> kill Grip ->
-- loot the treasure chest -> Straven's candlestick hand-in for the
-- armband), then the two solo-collectible arms (Ice Queen -> ice gloves ->
-- Entrana firebird -> feather; Gerrant -> Blamish oil -> oily rod -> lava
-- eel -> cook it), then the real hand-in to Achietties. Never ::setvar on
-- %heroquest itself past `hero_started` -- every later value is written by
-- the content this file actually clicks/fights through.
--
-- Prerequisites cheated in setup, never the quest's own deliverable (trap
-- 16): 55 QP, Lost City (%zanaris), Dragon Slayer I (%dragonquest),
-- Merlin's Crystal (%arthur) and the Phoenix side of Shield of Arrav
-- (%phoenixgang) are FOUR OTHER quests achietties.rs2's own
-- ~has_hero_quest_requirements gate demands before Heroes' Quest can even
-- be accepted (docs/quests/heroes_quest.md section 2). ::complete has no
-- arm for quest_lostcity/quest_merlinscrystal/quest_dragonslayer1 in this
-- checkout's quest_cheat.rs2 (grepped -- Shield of Arrav's own arm
-- (quest_shieldofarrav) only ever writes %blackarmgang, never %phoenixgang
-- either), so the varps are set directly with ::setvar, the same documented
-- cheat ladder ::give/::setlevel/::setvar/::kill/::spawn/::tele/::goto
-- (QUEST_AUTHORING.md section 3) blackknight.lua already uses for its own
-- %qp prerequisite. phoenixkey2 is the tradeable key Straven's own
-- straven_gangmember label (areas/varrock/scripts/straven.rs2:97-102)
-- hands out on joining the Phoenix Gang -- a Shield-of-Arrav bring-along,
-- not this quest's own deliverable, so it is given rather than earned by
-- replaying that quest.
--
-- Combat/skill LEVELS and plain bring-along tools/food/ingredients are all
-- prerequisites, the same "gear is a prerequisite" idiom hunt.lua/
-- mortton.lua/rovingelves.lua already use for a quest fight -- never the
-- quest's own deliverable, which HeroesQuest.java's own ItemRequirement set
-- confirms: only `iceGloves` carries `.canBeObtainedDuringQuest()` (grepped
-- /Users/matthewevers/Documents/git_repos/quest-helper/.../HeroesQuest.java)
-- -- fishingRod, fishingBait, harralanderUnf (harralandervial, the
-- unfinished potion Blamish Slime mixes into) and pickaxe are NOT marked
-- that way, so they are given directly. blamish_snail_slime, blamish_oil,
-- oily_fishing_rod, raw_lava_eel, lava_eel, hot_feather, master_thief_armband,
-- petecandlestick and grip_keys ARE the quest's own deliverables and are
-- driven for real below.
--
-- The candlestick-chest content bug this file used to stop at
-- (brimhaven_scarface_mansion.rs2's opencandlechest write clobbering a
-- Phoenix player's %heroquest with the Black Arm route's own checkpoint,
-- past hero_phoenix_obtained_armband) is FIXED as of the committed source
-- (brimhaven_scarface_mansion.rs2:134, gated on
-- `%heroquest >= ^hero_blackarm_gangmember_spoken`, which a Phoenix player
-- sitting at hero_phoenix_killed_grip(5) never satisfies) -- confirmed
-- below by reading %heroquest right after the loot and asserting it is
-- STILL hero_phoenix_killed_grip, then driving on to Straven for the
-- armband for real.

return {
    id = "hero",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::setvar qp 55", -- prerequisite quest points (hero_required_questpoints), not this quest's own reward
        "::setvar zanaris 6", -- Lost City complete (zanaris_complete) -- no ::complete arm for quest_lostcity exists
        "::setvar dragonquest 10", -- Dragon Slayer I complete (dragon_complete) -- no ::complete arm for quest_dragonslayer1 exists
        "::setvar arthur 7", -- Merlin's Crystal complete (arthur_complete) -- no ::complete arm for quest_merlinscrystal exists
        "::setvar phoenixgang 10", -- Shield of Arrav, Phoenix side, complete (phoenixgang_complete) -- ::complete quest_shieldofarrav only ever writes %blackarmgang
        "::complete quest_druidicritual", -- Druidic Ritual, the DBROW name (all.dbrow.compack:35), not the quest_druid folder name (QUEST_AUTHORING.md docs/quests notes) -- ~herblore_unlocked (quest_druid.rs2:35-39) gates ~attempt_brew_potion on %druidquest >= ^druid_complete, and Heroes' Quest's own Blamish-oil mix (brew_potion.rs2:513-516) needs Herblore unlocked; this is a bring-along prerequisite (trap 16), not the quest's own deliverable
        "::give phoenixkey2 1", -- Shield-of-Arrav bring-along Straven's own script hands a joined Phoenix member, not Heroes' Quest's own deliverable
        -- Combat levels: prerequisite for BOTH real fights below (Grip,
        -- level 22; Ice Queen, level 111, hp104/atk95/str94/def95,
        -- combat_stats.generated.npc:14496-14516) -- same idiom
        -- rovingelves.lua uses for its own level-111-class moss guardian.
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_mace 1", -- crush weapon: Ice Queen's own lowest defence stat is crushdefence=20 (combat_stats.generated.npc:14515), vs slashdefence=40/stabdefence=30 -- dragon_mace is unusable here, levelrequire.rs2:171-176 refuses to Wear it until %heroquest >= ^hero_complete, which is this quest's OWN completion; rune_mace carries no such gate
        "::give rune_platebody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 8", -- food for the Ice Queen fight, same idiom as mortton.lua/rovingelves.lua's own "::give shark" food
        -- Fire-arm + lava-eel-arm bring-alongs -- none of these carry
        -- .canBeObtainedDuringQuest() in HeroesQuest.java, unlike iceGloves.
        "::give fishing_rod 1",
        "::give fishing_bait 20",
        "::give harralandervial 1", -- "Harralander potion (unf)" -- the solvent brew.dbrow's herblore_blamish_oil row names
        "::give logs 3",
        "::give tinderbox 1",
        "::give rune_pickaxe 1", -- White Wolf Mountain rockslide fallback (mine_ice_queen_lair_rockslide, white_wolf_mountain.rs2:23-32) if the ice queen's own tile answers screen_position through goto_tile
        "::setlevel mining 50", -- rockslide fallback's own gate (white_wolf_mountain.rs2:29)
        "::setlevel fishing 99", -- lava eel fishing gate is level 53 (lavafish.rs2:90)
        "::setlevel cooking 99", -- lava eel cooking gate is level 53 (cooking_generic.dbrow's cooking_generic_lava_eel row, never burns)
        "::setlevel herblore 99", -- Blamish oil mixing gate is level 25 (brew.dbrow's herblore_blamish_oil row)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "heroquest",
            constants = {
                not_started = 0,
                started = 1,
                phoenix_gangmember_spoken = 2,
                phoenix_talked_alfonse = 3,
                phoenix_talked_charlie = 4,
                phoenix_killed_grip = 5,
                phoenix_obtained_armband = 6,
                blackarm_gangmember_spoken = 7,
                blackarm_hq_door_unlocked = 8,
                blackarm_id_papers_obtained = 9,
                blackarm_mansion_unlocked = 10,
                blackarm_id_papers_given = 11,
                blackarm_looted_chest = 12,
                blackarm_obtained_armband = 13,
                complete = 15,
            },
            row = "quest_heroes", -- docs/quests/heroes_quest.md section 2: "Cache row | quest_heroes"
            display = "Heroes' Quest",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the ::setvar/::give cheats above are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Equip the fight gear now, before anything else -- frees five
        -- backpack slots the fishing-bait/harralander/logs stack still needs,
        -- and both real fights below want it worn from the first swing.
        t.exec("equipMace", t.player.equip, "rune_mace")
        t.exec("equipPlatebody", t.player.equip, "rune_platebody")
        t.exec("equipPlatelegs", t.player.equip, "rune_platelegs")
        t.exec("equipFullHelm", t.player.equip, "rune_full_helm")
        t.exec("equipKiteshield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Achietties: accept the quest for real.
        -- ---------------------------------------------------------------
        t.exec("goto-achietties", t.player.goto_tile, 2903, 3510, 0)
        t.exec("talkToAchietties", t.player.talk_to, "achietties")
        t.exec("talkToAchietties-dialog", t.chat.play, {
            "npc:Greetings. Welcome to the Heroes",
            "npc:Only the greatest heroes of this land",
            "choose:I'm a hero, may I apply to join?",
            "player:I'm a hero. May I apply to join?",
            "npc:Well, you have a lot of quest points",
            "npc:for entrance are:",
            "choose:I'll start looking for all those things then.",
            "player:I'll start looking for all those things then.",
            "npc:Good luck with that.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- Straven: join the Phoenix side of the armband chain.
        -- ---------------------------------------------------------------
        t.exec("goto-straven", t.player.goto_tile, 3246, 9780, 0)
        t.exec("talkToStraven-1", t.player.talk_to, "straven")
        t.exec("talkToStraven-1-dialog", t.chat.play, {
            "npc:Greetings fellow gang member.",
            "choose:Is there any way I can get the rank of master thief?",
            "player:Is there any way I can get the rank of master thief?",
            "npc:As it happens, yes. Head to our restaurant front in Brimhaven",
        })
        t.expect("quest.stage.phoenix_gangmember_spoken", t.quest.expect_stage("phoenix_gangmember_spoken"))

        -- ---------------------------------------------------------------
        -- Alfonse: the gherkin password.
        -- ---------------------------------------------------------------
        t.exec("goto-alfonse", t.player.goto_tile, 2793, 3188, 0)
        t.exec("talkToAlfonse", t.player.talk_to, "alfonse_the_waiter")
        t.exec("talkToAlfonse-dialog", t.chat.play, {
            "npc:Welcome to the Shrimp and Parrot.",
            "choose:Do you sell Gherkins?",
            "player:Do you sell Gherkins?",
            "npc:Hmmmm Gherkins eh? Ask Charlie the cook",
            "mesbox:Alfonse winks at you.",
        })
        t.expect("quest.stage.phoenix_talked_alfonse", t.quest.expect_stage("phoenix_talked_alfonse"))

        -- ---------------------------------------------------------------
        -- Into the kitchen (herokitchendoor opens once heroquest >=
        -- phoenix_talked_alfonse & %phoenixgang = phoenixgang_complete,
        -- brimhaven_restaurant.rs2:8-13).
        -- ---------------------------------------------------------------
        t.exec("openKitchenDoor", t.player.click_loc, "herokitchendoor")

        -- ---------------------------------------------------------------
        -- Charlie the cook: the secret door into Mr Olbors' garden.
        -- ---------------------------------------------------------------
        t.exec("talkToCharlie", t.player.talk_to, "charlie_the_cook")
        t.exec("talkToCharlie-dialog", t.chat.play, {
            "npc:Hey! What are you doing back here?",
            "choose:I'm looking for a gherkin...",
            "player:I'm looking for a gherkin...",
            "npc:Aaaaaah... a fellow Phoenix! So, tell me compadre",
            "choose:I want to steal Scarface Pete's candlesticks.",
            "player:I want to steal Scarface Pete's candlesticks.",
            "npc:Ah yes, of course. The candlesticks.",
            "npc:a little assistance. The setting up of this restaurant",
            "npc:Now, at the other side of Mr Olbors",
            "npc:and we can't seem to find a way through.",
            "player:Mind if I check it out for myself?",
            "npc:Not at all! The more minds we have",
        })
        t.expect("quest.stage.phoenix_talked_charlie", t.quest.expect_stage("phoenix_talked_charlie"))

        -- ---------------------------------------------------------------
        -- Into Mr Olbors' garden (herokitchenpanel writes no state, plain
        -- p_teleport walk-through, brimhaven_restaurant.rs2:16-22).
        -- pete_sidedoor is a walk-through loc too
        -- (brimhaven_scarface_mansion.rs2:32-45); goto_tile onto its own
        -- resolved tile (section 8's documented fix for a loc placed off a
        -- short-range click's own reach) rather than click_loc it.
        -- ---------------------------------------------------------------
        t.exec("openKitchenPanel", t.player.click_loc, "herokitchenpanel")
        t.ticks(2) -- let the teleport settle

        local sidedoor_near_result, sidedoor_near = t.world.loc_near("pete_sidedoor", 15)
        local sidedoor_near_detail = "not_found"
        if sidedoor_near_result == "ok" and sidedoor_near ~= nil then
            sidedoor_near_detail = string.format("match=%s tile=%s,%s,%s",
                tostring(sidedoor_near.match), tostring(sidedoor_near.tile_x),
                tostring(sidedoor_near.tile_z), tostring(sidedoor_near.level))
        end
        t.check("sideDoor.locNear", sidedoor_near_result == "ok",
            "t.world.loc_near(pete_sidedoor, 15) -> " .. tostring(sidedoor_near_result) .. " " .. sidedoor_near_detail)
        if sidedoor_near_result == "ok" and sidedoor_near ~= nil then
            t.exec("goto-throughSideDoor", t.player.goto_tile, sidedoor_near.tile_x, sidedoor_near.tile_z, sidedoor_near.level)
        end

        -- ---------------------------------------------------------------
        -- Grip: a real fight, at his own confirmed *.spawn tile
        -- (areas/world/configs/m43_49.spawn:19).
        -- ---------------------------------------------------------------
        t.exec("goto-grip", t.player.goto_tile, 2774, 3192, 0)

        local grip_attack_result, grip_attack_detail = t.player.attack("grip", 2, 20)
        t.check("attackGrip", grip_attack_result == "ok" or grip_attack_result == "timeout", grip_attack_detail)
        t.exec("killGrip", t.npc.await_dead, "grip", 60)
        t.ticks(3) -- ai_queue3,grip's %heroquest write is not client-side the instant the corpse clears
        t.expect("quest.stage.phoenix_killed_grip", t.quest.expect_stage("phoenix_killed_grip"))
        t.expect("player.aliveAfterGrip", t.player.alive())

        -- grip_keys is a real ground drop (ai_queue3,grip -- obj_add at
        -- npc_coord), not a chat grant -- poll the entity pool before
        -- clicking, same idiom as rovingelves.lua's seed pickup.
        local keys_visible_result = t.await({
            level = function()
                return t.world.obj_near("grip_keys", 10) == "ok"
            end,
            note = "waiting for Grip's dropped keyring to reach the client's entity pool",
        }, 10)
        t.step("gripKeys.visible", keys_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(grip_keys, 10) polled up to 10 ticks -> " .. tostring(keys_visible_result))

        local keys_before_result, keys_before = t.inv.count("grip_keys")
        local keys_click_result, keys_click_detail = t.player.click_obj("grip_keys")
        if keys_click_result ~= "ok" then
            -- Grip's own corpse/death animation can cover his ground drop
            -- for a moment (the same "covered" geometry class section 8
            -- describes for a loc) -- settle and press once more.
            t.ticks(3)
            keys_click_result, keys_click_detail = t.player.click_obj("grip_keys")
        end
        t.inv.await("grip_keys", 1, 10)
        local keys_after_result, keys_after = t.inv.count("grip_keys")
        local keys_pass = keys_click_result == "ok" and keys_after_result == "ok"
            and keys_after > (keys_before_result == "ok" and keys_before or 0)
        t.step("pickUpGripKeys", keys_pass and "PASS" or "FAIL",
            string.format("click_obj grip_keys -> %s (%s), count %s -> %s",
                tostring(keys_click_result), tostring(keys_click_detail), tostring(keys_before), tostring(keys_after)))

        -- ---------------------------------------------------------------
        -- Treasure door + chest: two candlesticks.
        -- ---------------------------------------------------------------
        local treasuredoor_near_result, treasuredoor_near = t.world.loc_near("pete_treasuredoor", 15)
        local treasuredoor_near_detail = "not_found"
        if treasuredoor_near_result == "ok" and treasuredoor_near ~= nil then
            treasuredoor_near_detail = string.format("match=%s tile=%s,%s,%s",
                tostring(treasuredoor_near.match), tostring(treasuredoor_near.tile_x),
                tostring(treasuredoor_near.tile_z), tostring(treasuredoor_near.level))
            t.exec("goto-treasuredoor", t.player.goto_tile, treasuredoor_near.tile_x, treasuredoor_near.tile_z, treasuredoor_near.level)
        end
        t.check("treasuredoor.locNear", treasuredoor_near_result == "ok",
            "t.world.loc_near(pete_treasuredoor, 15) -> " .. tostring(treasuredoor_near_result) .. " " .. treasuredoor_near_detail)

        local treasuredoor_target = t.player.by_symbol("loc", "pete_treasuredoor")
        local unlock_result, unlock_detail = t.player.use_on("grip_keys", treasuredoor_target)
        t.check("unlockTreasureDoor", unlock_result == "ok", "use_on(grip_keys, pete_treasuredoor) -> "
            .. tostring(unlock_result) .. " " .. tostring(unlock_detail))

        -- The chest sequence (shutcandlechest -> opencandlechest via
        -- loc_change, brimhaven_scarface_mansion.rs2:93-110) is a plain
        -- loc_change PAIR -- trap 20's door bullet ("name the _open half")
        -- applies: press "shutcandlechest" to open it, then
        -- "opencandlechest" for the second press. Same cached tile for
        -- both, no second loc_near call on the just-transformed symbol
        -- (section 8's documented run-2 crash for this exact pair).
        local chest_near_result, chest_near = t.world.loc_near("shutcandlechest", 15)
        local chest_near_detail = "not_found"
        local chest_x, chest_z, chest_level = nil, nil, nil
        if chest_near_result == "ok" and chest_near ~= nil then
            chest_x, chest_z, chest_level = chest_near.tile_x, chest_near.tile_z, chest_near.level
            chest_near_detail = string.format("match=%s tile=%s,%s,%s",
                tostring(chest_near.match), tostring(chest_x), tostring(chest_z), tostring(chest_level))
        end
        t.check("chest.locNear", chest_near_result == "ok",
            "t.world.loc_near(shutcandlechest, 15) -> " .. tostring(chest_near_result) .. " " .. chest_near_detail)

        local open1_result, open1_detail, open1_tries = "refused", "not attempted", 0
        for attempt = 1, 2 do
            open1_tries = attempt
            if chest_x ~= nil then
                t.player.goto_tile(chest_x, chest_z, chest_level)
            end
            open1_result, open1_detail = t.player.click_loc("shutcandlechest")
            if open1_result == "ok" then
                break
            end
            t.ticks(2)
        end
        t.check("openChest-1", open1_result == "ok",
            string.format("click_loc shutcandlechest [oploc1,shutcandlechest], attempt %d/2 -> %s (%s)",
                open1_tries, tostring(open1_result), tostring(open1_detail)))
        t.ticks(3) -- the loc_change to opencandlechest is not client-side yet (section 8)

        local candlesticks_before_result, candlesticks_before = t.inv.count("petecandlestick")

        local open2_result, open2_detail, open2_tries = "refused", "not attempted", 0
        for attempt = 1, 2 do
            open2_tries = attempt
            if chest_x ~= nil then
                t.player.goto_tile(chest_x, chest_z, chest_level)
            end
            open2_result, open2_detail = t.player.click_loc("opencandlechest")
            if open2_result == "ok" then
                break
            end
            t.ticks(2)
        end
        t.check("openChest-2", open2_result == "ok",
            string.format("click_loc opencandlechest [the transformed pair's other half], attempt %d/2 -> %s (%s)",
                open2_tries, tostring(open2_result), tostring(open2_detail)))

        -- Section 8: a `~mesbox` PAUSES the content script -- opencandlechest
        -- (98-109) is entirely inv_add/mesbox/%heroquest INSIDE the branch
        -- that only runs once the page is dismissed.
        local chest_text_result, chest_text = t.chat.text()
        t.check("chest.mesboxText", chest_text_result == "ok",
            "t.chat.text() -> " .. tostring(chest_text_result) .. " " .. tostring(chest_text))
        t.exec("chest.dismissMesbox", t.chat.continue_, true)
        t.inv.await("petecandlestick", 2, 10)
        local candlesticks_after_result, candlesticks_after = t.inv.count("petecandlestick")
        local stage_after_chest_result, stage_after_chest = t.quest.stage()
        local candles_grew = candlesticks_before_result == "ok" and candlesticks_after_result == "ok"
            and candlesticks_after == candlesticks_before + 2
        t.step("lootCandlesticks", candles_grew and "PASS" or "FAIL",
            string.format("petecandlestick count %s -> %s, %%heroquest (t.quest.stage) -> %s %s",
                tostring(candlesticks_before), tostring(candlesticks_after),
                tostring(stage_after_chest_result), tostring(stage_after_chest)))

        -- RETRY: brimhaven_scarface_mansion.rs2:134 now gates the
        -- Black-Arm-route stage write on `%heroquest >=
        -- ^hero_blackarm_gangmember_spoken` (7) -- this Phoenix player sits
        -- at hero_phoenix_killed_grip (5), so the write no longer fires.
        -- Assert the FIX for real: %heroquest reads unchanged, still 5, not
        -- clobbered to 12.
        t.check("chest.heroquestUnclobbered",
            stage_after_chest_result == "ok" and stage_after_chest == 5,
            string.format("t.quest.stage() -> %s %s (want 5=hero_phoenix_killed_grip; 12=hero_blackarm_looted_chest "
                .. "would mean brimhaven_scarface_mansion.rs2:134's route gate regressed)",
                tostring(stage_after_chest_result), tostring(stage_after_chest)))

        -- ---------------------------------------------------------------
        -- Straven, second visit: hand in a candlestick for the armband
        -- (straven.rs2:97-141's straven_gangmember label, $option=5).
        -- ---------------------------------------------------------------
        t.exec("goto-straven-2", t.player.goto_tile, 3246, 9780, 0)
        t.exec("talkToStraven-2", t.player.talk_to, "straven")
        t.exec("talkToStraven-2-dialog", t.chat.play, {
            "npc:Greetings fellow gang member.",
            "choose:I have a candlestick now.",
            "player:I have a candlestick now.",
            "npc:Excellent work. Here",
        })
        t.expect("quest.stage.phoenix_obtained_armband", t.quest.expect_stage("phoenix_obtained_armband"))
        local armband_result, armband_count = t.inv.count("master_thief_armband")
        t.check("straven.armbandGranted", armband_result == "ok" and armband_count == 1,
            "inv.count(master_thief_armband) -> " .. tostring(armband_result) .. " " .. tostring(armband_count))

        -- ---------------------------------------------------------------
        -- Ice Queen: real fight for ice_gloves (fire_feather.rs2:20's own
        -- gate on the firebird feather pickup below), level 111,
        -- hp104/atk95/str94/def95 (combat_stats.generated.npc:14496-14516).
        -- She is quest-state-blind (ice_queen.rs2's own ai_queen3 drop has
        -- no %heroquest check at all) -- fightable the moment she is
        -- reached. *.spawn tile areas/world/configs/m44_155.spawn:19.
        -- The passage in is a mined rockslide (white_wolf_mountain.rs2:23-46,
        -- a plain navigation-only loc_change/forcemove obstacle, no quest
        -- state written) -- goto_tile teleports straight onto her own tile
        -- the same way section 2 documents for a ladder/trapdoor; the
        -- rune_pickaxe/mining-50 setup above is the fallback if that reads
        -- screen_position instead.
        -- ---------------------------------------------------------------
        local icequeen_goto_result = t.exec("goto-icequeen", t.player.goto_tile, 2866, 9956, 0)
        if icequeen_goto_result ~= "ok" then
            -- Fallback: mine the rockslide for real, then retry the goto.
            t.exec("goto-rockslide", t.player.goto_tile, 2839, 3518, 0)
            t.exec("mineRockslide", t.player.use_item_on_item, "rune_pickaxe", "rune_pickaxe")
            t.exec("goto-icequeen-retry", t.player.goto_tile, 2866, 9956, 0)
        end

        local icequeen_rounds = 0
        local icequeen_sharks_eaten = 0
        local icequeen_dead = false
        while not icequeen_dead and icequeen_rounds < 25 do
            icequeen_rounds = icequeen_rounds + 1

            local hp_result, hp = t.skill.read("hitpoints")
            if hp_result == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < 90 then
                local has_shark_result, has_shark = t.inv.has("shark")
                if has_shark_result == "ok" and has_shark then
                    t.player.inv_op("shark", 1) -- shark's own ifop1=Eat
                    icequeen_sharks_eaten = icequeen_sharks_eaten + 1
                end
            end

            t.player.attack("ice_queen", 2, 20)
            local await_result = t.npc.await_dead("ice_queen", 30)
            if await_result == "ok" then
                icequeen_dead = true
            end

            local alive_result = t.player.alive()
            if alive_result ~= "ok" then
                break -- the driver's own terminal player.died row ends the run right after this
            end
        end
        t.check("killIceQueen.await_dead", icequeen_dead,
            "hunted " .. tostring(icequeen_rounds) .. " round(s), ate " .. tostring(icequeen_sharks_eaten)
                .. " shark(s) -- t.npc.await_dead(ice_queen, 30) per round -> "
                .. tostring(icequeen_dead and "ok" or "not dead within the round budget"))
        t.expect("player.aliveAfterIceQueen", t.player.alive())

        -- ice_gloves is a real ground drop (ai_queue3,ice_queen -- obj_add
        -- at npc_coord, ice_queen.rs2:16), not a chat grant.
        local gloves_visible_result = t.await({
            level = function()
                return t.world.obj_near("ice_gloves", 10) == "ok"
            end,
            note = "waiting for the Ice Queen's dropped ice gloves to reach the client's entity pool",
        }, 10)
        t.step("iceGloves.visible", gloves_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(ice_gloves, 10) polled up to 10 ticks -> " .. tostring(gloves_visible_result))

        local gloves_before_result, gloves_before = t.inv.count("ice_gloves")
        local gloves_click_result, gloves_click_detail = t.player.click_obj("ice_gloves")
        if gloves_click_result ~= "ok" then
            t.ticks(3)
            gloves_click_result, gloves_click_detail = t.player.click_obj("ice_gloves")
        end
        t.inv.await("ice_gloves", 1, 10)
        local gloves_after_result, gloves_after = t.inv.count("ice_gloves")
        local gloves_pass = gloves_click_result == "ok" and gloves_after_result == "ok"
            and gloves_after > (gloves_before_result == "ok" and gloves_before or 0)
        t.step("pickUpIceGloves", gloves_pass and "PASS" or "FAIL",
            string.format("click_obj ice_gloves -> %s (%s), count %s -> %s",
                tostring(gloves_click_result), tostring(gloves_click_detail), tostring(gloves_before), tostring(gloves_after)))
        t.exec("equipIceGloves", t.player.equip, "ice_gloves")

        -- ---------------------------------------------------------------
        -- Entrana firebird: real fight (hp5/atk1, trivial) for hot_feather.
        -- fire_bird *.spawn tile areas/world/configs/m44_52.spawn:20. No
        -- quest-state check on the fight itself (entrana_firebird.rs2's
        -- ai_queue3 only gates the FEATHER drop on %heroquest < hero_complete,
        -- true here). Weapons are never stripped by a goto_tile teleport
        -- (only the monk's own boarding dialogue checks that), so no
        -- unequip is needed.
        -- ---------------------------------------------------------------
        t.exec("goto-firebird", t.player.goto_tile, 2847, 3386, 0)
        local firebird_attack_result, firebird_attack_detail = t.player.attack("fire_bird", 2, 20)
        t.check("attackFirebird", firebird_attack_result == "ok" or firebird_attack_result == "timeout", firebird_attack_detail)
        t.exec("killFirebird", t.npc.await_dead, "fire_bird", 30)

        -- hot_feather is a real ground drop (entrana_firebird.rs2:13) --
        -- pickup needs ice_gloves WORN (fire_feather.rs2:20's op3 gate),
        -- already equipped above.
        local feather_visible_result = t.await({
            level = function()
                return t.world.obj_near("hot_feather", 10) == "ok"
            end,
            note = "waiting for the firebird's dropped feather to reach the client's entity pool",
        }, 10)
        t.step("hotFeather.visible", feather_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(hot_feather, 10) polled up to 10 ticks -> " .. tostring(feather_visible_result))

        local feather_before_result, feather_before = t.inv.count("hot_feather")
        local feather_click_result, feather_click_detail = t.player.click_obj("hot_feather")
        if feather_click_result ~= "ok" then
            t.ticks(3)
            feather_click_result, feather_click_detail = t.player.click_obj("hot_feather")
        end
        t.inv.await("hot_feather", 1, 10)
        local feather_after_result, feather_after = t.inv.count("hot_feather")
        local feather_pass = feather_click_result == "ok" and feather_after_result == "ok"
            and feather_after > (feather_before_result == "ok" and feather_before or 0)
        t.step("pickUpHotFeather", feather_pass and "PASS" or "FAIL",
            string.format("click_obj hot_feather (worn ice_gloves) -> %s (%s), count %s -> %s",
                tostring(feather_click_result), tostring(feather_click_detail), tostring(feather_before), tostring(feather_after)))

        -- ---------------------------------------------------------------
        -- Gerrant: Blamish snail slime for the lava-proof rod.
        -- gerrant.rs2:3-45 straven-style p_choice menu, option 3, real
        -- click. *.spawn tile areas/world/configs/m47_50.spawn:10.
        -- ---------------------------------------------------------------
        t.exec("goto-gerrant", t.player.goto_tile, 3013, 3225, 0)
        t.exec("talkToGerrant", t.player.talk_to, "gerrant")
        t.exec("talkToGerrant-dialog", t.chat.play, {
            "npc:Welcome! You can buy fishing equipment",
            "choose:I want to find out how to catch a lava eel.",
            "player:I want to find out how to catch a lava eel.",
            "npc:Lava eels eh?",
            "npc:You know... thinking about it",
            "mesbox:Gerrant searches around a bit.",
            "npc:Aha! Here it is!",
        })
        local slime_result, slime_count = t.inv.count("blamish_snail_slime")
        t.check("gerrant.slimeGranted", slime_result == "ok" and slime_count >= 1,
            "inv.count(blamish_snail_slime) -> " .. tostring(slime_result) .. " " .. tostring(slime_count))

        -- ---------------------------------------------------------------
        -- Mix Blamish oil (brew.dbrow's herblore_blamish_oil row, level 25,
        -- [opheldu,blamish_snail_slime] -> ~attempt_brew_potion), then oil
        -- the fishing rod (oily_fishing_rod.rs2:10-21).
        -- ---------------------------------------------------------------
        t.exec("mixBlamishOil", t.player.use_item_on_item, "blamish_snail_slime", "harralandervial")
        local oil_result, oil_count = t.inv.count("blamish_oil")
        t.check("mixBlamishOil.oilMade", oil_result == "ok" and oil_count >= 1,
            "inv.count(blamish_oil) -> " .. tostring(oil_result) .. " " .. tostring(oil_count))

        t.exec("oilTheRod", t.player.use_item_on_item, "blamish_oil", "fishing_rod")
        local oilyrod_result, oilyrod_count = t.inv.count("oily_fishing_rod")
        t.check("oilTheRod.rodMade", oilyrod_result == "ok" and oilyrod_count >= 1,
            "inv.count(oily_fishing_rod) -> " .. tostring(oilyrod_result) .. " " .. tostring(oilyrod_count))

        -- ---------------------------------------------------------------
        -- Fish a lava eel at Taverley Dungeon's own lava-fishing spot
        -- (skill_fishing/scripts/fishing_spots/lavafish.rs2, category 1313,
        -- *.spawn tile areas/world/configs/m45_152.spawn:8). op1 already
        -- resolves to `@attempt_fish_lava_eel`.
        -- ---------------------------------------------------------------
        t.exec("goto-lavafish", t.player.goto_tile, 2890, 9766, 0)
        t.exec("fish.lavaeel", t.player.talk_to, "0_45_152_lavafish")
        local raweel_result, raweel_detail = t.inv.await("raw_lava_eel", 1, 30)
        t.step("fish.lavaeel_caught", raweel_result == "ok" and "PASS" or "FAIL",
            "inv.await(raw_lava_eel, 1, 30) -> " .. tostring(raweel_result) .. " " .. tostring(raweel_detail))

        -- ---------------------------------------------------------------
        -- Cook it: light a fire on open ground (the fixture's own start
        -- tile, the same real click sequence seaslug.lua uses), then
        -- use_on the raw eel -- cooking_generic_lava_eel always succeeds
        -- (cooking_generic.dbrow's own successchance,1,1).
        -- ---------------------------------------------------------------
        t.exec("fire.goto", t.player.goto_tile, 3206, 3233, 0)
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        local fire_msg_result, fire_msg_detail = t.msg.await("The fire catches", 15)
        t.step("fire.lit", fire_msg_result == "ok" and "PASS" or "FAIL",
            "msg.await('The fire catches', 15) -> " .. tostring(fire_msg_result) .. " " .. tostring(fire_msg_detail))

        -- by_symbol's own search can resolve some OTHER "fire" already in
        -- the pool, not the one just lit (eadgar.lua's dryThistle hit this
        -- same seam) -- loc_near pins the specific fire we made, but the
        -- zone packet carrying the loc_add lands a couple of ticks behind
        -- the chat line msg.await already settled on (measured this pass:
        -- reading loc_near in the same tick as the "fire catches" message
        -- answers not_found; the scene has not been given the loc yet).
        t.ticks(2)
        local fire_lookup_result, fire_row = t.world.loc_near("fire", 10)
        t.check("lookup.fire", fire_lookup_result == "ok" and fire_row ~= nil,
            "world.loc_near(fire, 10) -> " .. tostring(fire_lookup_result))
        t.exec("cookLavaEel", t.player.use_on, "raw_lava_eel", fire_row)
        local eel_result, eel_detail = t.inv.await("lava_eel", 1, 10)
        t.step("cookLavaEel.cooked", eel_result == "ok" and "PASS" or "FAIL",
            "inv.await(lava_eel, 1, 10) -> " .. tostring(eel_result) .. " " .. tostring(eel_detail))

        -- ---------------------------------------------------------------
        -- All three deliverables in hand: snapshot every skill now, right
        -- before the hand-in, so the reward rows below measure ONLY the
        -- completion's own stat_advance calls -- not the Attack/Strength/
        -- Defence/Hitpoints XP the Grip/Ice Queen fights or the Fishing/
        -- Cooking XP the eel arm already granted as ordinary side effects.
        -- ---------------------------------------------------------------
        local feather_have_result, feather_have = t.inv.count("hot_feather")
        local armband_have_result, armband_have = t.inv.count("master_thief_armband")
        local eel_have_result, eel_have = t.inv.count("lava_eel")
        t.check("finalItems.allThreeHeld",
            feather_have_result == "ok" and feather_have >= 1
                and armband_have_result == "ok" and armband_have >= 1
                and eel_have_result == "ok" and eel_have >= 1,
            string.format("hot_feather=%s(%s) master_thief_armband=%s(%s) lava_eel=%s(%s)",
                tostring(feather_have_result), tostring(feather_have),
                tostring(armband_have_result), tostring(armband_have),
                tostring(eel_have_result), tostring(eel_have)))

        local snapshot_result, reward_snapshot = t.skill.snapshot()
        t.check("reward.snapshotTaken", snapshot_result == "ok", "t.skill.snapshot() -> " .. tostring(snapshot_result))

        -- ---------------------------------------------------------------
        -- Achietties: the real hand-in (achietties.rs2:20-32). The
        -- item-complete branch fires because all three are held; it queues
        -- hero_quest_complete and deletes the three items in the caller.
        -- ---------------------------------------------------------------
        t.exec("goto-achietties-handin", t.player.goto_tile, 2903, 3510, 0)
        t.exec("achietties.handIn", t.player.talk_to, "achietties")
        t.exec("achietties.handIn-dialog", t.chat.play, {
            -- achietties.rs2:18 opens EVERY talk_to with this greeting,
            -- unconditionally, before branching on hero_in_progress --
            -- measured run: a list starting "How goes thy quest" died on
            -- page 1 with a mismatch naming this exact text.
            "npc:Greetings. Welcome to the Heroes",
            "npc:How goes thy quest",
            "player:I have all the required items.",
            "npc:I see that you have. Well done",
            "player:W-what? What do you mean?",
            "npc:I'm sorry, I was just having a little fun",
            "npc:Congratulations! You have completed",
        })
        t.ticks(3) -- queue(hero_quest_complete, 0, 0) is not client-side yet

        t.quest.expect_complete() -- writes quest.varp_complete/quest.scroll_title/quest.points/quest.journal

        -- ---------------------------------------------------------------
        -- Every reward Quest Helper/quest_hero.rs2:49-60's own
        -- stat_advance list grants (twelve skills, docs/quests/
        -- heroes_quest.md section 2's XP table).
        -- ---------------------------------------------------------------
        t.check("reward.attack", t.skill.expect_gain("attack", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(attack, 3075)")
        t.check("reward.defence", t.skill.expect_gain("defence", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(defence, 3075)")
        t.check("reward.strength", t.skill.expect_gain("strength", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(strength, 3075)")
        t.check("reward.hitpoints", t.skill.expect_gain("hitpoints", 3075, reward_snapshot) == "ok",
            "t.skill.expect_gain(hitpoints, 3075)")
        t.check("reward.ranged", t.skill.expect_gain("ranged", 2075, reward_snapshot) == "ok",
            "t.skill.expect_gain(ranged, 2075)")
        t.check("reward.fishing", t.skill.expect_gain("fishing", 2725, reward_snapshot) == "ok",
            "t.skill.expect_gain(fishing, 2725)")
        t.check("reward.cooking", t.skill.expect_gain("cooking", 2825, reward_snapshot) == "ok",
            "t.skill.expect_gain(cooking, 2825)")
        t.check("reward.woodcutting", t.skill.expect_gain("woodcutting", 1575, reward_snapshot) == "ok",
            "t.skill.expect_gain(woodcutting, 1575)")
        t.check("reward.firemaking", t.skill.expect_gain("firemaking", 1575, reward_snapshot) == "ok",
            "t.skill.expect_gain(firemaking, 1575)")
        t.check("reward.smithing", t.skill.expect_gain("smithing", 2275, reward_snapshot) == "ok",
            "t.skill.expect_gain(smithing, 2275)")
        t.check("reward.mining", t.skill.expect_gain("mining", 2575, reward_snapshot) == "ok",
            "t.skill.expect_gain(mining, 2575)")
        t.check("reward.herblore", t.skill.expect_gain("herblore", 1325, reward_snapshot) == "ok",
            "t.skill.expect_gain(herblore, 1325)")

        -- achietties.rs2:26-29 deletes all three final items in the caller
        -- (the completion "consumes hot_feather+lava_eel+master_thief_armband
        -- exactly once" -- quest_hero.rs2's own ::herorun HERORUN treats a
        -- surviving final item as FAIL).
        t.check("reward.hotFeatherConsumed", t.inv.expect_absent("hot_feather") == "ok",
            "t.inv.expect_absent(hot_feather)")
        t.check("reward.lavaEelConsumed", t.inv.expect_absent("lava_eel") == "ok",
            "t.inv.expect_absent(lava_eel)")
        t.check("reward.armbandConsumed", t.inv.expect_absent("master_thief_armband") == "ok",
            "t.inv.expect_absent(master_thief_armband)")

        t.finish(0)
        return
    end,
}
