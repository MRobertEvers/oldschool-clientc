-- Temple of Ikov (Quest Helper helpers/quests/templeofikov/TempleOfIkov.java), the Armadyl ending:
-- Lucien -> boots of lightness -> lever piece -> bracket + mended lever -> ice arrows from the chests ->
-- trap lever (search, pull) -> Fire Warrior of Lesarkus (real ranged fight, ice arrow worn) -> Winelda
-- (20 limpwurts) -> shiny key -> secret wall -> Guardians (cleansing) -> kill Lucien in the forest.
-- Brought along (guide getItemRequirements): a throwable weapon (rune darts), 20 limpwurts, a knife and a light
-- source. Ice arrows, the boots, the lever and the key are all gathered.

return {
    id = "ikov",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel thieving 42",
        "::setlevel ranged 80",
        "::setlevel defence 60",
        "::setlevel hitpoints 99",
        "::give knife 1",
        "::give torch_lit 1",
        "::give rune_dart 100",
        "::give limpwurt_root 20",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "ikov",
            constants = {
                not_started = 0,
                started = 10,
                disabled_trap = 20,
                pulled_lever = 30,
                defeated_fire_warrior = 40,
                spoken_winelda = 50,
                paid_winelda = 60,
                helping_armadyl = 70,
                complete = 80,
            },
            row = "quest_templeofikov",
            display = "Temple of Ikov",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------- talkToLucien
        t.exec("goto-talkToLucien", t.player.goto_tile, 2573, 3323, 0)
        t.exec("talkToLucien", t.player.talk_to, "ikov_lucien1", 1)
        t.exec("talkToLucien-dialog", t.chat.play, {
            "npc:I seek a hero to go on an impo",
            "choose:I'm a mighty hero!",
            "player:I am a mighty hero!",
            "npc:I require the Staff of Armadyl",
            "npc:Take care hero! There is a dan",
            "choose:That sounds like a laugh!",
            "player:That sounds like a laugh!",
            "npc:It's not as easy as it sounds.",
            "player:I'm up for it!",
            "npc:Take this pendant. Without it ",
            "mesbox:Lucien has given you a pendant",
            "npc:I cannot stay here much longer",
            "npc:I will be in the forest north ",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local _, pendant_count = t.inv.count("ikov_pendantoflucien")
        t.check("talkToLucien-pendant", pendant_count == 1, "pendant of Lucien in the pack: " .. tostring(pendant_count))

        -- ---------------------------------------------------------------- enterDungeonForBoots
        t.exec("goto-enterDungeonForBoots", t.player.goto_tile, 2677, 3404, 0)
        t.exec("enterDungeonForBoots", t.player.click_loc, "ladder_cellar", 1)
        t.ticks(3)

        -- ---------------------------------------------------------------- goDownToBoots
        t.exec("goto-goDownToBoots", t.player.goto_tile, 2650, 9805, 0)
        t.exec("goDownToBoots", t.player.click_loc, "ikov_darkstairsdown", 1)
        t.ticks(3)

        -- ---------------------------------------------------------------- getBoots
        t.exec("goto-getBoots", t.player.goto_tile, 2654, 9766, 0)
        -- the floor pair is the worn variant; the pickup converts it to the carried one, so click_obj's own
        -- wait (for the floor variant's count) never sees a rise -- read the carried pair instead
        local boots_click_result, boots_click_detail = t.player.click_obj("ikov_bootsoflightnessworn", 3)
        local boots_await_result = t.inv.await("ikov_bootsoflightness", 1, 10)
        t.check("getBoots", boots_await_result == "ok", "click_obj " .. tostring(boots_click_result) .. " (" .. tostring(boots_click_detail) .. "); carried boots await " .. tostring(boots_await_result))
        t.exec("getBoots-pack", t.inv.expect_has, "ikov_bootsoflightness", 1)
        local equip_boots_result, equip_boots_detail = t.player._inv_dispatch("ikov_bootsoflightness", 2, "equip")
        t.check("getBoots-wear", equip_boots_result == "ok", tostring(equip_boots_result) .. " " .. tostring(equip_boots_detail))
        t.ticks(4)
        t.exec("getBoots-worn", t.inv.expect_absent, "ikov_bootsoflightness")

        -- ---------------------------------------------------------------- goUpFromBoots
        t.exec("goto-goUpFromBoots", t.player.goto_tile, 2638, 9764, 0)
        t.exec("goUpFromBoots", t.player.click_loc, "ikov_darkstairs", 1)
        t.ticks(3)

        -- ---------------------------------------------------------------- pickUpLever
        t.exec("wear-pendantOfLucien", t.player.equip, "ikov_pendantoflucien")
        t.exec("goto-northGate", t.player.goto_tile, 2661, 9814, 0)
        t.exec("northGate", t.player.click_loc, "ikov_dooroffearl", 1)
        t.ticks(3)
        t.player.walk_to(2652, 9828, 20)
        t.ticks(4)
        t.player.walk_to(2647, 9828, 14)
        t.ticks(6)
        local bridge_x, bridge_z
        do
            local _, tile = t.world.tile()
            bridge_x, bridge_z = tile.x, tile.z
        end
        t.check("pickUpLever-bridge", bridge_x <= 2647, "crossed the lava bridge wearing the boots, now at " .. bridge_x .. "," .. bridge_z)
        -- seam29: the lever room is behind a closed castle double door (castledoubledoorl/r at
        -- 2645,9828/9829; LostCity maps/m41_153.jm2 has its doors at the same tiles) -- open it and
        -- WALK in; a goto past it skipped the door and left the eastbound walk facing it shut.
        t.exec("openLeverRoomDoor", t.player.click_loc, "castledoubledoorl", 1)
        t.player.walk_to(2640, 9822, 20)
        do
            local _, tile = t.world.tile()
            t.check("walk-pickUpLever", tile.x <= 2644, "walked through the door into the lever room: " .. tile.x .. "," .. tile.z)
        end
        t.exec("pickUpLever", t.player.click_obj, "ikov_lever", 3)
        t.exec("pickUpLever-pack", t.inv.expect_has, "ikov_lever", 1)

        -- ---------------------------------------------------------------- leave the chamber, useLeverOnHole, pullLever
        t.player.walk_to(2647, 9828, 20)
        do
            local door_result = t.world.loc_near("castledoubledoorl", 3)
            if door_result == "ok" then
                t.exec("reopenLeverRoomDoor", t.player.click_loc, "castledoubledoorl", 1)
                t.player.walk_to(2647, 9828, 20)
            end
        end
        t.player.walk_to(2652, 9828, 20)
        t.ticks(2)
        local recross_x, recross_z
        do
            local _, tile = t.world.tile()
            recross_x, recross_z = tile.x, tile.z
        end
        local _, recent_lines = t.msg.last(3)
        local recent_text = ""
        for i = 1, #recent_lines do recent_text = recent_text .. " | " .. recent_lines[i].text end
        t.check("pickUpLever-recross", recross_x >= 2651, "crossed back east over the bridge carrying the lever, now at " .. recross_x .. "," .. recross_z .. recent_text)
        t.exec("goto-gateInside", t.player.goto_tile, 2661, 9816, 0)
        t.exec("exitNorthGate", t.player.click_loc, "ikov_dooroffearr", 1)
        t.ticks(3)
        t.exec("goto-useLeverOnHole", t.player.goto_tile, 2671, 9805, 0)
        t.exec("useLeverOnHole", t.player.use_on, "ikov_lever", t.player.by_symbol("loc", "ikov_leverbracket"))
        t.exec("useLeverOnHole-dialog", t.chat.play, { "mesbox:You fit the lever into the bracket." })
        t.exec("useLeverOnHole-gone", t.inv.expect_absent, "ikov_lever")
        t.exec("pullLever", t.player.click_loc, "ikov_mendedlever", 1)
        t.exec("pullLever-dialog", t.chat.play, { "mesbox:You hear the clunking of some hidden machinery." })

        -- ---------------------------------------------------------------- enterArrowRoom, collectArrows
        t.exec("goto-enterArrowRoom", t.player.goto_tile, 2662, 9804, 0)
        t.exec("enterArrowRoom", t.player.click_loc, "ikov_mendedleverdoorl", 1)
        t.ticks(3)
        local arrow_room_z
        do
            local _, tile = t.world.tile()
            arrow_room_z = tile.z
        end
        t.check("enterArrowRoom-through", arrow_room_z <= 9802, "south of the gate now, z=" .. arrow_room_z)

        -- ---------------------------------------------------------------- collectArrows
        -- The six ice-area chests are dead-end alcoves of the arrow room (m42_153.jl2 loc 103); only ONE
        -- holds arrows at a time (ikov_dungeon.rs2 ikov_chest_pay), so open and search until a search pays.
        -- One ice arrow is enough with a throwable weapon (guide hasArrowsWithThrowableWeapon).
        local chest_stands = {
            { 2745, 9822, 2745, 9821 },
            { 2739, 9835, 2738, 9835 },
            { 2746, 9848, 2747, 9848 },
            { 2719, 9839, 2719, 9838 },
            { 2710, 9849, 2710, 9850 },
            { 2729, 9849, 2729, 9850 },
        }
        local arrows_found = 0
        for chest_index = 1, #chest_stands do
            local stand = chest_stands[chest_index]
            t.exec("goto-collectArrows-" .. chest_index, t.player.goto_tile, stand[1], stand[2], 0)
            t.exec("collectArrows-open-" .. chest_index, t.player.click_loc, "ikov_chestclosed", 1, { at = { stand[3], stand[4] } })
            t.ticks(2)
            t.exec("collectArrows-search-" .. chest_index, t.player.click_loc, "ikov_chestopen", 1, { at = { stand[3], stand[4] } })
            t.ticks(3)
            if t.chat.kind() ~= "none" then
                t.exec("collectArrows-found-" .. chest_index, t.chat.play, { "*" })
            end
            t.ticks(2)
            local _, arrow_total = t.inv.count("ice_arrow")
            arrows_found = arrow_total
            if arrow_total > 0 then
                break
            end
        end
        t.check("collectArrows", arrows_found > 0, "ice arrows in the pack after searching the chests: " .. tostring(arrows_found))

        -- ---------------------------------------------------------------- returnToMainRoom
        t.exec("goto-returnToMainRoom", t.player.goto_tile, 2662, 9801, 0)
        t.exec("returnToMainRoom", t.player.click_loc, "ikov_mendedleverdoorl", 1)
        t.ticks(3)
        do
            local _, tile = t.world.tile()
            t.check("returnToMainRoom-through", tile.z >= 9803, "north of the south gate again, z=" .. tile.z)
        end

        -- ---------------------------------------------------------------- goSearchThievingLever
        -- Food is not a quest item: it is here (past the lava bridge, whose weight test it would fail) for the
        -- Fire Warrior, the demons and Lucien.
        t.check("food", t.cheat("::give lobster 20") == "ok", "gave 20 lobster for the fights ahead")
        t.exec("goto-northGate-again", t.player.goto_tile, 2661, 9814, 0)
        t.exec("northGate-again", t.player.click_loc, "ikov_dooroffearl", 1)
        t.ticks(3)
        local lever_route = { { 2666, 9824 }, { 2666, 9836 }, { 2665, 9847 }, { 2665, 9853 } }
        for hop = 1, #lever_route do
            t.player.walk_to(lever_route[hop][1], lever_route[hop][2], 40)
        end
        do
            local _, tile = t.world.tile()
            t.check("walk-goSearchThievingLever", math.abs(tile.x - 2665) <= 3 and tile.z >= 9850,
                "walked through the north room to the trap lever: " .. tile.x .. "," .. tile.z)
        end
        t.exec("goSearchThievingLever", t.player.click_loc, "ikov_traplever", 2)
        t.exec("goSearchThievingLever-dialog", t.chat.play, { "mesbox:You find a trap on the lever! You disable" })
        t.expect("quest.stage.disabled_trap", t.quest.expect_stage("disabled_trap"))
        t.exec("goPullThievingLever", t.player.click_loc, "ikov_traplever", 1)
        t.ticks(4)
        t.expect("quest.stage.pulled_lever", t.quest.expect_stage("pulled_lever"))

        -- ---------------------------------------------------------------- tryToEnterWitchRoom, fightLes
        local door_route = { { 2664, 9847 }, { 2658, 9845 }, { 2651, 9844 }, { 2648, 9849 }, { 2648, 9857 } }
        for hop = 1, #door_route do
            t.player.walk_to(door_route[hop][1], door_route[hop][2], 40)
        end
        t.exec("trapLeverDoor", t.player.click_loc, "ikov_trapleverdoor", 1)
        t.ticks(3)
        t.player.walk_to(2646, 9865, 30)
        t.player.walk_to(2646, 9869, 20)
        t.exec("wield-throwableWeapon", t.player.equip, "rune_dart")
        t.exec("wear-iceArrows", t.player.equip, "ice_arrow")
        t.exec("tryToEnterWitchRoom", t.player.click_loc, "ikov_firewarriordoor", 1)
        t.ticks(12)
        t.exec("fightLes-talkRefused", t.npc.by_symbol, "ikov_firewarrior")
        t.exec("fightLes", t.player.attack, "ikov_firewarrior", 2, 30)
        t.exec("fightLes-dead", t.npc.await_dead_engaged, 400, 12, { eat = { item = "lobster", below = 45 } })
        t.ticks(12)
        t.expect("quest.stage.defeated_fire_warrior", t.quest.expect_stage("defeated_fire_warrior"))

        -- ---------------------------------------------------------------- enterLesDoor, giveWineldaLimps
        t.exec("enterLesDoor", t.player.click_loc, "ikov_firewarriordoor", 1)
        t.ticks(3)
        t.player.walk_to(2655, 9875, 30)
        t.exec("giveWineldaLimps-talk", t.player.talk_to, "ikov_winelda", 1)
        t.exec("giveWineldaLimps-dialog", t.chat.play, {
            "npc:Hehe! We see you're in a pickle",
            "npc:Wants to be getting over",
            "choose:Yes I do!",
            "player:Yes I do!",
            "npc:I'm knowing some magic",
            "npc:Don't tell them!",
            "player:If you're such a great witch",
            "npc:See! They pester Winelda!",
            "player:I can do something for you!",
            "npc:Good! Don't pester!",
            "npc:Get Winelda 20 limpwurt",
            "npc:Then we shows them some magic",
        })
        t.ticks(2)
        t.expect("quest.stage.spoken_winelda", t.quest.expect_stage("spoken_winelda"))
        t.exec("giveWineldaLimps", t.player.talk_to, "ikov_winelda", 1)
        t.exec("giveWineldaLimps-pay", t.chat.play, {
            "player:I've got you the limpwurt roots",
            "npc:Good! Good! My potion",
            "npc:Now we shows them ours magic",
        })
        t.ticks(20)
        t.expect("quest.stage.paid_winelda", t.quest.expect_stage("paid_winelda"))
        t.exec("giveWineldaLimps-limpwurtsGone", t.inv.expect_absent, "limpwurt_root")
        do
            local _, tile = t.world.tile()
            t.check("winelda-teleport", tile.z >= 9872 and tile.x >= 2660, "Winelda's magic carried us over the lava: " .. tile.x .. "," .. tile.z)
        end

        -- ---------------------------------------------------------------- pickUpKey
        local key_route = { { 2660, 9882 }, { 2649, 9891 }, { 2639, 9891 }, { 2632, 9888 }, { 2631, 9879 }, { 2631, 9869 }, { 2630, 9860 } }
        for hop = 1, #key_route do
            t.player.walk_to(key_route[hop][1], key_route[hop][2], 40)
        end
        t.player.walk_to(2629, 9859, 20)
        t.exec("pickUpKey", t.player.click_obj, "ikov_shinykey", 3)
        t.exec("pickUpKey-pack", t.inv.expect_has, "ikov_shinykey", 1)

        -- ---------------------------------------------------------------- pushWall
        for hop = #key_route - 1, 1, -1 do
            t.player.walk_to(key_route[hop][1], key_route[hop][2], 40)
        end
        t.player.walk_to(2643, 9891, 20)
        t.exec("pushWall", t.player.click_loc, "secretdoor2", 1)
        t.ticks(3)
        do
            local _, tile = t.world.tile()
            t.check("pushWall-through", tile.z >= 9893, "through the secret door into the guardians' hall: " .. tile.x .. "," .. tile.z)
        end

        -- ---------------------------------------------------------------- makeChoice (help the Guardians)
        t.exec("makeChoice-removePendant", t.player.unequip, "ikov_pendantoflucien")
        t.exec("makeChoice-talk", t.player.talk_to, "ikov_guardianmale", 1)
        t.exec("makeChoice-dialog", t.chat.play, {
            "npc:Thou hast ventured deep",
            "choose:I seek the Staff of Armadyl.",
            "player:I seek the Staff of Armadyl.",
            "npc:We are the guardians of the staff",
            "choose:Lucien will give me a grand reward for it!",
            "player:Lucien will give me a grand reward",
            "npc:Thou art working for that spawn",
            "choose:You're right, it's time for my yearly bath.",
            "player:You're right, it's time",
            "mesbox:The guardian splashes holy water",
            "npc:You have been cleansed!",
            "npc:Lucien must not get hold",
            "npc:Hast thou come across",
            "choose:Ok! I'll help!",
            "player:Ok! I'll help!",
            "npc:So he is close by?",
            "player:Yes!",
            "npc:He must be gaining in power",
            "mesbox:The guardian has given you a pendant",
        })
        t.ticks(2)
        t.expect("quest.stage.helping_armadyl", t.quest.expect_stage("helping_armadyl"))
        t.exec("makeChoice-pendantOfArmadyl", t.inv.expect_has, "ikov_pendantofarmardyl", 1)

        -- ---------------------------------------------------------------- killLucien
        t.exec("killLucien-wear", t.player.equip, "ikov_pendantofarmardyl")
        t.exec("goto-killLucien", t.player.goto_tile, 3120, 3485, 0)
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.step("killLucien-xpBefore", xp_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the kill -> " .. tostring(xp_snapshot_result))
        t.exec("killLucien", t.player.attack, "ikov_lucien2", 2, 30)
        t.npc.await_dead_engaged(400, 12, { eat = { item = "lobster", below = 45 } })
        t.ticks(4)
        t.exec("killLucien-defeated", t.chat.play, { "npc:You have defeated me for now" })
        t.ticks(10)

        -- ---------------------------------------------------------------- rewards
        -- scroll.reward_xp does not parse thousands separators ("10,500" reads 500), so read the lines
        local rewards_result, rewards = t.scroll.rewards()
        local rewards_text = ""
        if rewards_result == "ok" then
            for i = 1, #rewards.lines do rewards_text = rewards_text .. rewards.lines[i] .. " | " end
        end
        t.check("scroll.reward_ranged", rewards_text:find("10,500 Ranged XP", 1, true) ~= nil,
            "scroll lines: " .. rewards_text)
        t.check("scroll.reward_fletching", rewards_text:find("8,000 Fletching XP", 1, true) ~= nil,
            "scroll lines: " .. rewards_text)
        t.expect("reward.fletching_xp", t.skill.expect_gain("fletching", 8000, xp_snapshot))
        do
            -- the kill itself earned Ranged combat xp, so the exact-delta verb cannot judge Ranged: read it
            local _, ranged_after = t.skill.read("ranged")
            local gained = ranged_after.experience - xp_snapshot.ranged.experience
            t.check("reward.ranged_xp", gained >= 10500 or gained >= 105000,
                "ranged xp moved by " .. tostring(gained) .. " over the kill and the 10,500 quest reward")
        end
        t.quest.expect_complete()
        t.finish(0)
    end,
}
