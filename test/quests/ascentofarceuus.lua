-- The Ascent of Arceuus. Guide: Quest Helper theascentofarceuus (24 steps, stage 0..13 -> 14).
-- Prerequisites by cheat: X Marks the Spot, Client of Kourend, Hunter 12 (guide requirements).
-- Combat kit (guide: "Combat gear"): a rune scimitar worn in setup, sharks after it.
-- Parity notes: docs/quests/ladders/ascentofarceuus.notes.md.
return {
    id = "ascentofarceuus",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_xmarksthespot",
        "::complete quest_clientofkourend",
        "::setlevel hunter 12",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 40",
        "::setlevel hitpoints 40",
        "::give rune_scimitar 1",
        "::wield rune_scimitar",
        "::give shark 8",
    },

    run = function(t)
        local r0, d0 = t.quest.bind({
            varp = "varb7856_arcquest",
            constants = {
                not_started = 0, andrews = 1, mori2 = 2, souls = 3, souls2 = 4, arceuus = 5,
                kaal = 7, grave = 8, track = 9, soul = 10, kaal2 = 11, rocks = 12, finish = 13,
                complete = 14,
            },
            row = "quest_ascentofarceuus",
            display = "The Ascent of Arceuus",
            points = 1,
        })
        t.step("quest.bind", r0 == "ok" and "PASS" or "FAIL", d0)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local hp_low = 40
        local fight_ticks = {}
        local _, hp_start_t = t.skill.read("hitpoints")
        local hp_start = hp_start_t and (hp_start_t.current or hp_start_t.level or hp_start_t.boosted)
        t.check("fight.staged", hp_start == 40, "hitpoints staged deliberately at level 40 (::setlevel hitpoints 40), read back " .. tostring(hp_start) .. "/40, sharks staged 8")
        local eaten_start = select(2, t.inv.count("shark"))

        -- Travel to Great Kourend by ship (door rule: no goto onto the island): Port Sarim -> Veos
        -- "Can you take me somewhere?" / "Port Piscarilius" (veos_ferry.rs2:72-110) ->
        -- the Port Piscarilius dock, then overland (reach.py 1824,3690 -> 1698,3743 REACH closed-doors).
        t.exec("goto-veosSarim", t.player.goto_tile, 3054, 3246, 0)
        t.exec("talkToVeos", t.player.talk_to, "veos_sarim", 1)
        -- Client of Kourend is done, so Veos opens his standard Sarim menu (veos_ferry.rs2 [label,veos_sarim_standard_talk])
        t.exec("talkToVeos-sail", t.chat.play, {
            "player:Hello Veos.",
            "npc:Hello there. What can I do for you?",
            "choose:Can you take me somewhere?",
            "player:Can you take me somewhere?",
            "npc:Where would you like to go?",
            "choose:I'd like to travel to Port Piscarilius, please.",
            "player:I'd like to travel to Port Piscarilius, please.",
            "npc:As you wish",
        })
        t.ticks(4)
        local _, dock_arrival = t.world.tile()
        t.check("talkToVeos-landed", dock_arrival ~= nil and dock_arrival.x >= 1800 and dock_arrival.x < 1850,
            "landed at the Piscarilius dock at " .. tostring(dock_arrival and (dock_arrival.x .. "," .. dock_arrival.z)))

        -- 1.1 talkToMori (s0)
        t.exec("goto-mori", t.player.goto_tile, 1698, 3743, 0)
        t.exec("talkToMori", t.player.talk_to, "arcquest_mori_visible", 1)
        t.exec("talkToMori-dialog", t.chat.play, {
            "player:What happened here?",
            "npc:She's... she's dead... Thana..",
            "choose:What can I do to help?",
            "player:What can I do to help?",
            "npc:I... I don't know...",
            "player:Right. Let's start with what a",
            "npc:Okay... Thank you...",
            "npc:I'm Mori... I was going to the",
            "npc:...she just died!",
            "player:Just like that? Was she ill?",
            "npc:The people of Arceuus don't ge",
            "choose:The Ascent of Arceuus?",
            "player:The Ascent of Arceuus?",
            "npc:Yes... A long time ago, our le",
            "npc:The events of that day became ",
            "npc:But something's gone wrong... ",
            "player:We should let someone know abo",
            "npc:You... You're right. We should",
            "choose:Yes.",
            "player:Of course I'll help.",
            "npc:Thank you... Could you head to",
            "player:Will do. I'll be back soon.",
        })
        t.expect("quest.stage.andrews", t.quest.expect_stage("andrews"))

        -- 1.2 goUpToAndrews (s1): the castle staircase fai_varrock_stairs_taller_new_fix (1615,3680, a
        -- 3x2 footprint 1615-1617,3680-3681) is pressed from its open east side 1618,3681. No maplink row
        -- names it, so ladders.rs2 [proc,climb] moves the player one plane up on the same tile.
        t.exec("goto-stairs-andrews", t.player.goto_tile, 1618, 3681, 0)
        t.exec("goUpToAndrews", t.player.climb, { loc = "fai_varrock_stairs_taller_new_fix",
            at = { 1615, 3680, 0 }, src = { 1618, 3681 }, dest = { 1618, 3681, 1 }, slack = 1 })
        t.ticks(2)
        -- 1.3 talkToAndrews (s1)
        t.exec("goto-andrews", t.player.goto_tile, 1620, 3672, 1)
        t.exec("talkToAndrews", t.player.talk_to, "zeah_head_councillor_vis", 1)
        t.exec("talkToAndrews-dialog", t.chat.play, {
            "player:Hi there.",
            "npc:Good day to you.",
            "choose:There's been a death in Arceuus.",
            "player:There's been a death in Arceuu",
            "npc:A death? Well that doesn't see",
            "player:I've seen it with my own eyes.",
            "npc:Well I don't really know what ",
            "player:So you'll do nothing?",
            "npc:If I could do something, I wou",
            "player:I see. I suppose I'd better te",
        })
        t.expect("quest.stage.mori2", t.quest.expect_stage("mori2"))

        -- 1.4 returnToMori (s2): back down the castle staircase (fai_varrock_stairs_top 1615,3680,1)
        -- (the press walks to the flight's west side 1614,3681,1 and lands one plane down on that tile)
        t.exec("goto-stairs-andrews-down", t.player.goto_tile, 1614, 3681, 1)
        t.exec("leaveAndrews", t.player.climb, { loc = "fai_varrock_stairs_top",
            at = { 1615, 3680, 1 }, src = { 1614, 3681 }, dest = { 1614, 3681, 0 }, slack = 1 })
        t.ticks(2)
        t.exec("goto-mori2", t.player.goto_tile, 1698, 3743, 0)
        t.exec("returnToMori", t.player.talk_to, "arcquest_mori_visible", 1)
        t.exec("returnToMori-dialog", t.chat.play, {
            "player:Hello again.",
            "npc:Hello. Have you spoken to Coun",
            "player:I have. The council won't be h",
            "npc:Oh... I shouldn't be surprised",
            "choose:What should we do now?",
            "player:What should we do now?",
            "npc:Well... if the Council won't h",
            "player:Good idea. Where can I find hi",
            "npc:He'll be in the Tower of ",
        })
        t.expect("quest.stage.souls", t.quest.expect_stage("souls"))

        -- 1.5 enterTowerOfMagic (s3)
        t.exec("goto-towerdoor", t.player.goto_tile, 1597, 3820, 0)
        t.exec("enterTowerOfMagic", t.player.click_loc, "arcquest_tower_door_right", 1)
        t.exec("enterTowerOfMagic-dialog", t.chat.play, {
            "player:I need to speak to Lord Arceuu",
            "npc:We're currently facing an issu",
            "player:But it's important, there's be",
            "npc:A death? This isn't good. The ",
            "choose:Yes.",
        })
        t.expect("quest.stage.souls2", t.quest.expect_stage("souls2"))
        local rt, tile = t.world.tile()
        t.check("tower.instance_tile", rt == "ok", tile and (tile.x .. "," .. tile.z .. "," .. tile.level) or tostring(tile))

        -- 1.6 killTormentedSouls (s4): five real fights in the player's own copy
        local souls = { "arcquest_ghost1", "arcquest_ghost1", "arcquest_ghost1", "arcquest_ghost2", "arcquest_ghost2" }
        for i = 1, 5 do
            t.exec("killTormentedSouls-" .. i .. ".present", t.npc.await_present, souls[i], 30, 20)
            t.exec("killTormentedSouls-" .. i, t.player.attack, souls[i], 2, 10)
            local _, det = t.exec("killTormentedSouls-" .. i .. ".dead", t.npc.await_dead_engaged, 120, 12, { eat = { item = "shark", below = 20 } })
            local low = tonumber(tostring(det):match("lowest hp (%d+)/"))
            if low and low < hp_low then hp_low = low end
            fight_ticks[#fight_ticks + 1] = tostring(tostring(det):match("dead after (%d+) tick"))
            local _, sk = t.inv.count("shark")
            t.check("killTormentedSouls-" .. i .. ".margin", low ~= nil and low >= 10 and (sk or 0) >= 1,
                "lowest hp " .. tostring(low) .. "/40 (>= 10, a quarter) with " .. tostring(sk) .. " shark(s) left")
        end
        t.expect("quest.stage.arceuus", t.quest.expect_stage("arceuus"))

        -- 1.7 goUpstairsTowerOfMagic (s5)
        t.exec("goUpstairsTowerOfMagic", t.player.click_loc, "arcquest_stairs_lower_left", 1)
        t.ticks(3)
        local rt2, tile2 = t.world.tile()
        t.check("tower.upstairs_tile", rt2 == "ok" and tile2.level == 1, rt2 == "ok" and (tile2.x .. "," .. tile2.z .. "," .. tile2.level) or tostring(tile2))
        -- 1.8 talkToArceuus (s5): Asteros stands at 1579,3818; Trobin is in a trance
        t.exec("goto-asteros", t.player.goto_tile, 1580, 3818, 1)
        t.exec("talkToArceuus", t.player.talk_to, "asteros_arceuus_vis", 1) -- guide names Trobin; content: Asteros (parity notes)
        t.exec("talkToArceuus-dialog", t.chat.drain, { max_pages = 40 })
        t.ticks(3)
        t.expect("quest.stage.kaal", t.quest.expect_stage("kaal"))

        -- Out of the tower: the upper stairs (arcquest_stairs_upper_left 1581,3820,1) telejump to
        -- ^aoa_tower_inside 1587,3821,0, the foot of the flight east of arcquest_stairs_lower_*
        -- (ascentofarceuus_locs.rs2:86-89, ascentofarceuus.constant), then the door from inside
        -- telejumps to ^aoa_door_outside 1597,3820 (ascentofarceuus_locs.rs2:24-27).
        t.exec("leaveTowerF1", t.player.climb, { loc = "arcquest_stairs_upper_left",
            at = { 1581, 3820, 1 }, dest = { 1587, 3821, 0 }, slack = 1 })
        t.ticks(2)
        t.exec("goto-towerdoor-inside", t.player.goto_tile, 1595, 3820, 0)
        t.exec("leaveTower", t.player.cross_gate, { loc = "arcquest_tower_door_right", at = { 1596, 3820, 0 },
            near = { 1595, 3820 }, far_ok = function(tile) return tile.x >= 1597 end,
            far_desc = "outside the Tower of Magic, x >= 1597" })

        -- 1.9 enterKaruulm (s7)
        t.exec("goto-elevator", t.player.goto_tile, 1311, 3809, 0)
        t.exec("enterKaruulm", t.player.click_loc, "brimstone_elevator_central_tile", 1)
        t.ticks(3)
        local rk, tk = t.world.tile()
        t.check("karuulm.tile", rk == "ok", rk == "ok" and (tk.x .. "," .. tk.z .. "," .. tk.level) or tostring(tk))
        -- 1.10 talkToKaal (s7)
        t.exec("goto-kaal", t.player.goto_tile, 1311, 10208, 0)
        t.exec("talkToKaal", t.player.talk_to, "tasakaal_ket", 1)
        t.exec("talkToKaal-dialog", t.chat.drain, { max_pages = 80 })
        t.ticks(3)
        t.expect("quest.stage.grave", t.quest.expect_stage("grave"))

        -- 1.11 leaveKaal (s8)
        t.exec("goto-exit", t.player.goto_tile, 1312, 10188, 0)
        t.exec("leaveKaal", t.player.click_loc, "brimstone_dungeon_exit", 1)
        t.ticks(3)
        -- 1.12 inspectGrave (s8): the grave's fence is open to the north (1347-1350,3738 inside it)
        t.exec("goto-grave", t.player.goto_tile, 1348, 3738, 0)
        t.exec("inspectGrave", t.player.click_loc, "arcquest_grave", 1)
        t.ticks(2)
        t.expect("quest.stage.track", t.quest.expect_stage("track"))

        -- 1.17 inspectTrack1 (s9): the bush at 1335,3743
        t.exec("goto-t1", t.player.goto_tile, 1335, 3741, 0)
        t.exec("inspectTrack1", t.player.click_loc, "arcquest_hunting_bush", 1, { at = { 1335, 3743 } })
        -- 1.13 inspectTrack2: plant 1317,3750
        t.exec("goto-t2", t.player.goto_tile, 1317, 3748, 0)
        t.exec("inspectTrack2", t.player.click_loc, "arcquest_hunting_plant", 1, { at = { 1317, 3750 } })
        -- 1.14 inspectTrack3: plant 1305,3750, reached from the north
        t.exec("goto-t3", t.player.goto_tile, 1305, 3752, 0)
        t.exec("inspectTrack3", t.player.click_loc, "arcquest_hunting_plant", 1, { at = { 1305, 3750 } })
        -- 1.15 inspectTrack4: stump 1287,3750
        t.exec("goto-t4", t.player.goto_tile, 1289, 3749, 0)
        t.exec("inspectTrack4", t.player.click_loc, "arcquest_hunting_tree_stump", 1, { at = { 1287, 3750 } })
        -- 1.16 inspectTrack5: plant 1285,3737
        t.exec("goto-t5", t.player.goto_tile, 1285, 3736, 0)
        t.exec("inspectTrack5", t.player.click_loc, "arcquest_hunting_plant2", 1, { at = { 1285, 3737 } })
        t.ticks(2)
        -- 1.19 inspectTrack6: final plant, the Trapped Soul appears
        t.exec("goto-end", t.player.goto_tile, 1283, 3728, 0)
        t.exec("inspectTrack6", t.player.click_loc, "arcquest_hunting_end", 1)
        t.ticks(3)
        t.expect("quest.stage.soul", t.quest.expect_stage("soul"))
        -- 1.18 killTrappedSoul (s10)
        t.exec("killTrappedSoul", t.player.attack, "arcquest_soul", 2, 10)
        local _, soul_det = t.exec("killTrappedSoul.dead", t.npc.await_dead_engaged, 150, 12, { eat = { item = "shark", below = 20 } })
        do
            local low = tonumber(tostring(soul_det):match("lowest hp (%d+)/"))
            if low and low < hp_low then hp_low = low end
            fight_ticks[#fight_ticks + 1] = tostring(tostring(soul_det):match("dead after (%d+) tick"))
            local _, sk = t.inv.count("shark")
            t.check("killTrappedSoul.margin", low ~= nil and low >= 10 and (sk or 0) >= 1,
                "lowest hp " .. tostring(low) .. "/40 (>= 10, a quarter) with " .. tostring(sk) .. " shark(s) left")
        end
        t.ticks(2)
        t.expect("quest.stage.kaal2", t.quest.expect_stage("kaal2"))

        -- 1.20 enterKaruulmAgain (s11)
        t.exec("goto-elevator-2", t.player.goto_tile, 1311, 3809, 0)
        t.exec("enterKaruulmAgain", t.player.click_loc, "brimstone_elevator_central_tile", 1)
        t.ticks(3)
        -- 1.21 talkToKaalAgain
        t.exec("goto-kaal-2", t.player.goto_tile, 1311, 10208, 0)
        t.exec("talkToKaalAgain", t.player.talk_to, "tasakaal_ket", 1)
        t.exec("talkToKaalAgain-dialog", t.chat.drain, { max_pages = 60 })
        t.ticks(3)
        t.expect("quest.stage.rocks", t.quest.expect_stage("rocks"))

        -- Out of the dungeon (brimstone_dungeon_exit 1311,10185 -> ^karuulm_surface 1311,3809,
        -- karuulm.rs2:11-14), then overland to the Dark Altar.
        t.exec("goto-exit-2", t.player.goto_tile, 1312, 10188, 0)
        t.exec("leaveKaal-2", t.player.click_loc, "brimstone_dungeon_exit", 1)
        t.ticks(3)
        local rx2, tx2 = t.world.tile()
        t.check("leaveKaal-2.surface", rx2 == "ok" and tx2.z < 6400 and tx2.level == 0,
            rx2 == "ok" and (tx2.x .. "," .. tx2.z .. "," .. tx2.level) or tostring(tx2))

        -- 1.22 searchRocks (s12): the device rock is random among four (varb7865, ^aoa_rock_0..3
        -- ascentofarceuus.constant:73-76); each is searched from an open tile beside it.
        local rock_tiles = { { 1706, 3888 }, { 1713, 3892 }, { 1713, 3875 }, { 1722, 3881 } }
        local rock_stand = { { 1706, 3887 }, { 1713, 3891 }, { 1714, 3877 }, { 1721, 3881 } }
        local found = false
        for i = 1, 4 do
            if not found then
                local rx, rz = rock_tiles[i][1], rock_tiles[i][2]
                t.exec("goto-rock" .. i, t.player.goto_tile, rock_stand[i][1], rock_stand[i][2], 0)
                local sym = "arcquest_rocks_1"
                local rn = t.world.loc_near(sym, 3)
                if rn ~= "ok" then sym = "arcquest_rocks_2" end
                local rr, rd = t.player.click_loc(sym, 1, { at = { rx, rz } })
                t.ticks(2)
                local ks = t.chat.kind()
                local stage = select(2, t.quest.stage())
                t.check("searchRocks-" .. i, rr == "ok" and (stage == 12 or stage == 13), "rock " .. i .. " at " .. rx .. "," .. rz .. " via " .. sym .. ": " .. tostring(rr) .. " " .. tostring(rd) .. " chat=" .. tostring(ks) .. " stage=" .. tostring(stage))
                if stage == 13 then
                    found = true
                    t.exec("searchRocks-dialog", t.chat.drain, { max_pages = 6 })
                end
            end
        end
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))

        -- 1.23 goUpstairsInTowerToFinish (s13)
        t.exec("goto-towerdoor2", t.player.goto_tile, 1597, 3820, 0)
        t.exec("goInTower", t.player.cross_gate, { loc = "arcquest_tower_door_right", at = { 1596, 3820, 0 },
            near = { 1597, 3820 }, far_ok = function(tile) return tile.x <= 1595 end,
            far_desc = "inside the Tower of Magic, x <= 1595 (^aoa_door_inside)" })
        t.ticks(2)
        t.exec("goUpstairsInTowerToFinish", t.player.click_loc, "arcquest_stairs_lower_left", 1)
        t.ticks(3)
        -- 1.24 talkToArceuusToFinish
        local _, coins_before = t.inv.count("coins")
        local snapr, snap = t.skill.snapshot()
        t.check("skill.snapshot", snapr == "ok", "snapshot before hand-in: " .. tostring(snapr))
        t.exec("goto-trobin", t.player.goto_tile, 1580, 3821, 1)
        t.exec("talkToArceuusToFinish", t.player.talk_to, "trobin_arceuus_visible", 1)
        t.exec("talkToArceuusToFinish-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(3)

        local _, sharks_left = t.inv.count("shark")
        local _, hp_end_t = t.skill.read("hitpoints")
        local hp_end = hp_end_t and (hp_end_t.current or hp_end_t.level or hp_end_t.boosted)
        t.check("fight.margin", (sharks_left or 0) >= 1 and hp_low >= 10,
            "hitpoints staged 40, sharks staged 8 / eaten " .. tostring(8 - (sharks_left or 0)) .. " / left " .. tostring(sharks_left)
            .. ", lowest hp over six fights " .. tostring(hp_low) .. "/40, hp after " .. tostring(hp_end) .. "/40, ticks per fight "
            .. table.concat(fight_ticks, ",") .. " (margin: a shark left and lowest hp >= 10, a quarter)")
        t.quest.expect_complete()
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", (coins_after or 0) - (coins_before or 0) == 2000, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. ", delta " .. tostring((coins_after or 0) - (coins_before or 0)) .. " == 2000 (^aoa_coin_reward)")
        local _, pages = t.inv.count("veos_memoirs_arc_page")
        t.check("reward.memoirs_page", pages == 1, "veos_memoirs_arc_page count " .. tostring(pages) .. " == 1 (aoa_quest_complete inv_add)")
        local hr, hd = t.skill.expect_gain("hunter", 1500, snap)
        t.check("reward.hunter", hr == "ok", "hunter +1500 xp documented: " .. tostring(hr) .. " " .. tostring(hd))
        local rr2, rd2 = t.skill.expect_gain("runecraft", 500, snap)
        t.check("reward.runecraft", rr2 == "ok", "runecraft +500 xp documented: " .. tostring(rr2) .. " " .. tostring(rd2))
        t.finish(0)
    end,
}
