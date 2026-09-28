-- Witch's House, driven for real end to end: talk to the Boy in Taverley,
-- find the door key under the pot, cross the basement shock gate wearing
-- gloves, fetch the magnet from the basement cupboard, fit it to the mouse
-- to unlock the back door, read the witch's diary (keeps the door unlocked),
-- search the garden fountain for the shed key, use it on the shed (spawns
-- the four-form shapeshifter experiment), fight all four forms for real,
-- pick up the ball and hand it back to the Boy.
--
-- Read against OSRS-Content/osrs239-content/server/scripts/quests/quest_ball/
-- (quest_ball_locs.rs2, nora_t_hagg.rs2, witches_diary.rs2) and
-- server/scripts/areas/area_taverly/scripts/boy.rs2, cross-checked against
-- Quest Helper's WitchsHouse.java (quest-helper/.../helpers/quests/
-- witchshouse/WitchsHouse.java) for every WorldPoint and op string.
--
-- Prerequisites cheated in setup (trap 16 -- gear/tools are a prerequisite,
-- never this quest's own deliverable): combat levels for the four
-- full-health shapeshifter forms (levels 19/30/42/53, witches_house.npc),
-- a rune loadout, food and cheese ("multiple if you mess up", Quest Helper's
-- own note). leather_gloves is marked canBeObtainedDuringQuest() by Quest
-- Helper ("search the nearby boxes until you get a pair"), but this content
-- pack implements NO box-search trigger for them anywhere in
-- quest_ball_locs.rs2 or the m45_54 area spawn table (grepped; the only
-- leather_gloves ground spawns in the whole pack are in unrelated map
-- squares m50_52/m26_48/m30_147/m23_55/m48_54/m49_49) -- there is nothing to
-- click here, so they are given directly, same idiom as Heroes' Quest's
-- non-canBeObtainedDuringQuest bring-alongs (hero.lua).
--
-- Every other mid-quest item (door key, magnet, diary, shed key, ball) is
-- the quest's own deliverable and is driven for real below, never ::given.

return {
    id = "ball",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give cheese 3", -- "Cheese (multiple if you mess up)" -- Quest Helper's own note
        "::give leather_gloves 1", -- see header: no box-search trigger exists in this content pack
        "::give rune_scimitar 1",
        "::give adamant_platebody 1", -- rune_platebody refuses Wear without Dragon Slayer (levelrequire.rs2:179-182) -- a prerequisite this quest does not grant, so a body armour with no such gate is given instead (99 defence carries the fight either way)
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 4", -- food, same idiom as hero.lua/mortton.lua/rovingelves.lua
        -- Combat levels: prerequisite for the four full-health shapeshifter
        -- forms (witches_house.npc: glob atk18/def19, spider atk28/def29,
        -- bear atk38/def39, wolf atk48/def49hp51) -- same idiom hero.lua
        -- uses for its own level-111-class fight.
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "ballquest",
            constants = {
                complete = 7,
                defeated_experiment = 6,
                found_magnet = 2,
                not_started = 0,
                questpoints = 4,
                read_diary_after_door = 5,
                started = 1,
                unlocked_mousedoor = 3,
            },
            row = "quest_witchshouse",
            display = "Witch's House",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Wear the rune loadout and the gloves now -- the gloves matter at
        -- the shock gate below, the armour/weapon at the shed fight later.
        t.exec("wearGloves", t.player.equip, "leather_gloves")
        t.exec("wearWeapon", t.player.equip, "rune_scimitar")
        t.exec("wearBody", t.player.equip, "adamant_platebody")
        t.exec("wearLegs", t.player.equip, "rune_platelegs")
        t.exec("wearHelm", t.player.equip, "rune_full_helm")
        t.exec("wearShield", t.player.equip, "rune_kiteshield")

        -- ---- Start the quest: talk to the Boy in Taverley ----
        t.exec("goto-talkToBoy", t.player.goto_tile, 2928, 3456, 0)
        t.exec("talkToBoy", t.player.talk_to, "ballboy", 1)
        t.exec("talkToBoy-dialog", t.chat.play, {
            "player:Hello young man.",
            "mesbox:The boy sobs.",
            "choose:What's the matter?",
            "player:What's the matter?",
            "npc:I've kicked my ball over that hedge, into that garden!",
            "choose:Ok, I'll see what I can do.",
            "player:Ok, I'll see what I can do.",
            "npc:Thanks",
        })
        t.ticks(2) -- trap 25: the branch's own varp write is not readable in the tick it was clicked
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---- Get the house key from under the pot ----
        t.exec("goto-getKey", t.player.goto_tile, 2900, 3474, 0)
        t.exec("getKey", t.player.click_loc, "witchpot", 1)
        -- trap 22: ~mesbox suspends the branch -- inv_add runs only once the
        -- page is dismissed. Dismiss before reading the count.
        t.exec("getKey-dismiss", t.chat.continue_, true)
        -- trap 24: a click/dismiss's own ok is the server's SENTENCE, not
        -- the container update -- poll, never a bare count on the next line.
        t.exec("keyFound.await", t.inv.await, "witches_doorkey", 1, 10)

        -- ---- Enter the house, go down the ladder to the basement ----
        t.exec("enterHouse", t.player.click_loc, "witchhousedoor", 1)
        -- Ladders/stairs are plain travel (QUEST_AUTHORING.md section 2 /
        -- trap 32's TRAVEL_WORDS rule) -- goto_tile the far tile at its own
        -- level, no click_loc on the ladder itself.
        t.exec("goto-goDownstairs", t.player.goto_tile, 2907, 9876, 0)

        -- ---- Cross the basement shock gate wearing the gloves ----
        local hp_before_gate = select(2, t.skill.read("hitpoints"))
        t.exec("enterGate", t.player.click_loc, "shockgater", 1)
        local hp_after_gate = select(2, t.skill.read("hitpoints"))
        t.check("enterGate.no_shock", hp_after_gate.level == hp_before_gate.level,
            "hitpoints " .. tostring(hp_before_gate.level) .. " -> " .. tostring(hp_after_gate.level)
                .. " (gloves worn -- ball_irongate_open should take the ~door_selfstage_open arm, not the shock)")

        -- ---- Open the cupboard, search it for the magnet ----
        t.exec("openCupboardAndLoot", t.player.click_loc, "magnetcbshut", 1)
        t.ticks(1) -- trap 8: let the loc_change land before the second click sees the new form
        t.exec("openCupboardAndLoot2", t.player.click_loc, "magnetcbopen", 1)
        t.exec("openCupboardAndLoot2-dismiss", t.chat.continue_, true) -- trap 22
        t.expect("quest.stage.found_magnet", t.quest.expect_stage("found_magnet"))

        -- ---- Back through the gate, up the ladder ----
        t.exec("returnGate", t.player.click_loc, "shockgatel", 1)
        t.exec("goto-goBackUpstairs", t.player.goto_tile, 2907, 3476, 0)

        -- ---- Open the two interior doors between the ladder room and the
        -- south room (grim_witch_house_door, a plain door via doors.loc's
        -- generic category=167 handler, not a quest-specific trigger -- not
        -- in quest_ball_locs.rs2 at all). Decoded maps/m45_54.jl2 (mapx=45,
        -- mapz=54): this symbol places on THREE tiles -- local (22,11) =
        -- world 2902,3467 and local (22,18) = world 2902,3474 on the ground
        -- floor, plus one more on level 1 -- three rooms in a row along z
        -- (north/ladder room, a middle room off the front door, the south
        -- room with the mousehole), each pair of rooms divided by one of
        -- these doors. click_loc resolves the NEAREST closed copy, so run 2
        -- only ever opened the z=3474 door (between the ladder room and the
        -- middle room) -- useCheeseOnHole's route crossed that one and then
        -- stalled at z=3468, one wall short of the mousehole. A second call
        -- from inside the middle room resolves the remaining closed copy at
        -- z=3467, the one that actually borders the mousehole's room. ----
        t.exec("openInteriorDoor", t.player.click_loc, "grim_witch_house_door", 1)
        t.ticks(2) -- trap 8: let the door's loc_change land in the collision map before the next click_loc paths through it
        t.exec("openInteriorDoor2", t.player.click_loc, "grim_witch_house_door", 1)
        t.ticks(2)

        -- ---- Cheese the mouse hole, fit the magnet to the mouse ----
        local mousehole = t.player.by_symbol("loc", "witchmousehole")
        t.exec("useCheeseOnHole", t.player.use_on, "cheese", mousehole)
        t.exec("useCheeseOnHole-dismiss", t.chat.continue_, true) -- trap 22
        -- trap 12: npc.await_present is hollow (ok, no detail) -- call it
        -- directly and read the pool back with npc.nearest for the row.
        local ratout_result = t.npc.await_present("witchrat", 5, 10)
        t.step("ratOut", ratout_result == "ok" and "PASS" or "FAIL", "npc.await_present -> " .. tostring(ratout_result))
        local rat_result, witchrat_row = t.npc.nearest("witchrat", 5)
        t.check("ratOut.row", rat_result == "ok", "npc.nearest(witchrat) -> " .. tostring(rat_result) .. " " .. tostring(witchrat_row))
        local witchrat = t.player.by_symbol("npc", "witchrat")
        t.exec("fitMagnetToMouse", t.player.use_on, "magnet", witchrat)
        t.ticks(2) -- trap 25
        t.expect("quest.stage.unlocked_mousedoor", t.quest.expect_stage("unlocked_mousedoor"))

        -- ---- Pick up and read the witch's diary (keeps the door unlocked
        -- if read while at exactly ball_unlocked_mousedoor) ----
        -- trap 12/section 8: click_obj is hollow (ok, nil detail) -- call it
        -- directly, grade the row on the backpack count landing.
        local pickup_diary_result = t.player.click_obj("witches_diary", 3)
        t.step("pickupDiary", pickup_diary_result == "ok" and "PASS" or "FAIL", "click_obj witches_diary -> " .. tostring(pickup_diary_result))
        t.exec("pickupDiary.await", t.inv.await, "witches_diary", 1, 10) -- trap 24
        t.exec("readDiary", t.player.inv_op, "witches_diary", 1)
        t.exec("readDiary-drain", t.chat.drain, { stop_at = "none" })
        t.ticks(2) -- trap 25
        t.expect("quest.stage.read_diary_after_door", t.quest.expect_stage("read_diary_after_door"))

        -- ---- Out the back door, search the fountain for the shed key ----
        t.exec("exitBackDoor", t.player.click_loc, "witchbackdoor", 1)
        -- click_loc's own walk_near timed out reaching the fountain through
        -- the garden on its own (run 5: FAIL settle_after_click, 43 ticks,
        -- no press ever landed) -- goto the guide's own WorldPoint for this
        -- step first, same idiom as every other approach in this file.
        t.exec("goto-searchFountain", t.player.goto_tile, 2910, 3471, 0)
        t.exec("searchFountain", t.player.click_loc, "witchfountain", 2)
        t.exec("searchFountain-dismiss", t.chat.continue_, true) -- trap 22
        t.exec("searchFountain.await", t.inv.await, "witches_shedkey", 1, 10) -- trap 24

        -- ---- Use the shed key on the shed door -- this is what spawns the
        -- experiment (quest_ball_locs.rs2's [oplocu,witchsheddoor]). The
        -- door (local 54,7 in maps/m45_54.jl2 -> world 2934,3463,0) sits in
        -- a west-facing wall; the loc's own square shares 2934, so stand
        -- one tile west of it (garden side) before using the key, or the
        -- click_minimenu retry walks the LONG way round to the shed's own
        -- east side and answers "I can't reach that!" from there (run 4).
        t.exec("goto-enterShed", t.player.goto_tile, 2933, 3463, 0)
        local shed_door = t.player.by_symbol("loc", "witchsheddoor")
        t.exec("enterShed", t.player.use_on, "witches_shedkey", shed_door)
        -- entering the shed does not itself change ballquest -- it stays at
        -- read_diary_after_door until the experiment is defeated.
        t.expect("quest.stage.read_diary_after_door.still", t.quest.expect_stage("read_diary_after_door"))

        -- ---- Fight all four forms of the shapeshifter (one live uid that
        -- changetypes through glob -> spider -> bear -> wolf on each kill,
        -- quest_ball_locs.rs2's [ai_queue3,...] chain) ----
        t.exec("killWitchsExperiment", t.player.attack, "shapeshifterglob", 2)
        t.exec("killWitchsExperiment.dead", t.npc.await_dead_engaged, 200, 12)
        t.expect("quest.stage.defeated_experiment", t.quest.expect_stage("defeated_experiment"))

        -- ---- Pick up the ball off the crate ----
        local pickup_ball_result = t.player.click_obj("ball", 3) -- trap 12: click_obj is hollow
        t.step("pickupBall", pickup_ball_result == "ok" and "PASS" or "FAIL", "click_obj ball -> " .. tostring(pickup_ball_result))
        t.exec("pickupBall.await", t.inv.await, "ball", 1, 10) -- trap 24

        -- ---- Leave the shed, return the ball to the Boy ----
        t.exec("leaveShed", t.player.click_loc, "witchsheddoor", 1)
        t.exec("goto-returnToBoy", t.player.goto_tile, 2928, 3456, 0)

        -- The Boy is an ordinary wandering npc (no override in
        -- witches_house.npc, so the pack default wander mode applies) --
        -- by the time the long fight (87 ticks in run 6) and everything
        -- before it has played out, he is no longer necessarily on his own
        -- spawn tile. Run 6's goto to the fixed spawn tile landed the
        -- player behind a fence with the Boy visible but unreachable
        -- ("I can't reach that!" in the chat log, 58-returnToBoy.png) --
        -- talk_to's own trap-26 "no dialogue in 5 tick(s)" allowance graded
        -- that PASS even though the press was really refused. Read his
        -- LIVE tile and goto there directly before talking to him.
        local boy_result, boy_row = t.npc.nearest("ballboy", 20)
        t.check("returnToBoy.locate", boy_result == "ok",
            "npc.nearest(ballboy) -> " .. tostring(boy_result)
                .. (boy_row and (" at " .. tostring(boy_row.x) .. "," .. tostring(boy_row.z) .. "," .. tostring(boy_row.level)) or ""))
        if boy_result == "ok" and boy_row then
            t.exec("goto-returnToBoy2", t.player.goto_tile, boy_row.x, boy_row.z, boy_row.level or 0)
        end

        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("hp.snapshot", snapshot_result == "ok", "hitpoints xp=" .. tostring(snapshot and snapshot.hitpoints and snapshot.hitpoints.experience))

        t.exec("returnToBoy", t.player.talk_to, "ballboy", 1)
        t.exec("returnToBoy-dialog", t.chat.play, {
            "player:Hi, I have got your ball back.",
            "mesbox:You give the ball back.",
            "npc:Thank you so much!",
        })
        t.ticks(3) -- section 8: completion is asynchronous, the queued ball_quest_complete lands behind the dialogue

        t.quest.expect_complete()
        t.exec("reward.hitpoints_xp", t.skill.expect_gain, "hitpoints", 6325, snapshot)

        t.finish(0)
        return
    end,
}
