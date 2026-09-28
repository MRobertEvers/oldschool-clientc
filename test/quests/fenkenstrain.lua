-- Creature of Fenkenstrain -- full route, sign through completion.
-- Guide: Quest Helper CreatureOfFenkenstrain.java (5ea99d5e), 35 leaf steps
-- across 7 panels. Content: OSRS-Content quests/quest_fenkenstrain/scripts/
-- {fenkenstrain,fenkenstrain_parts,fenkenstrain_lightning,fenkenstrain_finish}.rs2.
--
-- Every action row that drives a guide leaf step is named EXACTLY after that
-- step's own Java variable (helper_coverage.py's driven() matches a PASS row
-- whose name equals or starts with the step's normalised name -- see
-- docs/QUEST_AUTHORING.md trap 32 and helper_coverage.py's driven()).
-- talkToFrenkenstrain keeps Quest Helper's own misspelling ("Frenkenstrain")
-- on purpose: that is the guide's literal step name.
--
-- Body parts, brain, amulets, shed key, canes and the conductor mould are all
-- obtained by real gathering (bought/dug/searched/crafted) -- none are
-- brought along by setup (trap 16). setup only carries the items Quest
-- Helper's own getItemRequirements() lists as brought along: ghostspeak
-- amulet, spade, needle, 5 thread, a silver bar, 3 bronze wire, coins, and a
-- weapon (armor is listed generically as "Armour and weapons defeat a level
-- 51 monster" -- a rune scimitar plus setlevel'd combat stats stand in for
-- gear the fixture does not hand out).

