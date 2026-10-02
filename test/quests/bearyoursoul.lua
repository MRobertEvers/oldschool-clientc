-- Bear Your Soul (miniquest, 0 quest points): shelf book, Aretha, dig, Key Master.
return {
    id = "bearyoursoul",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give spade 1",
        -- Quest Helper BearYourSoul.java:80-82,126-130: "Dusty key, or another way to get into
        -- the deep Taverley Dungeon" is an item requirement (brought along).
        "::give dusty_key 1",
        -- survival only: the dungeon's aggressive npcs kill a 10-hitpoint account on the walk
        "::setlevel hitpoints 80",
        "::setlevel defence 75",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb5078_arceuus_soulbearer_story",
            constants = { not_started = 0, aretha = 1, repair = 2, complete = 3 },
            row = "miniquest_bearyoursoul",
            display = "Bear Your Soul",
            points = 0,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- findSoulJourneyAndRead: ::bysshelf only stands the player beside whichever shelf holds the book now.
        t.cheat("::bysshelf")
        t.ticks(8)
        local lines_result, lines = t.msg.last(2)
        local said = "?"
        if lines_result == "ok" and type(lines) == "table" then
            for i = 1, #lines do said = said .. " | " .. tostring(lines[i].text) end
        end
        t.check("findSoulJourneyAndRead.shelf", lines_result == "ok", said)
        local tile_result, tile = t.world.tile()
        t.check("findSoulJourneyAndRead.pos", tile_result == "ok", tostring(tile.x) .. "," .. tostring(tile.z) .. "," .. tostring(tile.level))
        t.check("findSoulJourneyAndRead.shelf-at", said:find("bysshelf 1617,3801,0", 1, true) ~= nil, said)
        t.exec("findSoulJourneyAndRead.search", t.player.click_loc, "archeuus_library_bookcase_g1_end_right_door_03_turquoise", 1, { at = { 1617, 3801 } })
        t.exec("findSoulJourneyAndRead.book", t.inv.await, "arceuus_library_soulbearerbook", 1, 12)
        t.exec("findSoulJourneyAndRead.read", t.player.inv_op, "arceuus_library_soulbearerbook", 1)
        t.exec("findSoulJourneyAndRead.read-pages", t.chat.play, {
            "mesbox:The Journey of Souls",
            "mesbox:An artefact named the Soul Bearer",
            "mesbox:The book referred to an artefact",
        })
        t.expect("quest.stage.aretha", t.quest.expect_stage("aretha"))

        t.exec("goto-talkToAretha", t.player.goto_tile, 1814, 3851, 0)
        t.exec("talkToAretha", t.player.talk_to, "arceuus_soulguardian", 1)
        t.exec("talkToAretha-dialog", t.chat.play, {
            "player:I've been reading your book",
            "npc:did you come to ask me where the soul bearer",
            "choose:Yes.",
            "player:Yes.",
            "npc:We buried the artefact",
        })
        t.expect("quest.stage.repair", t.quest.expect_stage("repair"))

        t.exec("goto-arceuusChurchDig", t.player.goto_tile, 1699, 3794, 0)
        t.exec("arceuusChurchDig", t.player.inv_op, "spade", 1)
        t.exec("arceuusChurchDig-pages", t.chat.play, { "mesbox:You dig up a damaged Soul Bearer" })
        t.exec("arceuusChurchDig-pack", t.inv.await, "arceuus_soulbearer_damaged", 1, 8)

        t.exec("goto-goToTaverleyDungeon", t.player.goto_tile, 2884, 3397, 0)
        t.exec("goToTaverleyDungeon", t.player.click_loc, "ladder_outside_to_underground", 1)
        t.ticks(4)
        local dr, dt = t.world.tile()
        t.check("goToTaverleyDungeon.below", dr == "ok" and dt.z > 4000, tostring(dt.x) .. "," .. tostring(dt.z))
        -- Seam bearyoursoul_dusty_key_route: on foot from the ladder to the gate's entering
        -- (east) side, the dusty key on deepdungeondoor, then on foot to the cave mouth.
        local function hop(name, x, z, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks)
            if wr ~= "ok" then
                t.ticks(2)
                wr, wd = t.player.walk_to(x, z, ticks)
            end
            local _, at = t.world.tile()
            t.check(name, wr == "ok", "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. ") at " .. tostring(at.x) .. "," .. tostring(at.z))
        end
        hop("walk-cauldrondoor", 2888, 9831, 60)
        for i = 1, 3 do
            t.exec("cauldrondoor" .. i, t.player.click_loc, "cauldrondoor", 1, { at = { 2889, 9831 } })
            t.ticks(3)
        end
        hop("walk-metalgate", 2897, 9831, 30)
        t.exec("metalgate", t.player.click_loc, "metalgateclosedl", 1, { at = { 2898, 9831 } })
        t.ticks(2)
        hop("walk-railing", 2933, 9813, 80)
        hop("walk-east", 2951, 9790, 80)
        hop("walk-south", 2949, 9774, 80)
        hop("walk-gate-east", 2925, 9803, 80)
        local gate = t.player.by_symbol("loc", "deepdungeondoor")
        t.exec("useDustyKeyOnGate", t.player.use_on, "dusty_key", gate)
        t.ticks(3)
        t.exec("useDustyKeyOnGate.msg", t.msg.expect, "You unlock the gate")
        t.ticks(3)
        hop("walk-in-1", 2905, 9805, 60)
        hop("walk-in-2", 2892, 9799, 60)
        hop("walk-in-3", 2877, 9813, 60)
        local cr, cd = t.player.walk_to(2875, 9846, 60)
        local _, cm = t.world.tile()
        t.check("walk-enterCaveToKeyMaster", math.abs(cm.x - 2874) <= 1 and math.abs(cm.z - 9846) <= 2, "walk_to 2875,9846 -> " .. tostring(cr) .. " at " .. tostring(cm.x) .. "," .. tostring(cm.z))
        t.exec("enterCaveToKeyMaster", t.player.click_loc, "hellhound_cave_entrance_a_01", 1)
        t.ticks(6)
        local kr, kt = t.world.tile()
        t.check("enterCaveToKeyMaster.lobby", kr == "ok", tostring(kt.x) .. "," .. tostring(kt.z))
        t.exec("goto-speakKeyMaster", t.player.goto_tile, 1310, 1251, 0)
        t.exec("speakKeyMaster", t.player.talk_to, "keeper_of_keys", 1)
        t.exec("speakKeyMaster-dialog", t.chat.play, {
            "npc:The soul bearer! You have the soul bearer",
            "mesbox:The Key Master repairs your soul bearer",
            "npc:The voices say you must use it wisely",
        })
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.quest.expect_complete()
        local rr, rc = t.inv.count("arceuus_soulbearer")
        t.check("reward.arceuus_soulbearer", rr == "ok" and rc == 1, "arceuus_soulbearer count " .. tostring(rc))
        local dr2, dc = t.inv.count("arceuus_soulbearer_damaged")
        t.check("reward.damaged-consumed", dr2 == "ok" and dc == 0, "damaged count " .. tostring(dc))
        t.finish(0)
    end,
}