return {
    id = "fenkenstrain",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::fenkenstrain", -- quest debug reset: satisfies Priest in Peril /
                            -- Restless Ghost prereqs, zeroes every %fenk_*
                            -- flag, teleports to the Canifis signpost. This
                            -- STAGES the quest (trap 8), it does not finish it.
        "::setlevel hitpoints 80",
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel crafting 40",  -- boostable 20 req (conductor casting)
        "::setlevel thieving 40",  -- boostable 25 req (final pickpocket)
        "::give rune_scimitar 1",
        "::give amulet_of_ghostspeak 1",
        "::give spade 1",
        "::give needle 1",
        "::give thread 5",
        "::give silver_bar 1",
        "::give bronzecraftwire 3",
        "::give coins 200",
    },

    run = function(t)
        t.quest.bind({
            varp = "fenk_quest",
            constants = {
                not_started = 0, sign_read = 1, hired = 2, parts = 3,
                lightning = 4, alive = 5, tower = 6, spoke_creature = 7,
                complete = 9,
            },
            display = "Creature of Fenkenstrain",
            points = 2,
        })
        t.ticks(2)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("equip.ghostspeak", t.player.equip, "amulet_of_ghostspeak")
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")

        -- ================= Panel: Starting off =================
        t.exec("readSign", t.player.click_loc, "fenk_signpost", 1)
        t.exec("readSign-dialog", t.chat.play, {
            "mesbox:The signpost has a note pinned",
        })
        t.expect("quest.stage.sign_read", t.quest.expect_stage("sign_read"))

        t.exec("goto-castle", t.player.goto_tile, 3551, 3550, 0)
        t.exec("talkToFrenkenstrain", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("talkToFrenkenstrain-dialog", t.chat.play, {
            "npc:Have you come to apply for the",
            "options",
            "choose:Yes, if it pays well.",
            "player:Yes, if it pays well.",
            "npc:I'll have to ask you some quest",
            "player:Okay...",
            "npc:How would you describe yoursel",
            "options",
            "choose:Braindead.",
            "player:Braindead.",
            "npc:Mmmm, I see.",
            "npc:Just one more question. What w",
            "options",
            "choose:Grave-digging.",
            "player:Grave-digging.",
            "npc:Mmmm, I see.",
            "npc:Looks like you're just the",
            "player:Is there anything you'd like",
            "npc:Yes, there is. You're highly sk",
            "player:Err...yes, that's what I said",
            "npc:Excellent. Now listen carefully",
            "player:Stuff?",
            "npc:That's what I said...stuff.",
            "player:What kind of stuff?",
            "npc:Well...dead stuff.",
            "player:Go on...",
            "npc:I need you to get me enough dea",
            "player:Right...okay...if you insist.",
        })
        t.expect("quest.stage.hired", t.quest.expect_stage("hired"))

        -- Pickled brain: only sellable from Roavar once hired
        -- (werewolfinnkeeper.rs2's beer-menu gate reads
        -- %creatureoffenkenstrain >= fenk_hired), so this leg runs after the
        -- interview even though Quest Helper lists it as its own earlier
        -- panel.
        t.exec("goto-bar", t.player.goto_tile, 3493, 3471, 0)
        t.exec("getPickledBrain", t.player.talk_to, "werewolfinnkeeper", 1)
        t.exec("getPickledBrain-dialog", t.chat.play, {
            "player:Hello there!",
            "npc:Greetings traveller",
            "options",
            "choose:Do you sell pickled brains?",
            "player:Do you sell pickled brains?",
            "npc:Pickled brain, my friend",
            "options",
            "choose:I'll buy one, please.",
            "player:I'll buy one, please.",
            "npc:Pleasure doing business.",
        })
        t.exec("getPickledBrain.held", t.inv.await, "fenk_brain", 1, 10)

        -- ================= Panel: Graverobbing =================
        t.exec("goUpstairsForStar", t.player.goto_tile, 3559, 3552, 1)
        t.exec("goto-eastBookcase", t.player.goto_tile, 3555, 3558, 1)
        t.exec("getBook1", t.player.click_loc, "fenk_bookcase", 1)
        t.exec("getBook1-dialog", t.chat.play, {
            "options",
            "choose:Handy Maggot Avoidance Techniques.",
            "mesbox:As you pull the book a hidden",
        })
        t.exec("getBook1.held", t.inv.await, "fenk_obsidian_amulet", 1, 10)

        t.exec("goto-westBookcase", t.player.goto_tile, 3542, 3558, 1)
        t.exec("getBook2", t.player.click_loc, "fenk_bookcase", 1)
        t.exec("getBook2-dialog", t.chat.play, {
            "options",
            "choose:The Joy of Grave Digging.",
            "mesbox:As you pull the book a hidden",
        })
        t.exec("getBook2.held", t.inv.await, "fenk_marble_amulet", 1, 10)

        t.exec("combineAmulet", t.player.use_item_on_item, "fenk_marble_amulet", "fenk_obsidian_amulet")
        t.exec("combineAmulet.held", t.inv.await, "fenk_star_amulet", 1, 10)

        t.exec("goDownstairsForStar", t.player.goto_tile, 3537, 3551, 0)

        t.exec("goto-gardener1", t.player.goto_tile, 3551, 3561, 0)
        t.exec("talkToGardenerForHead", t.player.talk_to, "fenk_gardener_multi_2", 1)
        t.exec("talkToGardenerForHead-dialog", t.chat.play, {
            "player:What happened to your head?",
            "npc:It got chopped off while I was",
            "npc:Dig at my grave southeast of the",
        })
        t.exec("talkToGardenerForHead.flag", t.var.await_server, "fenk_spoken_to_gardener", 1, 10)

        t.exec("goToHeadGrave.goto", t.player.goto_tile, 3608, 3489, 0)
        t.exec("goToHeadGrave", t.player.click_loc, "fenk_grave_poor", 2)
        -- The dig's mesbox opens after anim(human_dig,0); p_delay(2), and a
        -- click_loc that first has to step off the grave's own tile (trap 8
        -- of section 8) adds a further tick -- click_loc's settle can win
        -- the race on the weaker map_flag arm instead of waiting for the
        -- mesbox (trap 24). Poll for the page instead of guessing a tick
        -- count.
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "goToHeadGrave dig mesbox" }, 8)
        t.exec("goToHeadGrave-dialog", t.chat.play, {
            "mesbox:...and you unearth a decapitated",
        })
        t.exec("goToHeadGrave.held", t.inv.await, "fenk_head_empty", 1, 10)

        t.exec("combinedHead", t.player.use_item_on_item, "fenk_head_empty", "fenk_brain")
        t.exec("combinedHead.held", t.inv.await, "fenk_head_full", 1, 10)

        t.exec("goto-coffin", t.player.goto_tile, 3578, 3527, 0)
        local coffin_target = t.player.by_symbol("loc", "fenk_coffin")
        t.exec("useStarOnGrave", t.player.use_on, "fenk_star_amulet", coffin_target)
        t.exec("useStarOnGrave-dialog", t.chat.play, {
            "mesbox:The star amulet fits exactly",
        })
        t.exec("useStarOnGrave.flag", t.var.await_server, "fenk_coffin", 1, 10)

        t.exec("enterExperimentCave", t.player.click_loc, "fenk_coffin", 1)
        t.ticks(2)
        local _, cave_tile = t.world.tile()
        t.check("enterExperimentCave.tile", cave_tile ~= nil and cave_tile.z > 6400,
            "tile " .. tostring(cave_tile and cave_tile.x) .. "," .. tostring(cave_tile and cave_tile.z)
                .. "," .. tostring(cave_tile and cave_tile.level) .. " (underground band is z+6400)")

        t.exec("goto-experiment", t.player.goto_tile, 3554, 9948, 0)
        t.exec("killExperiment", t.player.attack, "fenk_experiment_1", 2, 25)
        t.exec("killExperiment.dead", t.npc.await_dead_engaged, 40, 6)

        local key_result, key_row = t.world.obj_near("fenk_mausoleum_key", 15)
        t.check("pickupKey.locate", key_result == "ok",
            "world.obj_near(fenk_mausoleum_key,15) -> " .. tostring(key_result) .. " " .. tostring(key_row))
        if key_result == "ok" then
            t.exec("goto-key", t.player.goto_tile, key_row.tile_x, key_row.tile_z, key_row.level)
        end
        -- click_obj is hollow on success (ok, nil detail) -- section 8.
        local pickup_result = t.player.click_obj("fenk_mausoleum_key", 3)
        t.check("pickupKey", pickup_result == "ok", "click_obj fenk_mausoleum_key -> " .. tostring(pickup_result))
        t.exec("pickupKey.held", t.inv.await, "fenk_mausoleum_key", 1, 10)

        t.exec("goto-mausoleumDoor", t.player.goto_tile, 3511, 9957, 0)
        local mausoleum_door = t.player.by_symbol("loc", "fenk_mausoleum_door")
        t.exec("openMausoleumDoor", t.player.use_on, "fenk_mausoleum_key", mausoleum_door)
        t.exec("openMausoleumDoor.flag", t.var.await_server, "fenk_unlocked_cavern", 1, 10)

        t.exec("leaveExperimentCave", t.player.goto_tile, 3503, 3576, 0)

        t.exec("getTorso", t.player.click_loc, "fenk_grave", 2)
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "getTorso dig mesbox" }, 8)
        t.exec("getTorso-dialog", t.chat.play, {
            "mesbox:...and you unearth a torso.",
        })
        t.exec("getTorso.held", t.inv.await, "fenk_torso", 1, 10)

        t.exec("goto-armsGrave", t.player.goto_tile, 3504, 3577, 0)
        t.exec("getArm", t.player.click_loc, "fenk_grave", 2)
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "getArm dig mesbox" }, 8)
        t.exec("getArm-dialog", t.chat.play, {
            "mesbox:...and you unearth a pair of arms.",
        })
        t.exec("getArm.held", t.inv.await, "fenk_arms", 1, 10)

        t.exec("goto-legsGrave", t.player.goto_tile, 3506, 3576, 0)
        t.exec("getLeg", t.player.click_loc, "fenk_grave", 2)
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "getLeg dig mesbox" }, 8)
        t.exec("getLeg-dialog", t.chat.play, {
            "mesbox:...and you unearth a pair of legs.",
        })
        t.exec("getLeg.held", t.inv.await, "fenk_legs", 1, 10)

        t.exec("goto-castle2", t.player.goto_tile, 3551, 3550, 0)
        t.exec("deliverBodyParts", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("deliverBodyParts-dialog", t.chat.play, {
            "options",
            "choose:I have some body parts for you.",
            "player:I have some body parts for you",
            "npc:Excellent! Exactly what I needed",
            "npc:Now I need a needle and five s",
        })
        t.expect("quest.stage.parts", t.quest.expect_stage("parts"))

        -- ================= Panel: Getting tools =================
        t.exec("gatherNeedleAndThread", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("gatherNeedleAndThread-dialog", t.chat.play, {
            "npc:Where are my needle and thread",
            "npc:Ah, a needle. Wonderful.",
            "npc:Some thread. Excellent.",
            "mesbox:Fenkenstrain uses the needle a",
            "npc:Perfect. But I need one more t",
            "player:Really?",
            "npc:I have honed to perfection an a",
            "player:And what power is this?",
            "npc:The power of lightning.",
            "npc:The storm that brews overhead w",
            "npc:Repair the conductor and BEGONE",
        })
        t.expect("quest.stage.lightning", t.quest.expect_stage("lightning"))

        -- ================= Panel: Attracting lightning =================
        t.exec("goto-gardener2", t.player.goto_tile, 3551, 3561, 0)
        t.exec("talkToGardenerForKey", t.player.talk_to, "fenk_gardener_multi_2", 1)
        t.exec("talkToGardenerForKey-dialog", t.chat.play, {
            "options",
            "choose:Do you know where the key to the shed is?",
            "player:Do you know where the key to",
            "npc:Got it right 'ere in my pocket",
        })
        t.exec("talkToGardenerForKey.held", t.inv.await, "fenk_shed_key", 1, 10)

        t.exec("goto-shedDoor", t.player.goto_tile, 3548, 3567, 0)
        local shed_door = t.player.by_symbol("loc", "fenk_shed_door")
        t.exec("openShedDoor", t.player.use_on, "fenk_shed_key", shed_door)
        t.exec("openShedDoor.flag", t.var.await_server, "fenk_unlocked_shed", 1, 10)

        t.exec("goto-cupboard", t.player.goto_tile, 3546, 3563, 0)
        t.exec("searchForBrush.open", t.player.click_loc, "fenk_broomcupboard", 1)
        t.exec("searchForBrush", t.player.click_loc, "fenk_broomcupboard_open", 2)
        t.exec("searchForBrush.held", t.inv.await, "fenk_brush0", 1, 10)

        t.exec("goto-canepile", t.player.goto_tile, 3551, 3564, 0)
        t.exec("grabCanes-1", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes-2", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes-3", t.player.click_loc, "fenk_canepile", 1)
        t.exec("grabCanes.held", t.inv.await, "fenk_cane", 3, 10)

        t.exec("extendBrush-1", t.player.use_item_on_item, "fenk_cane", "fenk_brush0")
        t.exec("extendBrush-2", t.player.use_item_on_item, "fenk_cane", "fenk_brush1")
        t.exec("extendBrush-3", t.player.use_item_on_item, "fenk_cane", "fenk_brush2")
        t.exec("extendBrush.held", t.inv.await, "fenk_brush3", 1, 10)

        t.exec("goUpWestStairs", t.player.goto_tile, 3538, 3552, 1)
        t.exec("goto-fireplace", t.player.goto_tile, 3544, 3555, 1)
        local fireplace_target = t.player.by_symbol("loc", "fenk_fireplace")
        t.exec("searchFirePlace", t.player.use_on, "fenk_brush3", fireplace_target)
        t.exec("searchFirePlace-dialog", t.chat.play, {
            "mesbox:A lightning conductor mould falls",
        })
        t.exec("searchFirePlace.held", t.inv.await, "fenk_lightning_mould", 1, 10)

        -- Any furnace works (smelting.rs2's silver_bar case calls
        -- fenk_try_cast_conductor before the ordinary jewellery menu). None
        -- is placed near Canifis; Keldagrim's smithing quarter is the
        -- already-proven target (test/quests/betweenarock.lua's own
        -- smeltCannonball leg).
        t.exec("makeLightningRod.goto", t.player.goto_tile, 2869, 10202, 0)
        local furnace_result, furnace = t.world.loc_near("dwarf_keldagrim_furnace", 60)
        t.check("makeLightningRod.locate", furnace_result == "ok",
            "world.loc_near(dwarf_keldagrim_furnace,60) -> " .. tostring(furnace_result) .. " " .. tostring(furnace))
        if furnace_result == "ok" then
            t.exec("makeLightningRod.goto2", t.player.goto_tile, furnace.tile_x, furnace.tile_z, furnace.level)
        end
        local furnace_target = t.player.by_symbol("loc", "dwarf_keldagrim_furnace")
        t.exec("makeLightningRod", t.player.use_on, "silver_bar", furnace_target)
        t.exec("makeLightningRod.held", t.inv.await, "fenk_conductor", 1, 10)

        t.exec("goUpWestStairsWithRod", t.player.goto_tile, 3537, 3553, 0)
        t.exec("goUpTowerLadder", t.player.goto_tile, 3549, 3537, 2)
        t.exec("repairConductor", t.player.click_loc, "fenk_conductor_broken", 1)
        t.exec("repairConductor-dialog", t.chat.play, {
            "mesbox:You repair the lightning conductor",
        })
        t.expect("quest.stage.alive", t.quest.expect_stage("alive"))

        t.exec("goBackToFirstFloor", t.player.goto_tile, 3551, 3550, 0)
        t.exec("talkToFenkenstrainAfterFixingRod", t.player.talk_to, "fenk_fenkenstrain", 1)
        t.exec("talkToFenkenstrainAfterFixingRod-dialog", t.chat.play, {
            "player:So did it work, then?",
            "npc:Yes, I'm afraid it did",
            "npc:I tricked it into going up to",
            "npc:I have no control over it!",
            "npc:Destroy it!!!",
        })
        t.exec("talkToFenkenstrainAfterFixingRod.held", t.inv.await, "fenk_tower_key", 1, 10)
        t.expect("quest.stage.tower", t.quest.expect_stage("tower"))

        -- ================= Panel: Facing the monster =================
        t.exec("goToMonsterFloor1", t.player.goto_tile, 3548, 3549, 1)
        local tower_door = t.player.by_symbol("loc", "fenk_tower_door")
        t.exec("openLockedDoor", t.player.use_on, "fenk_tower_key", tower_door)
        t.exec("openLockedDoor.flag", t.var.await_server, "fenk_unlocked_tower", 1, 10)

        local walk_result = t.player.walk_to(3548, 3553)
        t.check("towerRoom.walk", walk_result == "ok", "walk_to 3548,3553 -> " .. tostring(walk_result))

        t.exec("goToMonsterFloor2", t.player.click_loc, "ladder", 1)
        t.ticks(3)
        local _, plane2_tile = t.world.tile()
        t.check("goToMonsterFloor2.tile", plane2_tile ~= nil and plane2_tile.level == 2,
            "tile " .. tostring(plane2_tile and plane2_tile.x) .. "," .. tostring(plane2_tile and plane2_tile.z)
                .. "," .. tostring(plane2_tile and plane2_tile.level))

        local present_result, present_creature = t.npc.await_present("fenk_creature", 6, 5)
        t.check("talkToMonster.present", present_result == "ok",
            "await_present fenk_creature -> " .. tostring(present_result) .. " "
                .. tostring(type(present_creature) == "table"
                    and (tostring(present_creature.tile_x) .. "," .. tostring(present_creature.tile_z))
                    or present_creature))
        t.exec("talkToMonster", t.player.talk_to, "fenk_creature", 1)
        t.exec("talkToMonster-dialog", t.chat.play, {
            "player:I am commanded to destroy you, creature!",
            "npc:Oh that's not very nice",
            "player:You don't look very dangerous.",
            "npc:How do I look?",
            "player:You really don't know",
            "mesbox:The creature stumbles over towards the mirror",
            "npc:AAAAARRGGGGHHHH!",
            "mesbox:The creature becomes instantly sober",
            "player:I'm sorry.",
            "npc:No - it was him I wager",
            "player:Who are - were - you?",
            "npc:I was Rologarth",
            "player:So the castle wasn't really abandoned",
            "npc:Is that what he told you?",
            "player:I found your brain in a jar",
            "npc:Of that I will not speak.",
            "player:Is there anything I can do for you",
            "npc:Only one - please stop Fenkenstrain",
        })
        t.expect("quest.stage.spoke_creature", t.quest.expect_stage("spoke_creature"))

        -- ================= Panel: Finishing off =================
        t.exec("goto-descend", t.player.click_loc, "laddertop", 1)
        t.ticks(3)
        local _, plane1_tile = t.world.tile()
        t.check("goto-descend.tile", plane1_tile ~= nil and plane1_tile.level == 1,
            "tile " .. tostring(plane1_tile and plane1_tile.x) .. "," .. tostring(plane1_tile and plane1_tile.z)
                .. "," .. tostring(plane1_tile and plane1_tile.level))

        t.exec("goto-doctor", t.player.goto_tile, 3551, 3548, 0)
        local snap_result, snap = t.skill.snapshot()
        t.check("reward.snapshot", snap_result == "ok", "skill.snapshot -> " .. tostring(snap_result))

        t.exec("pickPocketFenkenstrain", t.player.talk_to, "fenk_fenkenstrain", 3)
        t.exec("pickPocketFenkenstrain.held", t.inv.await, "ring_of_charos", 1, 10)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))

        t.ticks(3)
        t.settle()
        t.quest.expect_complete()

        t.check("reward.thieving_xp", t.skill.expect_gain("thieving", 1000, snap))
        t.check("reward.ring_of_charos", t.inv.expect_has("ring_of_charos", 1))

        t.finish(0)
    end,
}
