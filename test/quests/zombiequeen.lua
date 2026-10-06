-- Shilo Village (quest_zombiequeen) -- Quest Helper's helpers/quests/shilovillage/ArmsShiloVillage
-- steps, every marker resolved from quest_zombiequeen.rs2. Tier (quest_inventory.tsv): 3.
-- Rewards from Quest Helper: 3,875 Crafting xp, quest points 2; both asserted after quest.expect_complete.
--
-- Travel (door rule, b69): every goto departs from and lands on an open tile and stays on one
-- side of everything closed; everything closed is crossed by its own click.
--   * Karamja is an island: seaman_lorris's paid crossing at Port Sarim (sailors.rs2
--     karamja_sailor_pay, 30 coins -> the deck at Musa Point), the gangplank ashore
--     (gangplank.rs2 [oploc1,sarimshipplank_off]), then the members' gate membergatel 2816,3182
--     (cross_gate). Mosol Rei and Trufitus are then REACH closed-doors on foot (reach.py).
--   * The mound (Ah Za Rhoon), the palms and Rashiliyia's tomb lie EAST of the river south of Tai
--     Bwo Wannai. The Shilo stepping stones (zqrockjump3 2925,2948 north / zqrockjump1 2925,2950 south)
--     carry the player over on ONE Cross from an outer stone (2925,2947 <-> 2925,2951); a fall washes
--     him out on the far bank (2931,2953 going north / 2931,2945 going south), so cross_river()
--     passes on a clean landing OR the "You slip and fall" far-bank landing plus a walk to the end.
--   * Ah Za Rhoon: in by the roped fissure; cavern 1 -> cavern 2 by secretrubble 2887,9373 and
--     back by secretrubble 2886,9283 (rs2:846-855); out by the waterfall path zqwaterfallrocks
--     2940,9349 (rs2:997-1075: the climb lands 2929,2946, south of the stones; a fall washes the
--     player out on the north bank, from which the stones carry him back south).
--   * Cairn Isle: zqclimbingrocks 2794/2792,2979 (rs2:539-577, a roll each way) and the log
--     bridge 2776-2781,2979 (the cairn_island_bridge timer, rs2:581-626: a fall washes the player
--     to the river's south bank, east of the rocks). Into the Tomb of Bervirius by zqrocks
--     2762,2990 (rs2:633-668, a squeeze roll) and out by the handholds zqhandholds 2765,9376
--     (rs2:1340-1359, a climb roll) onto 2765,2976.
--   * Rashiliyia's tomb: in by the hillside doors (rs2:200-216), out by hillsideexitclosedl with
--     the bone key (rs2:233-247 -> 2916,3093).
-- Every loc step is pressed from an open tile beside it (never ON the mound, the sacks, the
-- gallows, the dolmen or the Cairn rocks).
--
-- Fight: Nazastarool's three forms (zombiequeen.npc zq_mainzombie1-3, hp 70/70/80, att/str/def
-- 85/80/80). No dialogue on the route branches on a combat stat (grep stat(attack/defence/
-- strength) in quest_zombiequeen/scripts: only the hitpoints-scaled fall damage), so the combat
-- stats are staged to 75 with rune gear and lobsters; each form has a margin row.
-- Agility 70 is staged for the quest's own 32 requirement (the zqrocks squeeze, rs2:639); every
-- agility roll on the route (rocks, bridge, squeeze, handholds, waterfall, tattered-scroll rubble,
-- the tomb rocks) is re-pressed on a slip.

return {
    id = "zombiequeen",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give spade 1",
        "::give torch_lit 1",
        "::give rope 1",
        "::give bronzecraftwire 1",
        "::give chisel 1",
        "::give bones 3",
        "::give coins 30", -- seaman_lorris's fare to Musa Point (sailors.rs2 karamja_sailor_pay)
        "::setlevel crafting 99",
        "::setlevel agility 70",
        "::setlevel hitpoints 75",
        "::setlevel attack 75",
        "::setlevel strength 75",
        "::setlevel defence 75",
        "::give rune_scimitar 1",
        "::give lobster 12",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::complete quest_junglepotion",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp116_zombiequeen",
            constants = {
                not_started = 0, started = 1, found_mound = 2, searched_mound = 3,
                dug_mound = 4, lit_mound = 5, roped_mound = 6, entered_ah_za_rhoon = 7,
                left_ah_za_rhoon = 8, entered_tomb_bervirius = 9,
                unlocked_rashliyia_tomb = 10, entered_with_beads = 11,
                unlocked_tombdoor = 12, retrieved_corpse = 14, complete = 15,
            },
            display = "Shilo Village",
            points = 2,
        })
        t.ticks(3)

        local function where()
            local r, tt = t.world.tile()
            if r ~= "ok" or type(tt) ~= "table" then
                return tostring(r) .. " " .. tostring(tt)
            end
            return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
        end
        local function tile_now()
            local r, tt = t.world.tile()
            if r ~= "ok" or type(tt) ~= "table" then return nil end
            return tt
        end
        local function hp_now()
            local _, hp = t.skill.read("hitpoints")
            if type(hp) == "table" then return hp.current or hp.boosted or hp.level end
            return hp
        end
        local function eat_below(limit)
            local hp = hp_now()
            if type(hp) == "number" and hp < limit then
                local _, food = t.inv.count("lobster")
                if (tonumber(food) or 0) > 0 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                end
            end
        end
        local VITALS = { eat = "lobster", below = 40 }

        -- The river south of Tai Bwo Wannai, by the stepping stones' own op (see the header):
        -- graded on the far BANK (a clean landing, or the slip's far-bank landing plus a walk).
        local function cross_river(name, northward)
            local spec
            if northward then
                spec = { loc = "zqrockjump3", op_name = "Cross", at = { 2925, 2948, 0 },
                    src = { 2925, 2947 }, dest = { 2925, 2951 }, attempts = 1 }
            else
                spec = { loc = "zqrockjump1", op_name = "Cross", at = { 2925, 2950, 0 },
                    src = { 2925, 2951 }, dest = { 2925, 2947 }, attempts = 1 }
            end
            local r, d = t.player.cross_trap(spec)
            if r ~= "ok" and string.find(tostring(d), "You slip and fall", 1, true) then
                -- A slip washes the player out on the FAR bank (2931,2953 north / 2931,2945 south);
                -- the crossing still passes once he walks to its end.
                t.ticks(4)
                t.player.walk_to(spec.dest[1], spec.dest[2], 20)
                local tt = tile_now()
                if tt ~= nil and tt.x == spec.dest[1] and tt.z == spec.dest[2] then
                    t.check(name, true, "fell, washed out on the far bank, walked to the crossing's end: " .. string.sub(tostring(d), 1, 200))
                    return true
                end
            end
            if r ~= "ok" then
                t.check(name, false, "the Shilo river crossing failed: " .. string.sub(tostring(d), 1, 300))
                return false
            end
            t.check(name, true, d)
            return true
        end

        -- Cairn Isle's climbing rocks (rs2:539-577): westward from 2795 (two force-moves to 2791, a
        -- slip falls back and hurts), eastward from 2791 (an exact-move to 2795; a slip still lands).
        local function cross_rocks(name, westward)
            if westward then
                t.exec(name, t.player.cross_trap, { loc = "zqclimbingrocks", op_name = "Climb", at = { 2794, 2979, 0 },
                    src = { 2795, 2979 }, dest = { 2791, 2979 }, attempts = 6, vitals = VITALS })
            else
                t.exec(name, t.player.cross_trap, { loc = "zqclimbingrocks", op_name = "Climb", at = { 2792, 2979, 0 },
                    src = { 2791, 2979 }, dest = { 2795, 2979 }, attempts = 6, vitals = VITALS })
            end
        end

        -- The log bridge 2776-2781,2979 (cairn_island_bridge, rs2:581-626): walked; a slip washes the
        -- player onto the river's south bank, east of the rocks, so westward he climbs the rocks again.
        local function to_cairn_isle(name)
            t.exec("goto-" .. name .. "Rocks", t.player.goto_tile, 2796, 2979, 0)
            cross_rocks(name .. "-rocksWest", true)
            local lines = {}
            local crossed = false
            for attempt = 1, 4 do
                local wr, wd = t.player.walk_route({ { 2784, 2979 }, { 2777, 2979 }, { 2770, 2979 }, { 2765, 2979 } },
                    { vitals = VITALS })
                local tt = tile_now()
                lines[#lines + 1] = "walk " .. attempt .. ": " .. tostring(wr) .. " " .. string.sub(tostring(wd), 1, 160)
                    .. " -> " .. where()
                if tt and tt.x <= 2770 and tt.z >= 2970 and tt.z <= 2992 then
                    crossed = true
                    break
                end
                eat_below(50)
                t.ticks(4)
                tt = tile_now()
                if tt and tt.x >= 2780 and tt.z <= 2975 then
                    -- washed onto the river's south bank, east of the rocks: climb them again
                    lines[#lines + 1] = "fell from the bridge onto the south bank at " .. where()
                    t.exec("goto-" .. name .. "Rocks-" .. (attempt + 1), t.player.goto_tile, 2796, 2979, 0)
                    cross_rocks(name .. "-rocksWest-" .. (attempt + 1), true)
                end
            end
            t.check(name .. "-bridgeWest", crossed, table.concat(lines, " | ") .. " (want west of the log bridge, x <= 2770)")
        end
        local function from_cairn_isle(name)
            local lines = {}
            local east = false
            for attempt = 1, 4 do
                local wr, wd = t.player.walk_route({ { 2768, 2979 }, { 2775, 2979 }, { 2783, 2979 }, { 2791, 2979 } },
                    { vitals = VITALS })
                local tt = tile_now()
                lines[#lines + 1] = "walk " .. attempt .. ": " .. tostring(wr) .. " " .. string.sub(tostring(wd), 1, 160)
                    .. " -> " .. where()
                if tt and tt.x == 2791 and tt.z == 2979 then
                    cross_rocks(name .. "-rocksEast", false)
                    east = true
                    break
                end
                if tt and tt.x >= 2782 and tt.z <= 2975 then
                    -- washed onto the south bank: that bank is the mainland side of the rocks
                    lines[#lines + 1] = "fell from the bridge onto the south bank, east of the rocks"
                    east = true
                    break
                end
                eat_below(50)
                t.ticks(4)
            end
            t.check(name .. "-bridgeEast", east, table.concat(lines, " | ") .. " (want over the log bridge onto the mainland side)")
        end

        -- zqrocks 2762,2990 (rs2:633-668): op2 Search, "Yes", then a squeeze roll; a slip leaves the
        -- player on the surface (mes lines only) and he searches again.
        local function enter_bervirius(name)
            t.exec("walk-" .. name, t.player.walk_to, 2762, 2989, 40)
            local below, lines = false, {}
            for attempt = 1, 5 do
                local sfx = attempt > 1 and ("-" .. attempt) or ""
                t.exec(name .. sfx, t.player.click_loc, "zqrocks", 2, { at = { 2762, 2990 } })
                t.exec(name .. "-dialog" .. sfx, t.chat.play, {
                    "mesbox:You investigate the rocks",
                    "choose:Yes Please, I can think of nothing nicer!",
                    "mesbox:You contort your body",
                })
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "squeeze answer" }, 6)
                if t.chat.kind() == "mesbox" then
                    t.exec(name .. "-squeeze" .. sfx, t.chat.play, {
                        "mesbox:You struggle through the narrow crevice",
                        "mesbox:And drop to your feet",
                    })
                end
                t.ticks(2)
                local tt = tile_now()
                lines[#lines + 1] = "search " .. attempt .. " -> " .. where()
                if tt and tt.z > 9000 then
                    below = true
                    break
                end
                eat_below(50)
                t.ticks(4)
                t.player.walk_to(2762, 2989, 10)
            end
            t.check(name .. "-below", below, table.concat(lines, "; ") .. " (want the Tomb of Bervirius, z > 9000)")
            return below
        end

        t.exec("equipWeapon", t.player.equip, "rune_scimitar")
        t.exec("equipBody", t.player.equip, "rune_chainbody")
        t.exec("equipLegs", t.player.equip, "rune_platelegs")
        t.exec("equipHelm", t.player.equip, "rune_full_helm")
        t.exec("equipShield", t.player.equip, "rune_kiteshield")
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- To Karamja: Port Sarim, the ship, the gangplank, the members' gate.
        -- ---------------------------------------------------------------
        t.exec("goto-seaman", t.player.goto_tile, 3028, 3221, 0)
        local coins0_r, coins0 = t.inv.count("coins")
        t.exec("talkToSeaman", t.player.talk_to, "seaman_lorris", 1)
        t.exec("talkToSeaman-dialog", t.chat.play, {
            "npc:Do you want to go on a trip to Karamja?",
            "npc:The trip will cost you 30 coins.",
            "options",
            "choose:Yes please.",
            "player:Yes please.",
        })
        t.expect("seaman.paidMessage", t.msg.expect("pay the 30 coins and board the ship"))
        local sail_r, sail_d = t.await({
            level = function() return t.chat.kind() == "mesbox" end,
            note = "seaman: the arrival mesbox after p_delay(2) + telejump",
        }, 15)
        t.step("seaman.sail", sail_r == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. tostring(sail_d))
        t.exec("seaman.arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
        local deck = tile_now()
        local coins1_r, coins1 = t.inv.count("coins")
        t.check("seaman.onDeckAtMusaPoint", deck ~= nil and deck.level == 1
                and math.abs(deck.x - 2956) <= 2 and math.abs(deck.z - 3143) <= 2
                and coins0_r == "ok" and coins1_r == "ok" and coins0 - coins1 == 30,
            "tile " .. where() .. " (want the deck 2956,3143,1), coins " .. tostring(coins0)
                .. " -> " .. tostring(coins1) .. " (want -30)")
        t.exec("musaPoint.disembark", t.player.climb, { loc = "sarimshipplank_off", op_name = "Cross",
            at = { 2956, 3144, 1 }, src = { 2956, 3143 }, dest = { 2956, 3146, 0 }, slack = 1 })
        t.exec("goto-karamjaGate", t.player.goto_tile, 2818, 3182, 0)
        t.exec("karamjaGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
            near = { 2817, 3182 }, far_ok = function(tile) return tile.x <= 2815 end,
            far_desc = "west of the gate on the Brimhaven side, x <= 2815" })

        t.exec("goto-talkToMosol", t.player.goto_tile, 2884, 2951, 0)
        t.exec("talkToMosol", t.player.talk_to, "mosol_rei_multi", 1)
        t.exec("talkToMosol-dialog", t.chat.play, {
            "mesbox:Mosol seems",
            "npc:Run! Run for your life",
            "choose:Why do I need to run?",
            "player:Why do I need to run?",
            "npc:Your very life",
            "choose:Rashiliyia? Who is she?",
            "player:Rashiliyia? Who is she?",
            "npc:Rashiliyia is the Queen",
            "choose:What can we do?",
            "player:What can we do?",
            "npc:We're doing all we can",
            "npc:And the undead",
            "npc:But you would need",
            "choose:I'll go to see the Shaman.",
            "player:I'll go to see the Shaman.",
            "npc:Well, that would be helpful",
            "choose:Yes, I'm sure and I'll take the Wampum belt to Trufitus.",
            "player:Yes, I'm sure",
            "npc:I would be very grateful",
        })
        t.ticks(2)
        local _, belt = t.inv.count("mosol_wampum_belt")
        t.check("talkToMosol-belt", belt == 1, "wampum belt count " .. tostring(belt))

        t.exec("goto-showBeltToTrufitus", t.player.goto_tile, 2809, 3085, 0)
        local trufitus = t.player.by_symbol("npc", "trufitus")
        t.exec("showBeltToTrufitus", t.player.use_on, "mosol_wampum_belt", trufitus)
        t.exec("showBeltToTrufitus-objbox", t.chat.continue_, true)
        t.exec("showBeltToTrufitus-dialog", t.chat.play, {
            "npc:Hello Bwana, this message from Mosol Rei",
            "choose:Mosol Rei said something about a legend?",
            "player:Mosol Rei said something",
            "npc:Ah, yes, there is a legend",
            "npc:The last place",
            "choose:Why was it called Ah Za Rhoon?",
            "player:Why was it called",
            "npc:It is from an ancient language",
            "npc:And most likely",
            "choose:I am going to search for Ah Za Rhoon!",
            "player:I am going to search",
            "npc:What?!",
            "npc:Are you sure",
            "choose:Yes, I will seriously look for Ah Za Rhoon and I'd appreciate your help.",
            "player:Yes, I will seriously",
            "npc:Ok then Bwana",
            "npc:adventuring",
            "npc:I'll hold on",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- The mound is across the river: the south bank of the stepping stones, then the stones.
        t.exec("goto-riverSouthBank", t.player.goto_tile, 2925, 2947, 0)
        if not cross_river("crossRiver-toMound", true) then
            return
        end

        -- The mound ahzarhoon_entrance 2921,2999 (2x2): pressed from the open tile south of it.
        t.exec("goto-useSpadeOnGround", t.player.goto_tile, 2922, 2998, 0)
        local mound = t.player.by_symbol("loc", "ahzarhoon_entrance")
        t.exec("useSpadeOnGround", t.player.use_on, "spade", mound)
        t.ticks(8)
        t.ticks(2)
        t.expect("quest.stage.dug_mound", t.quest.expect_stage("dug_mound"))

        local fissure = t.player.by_symbol("loc", "ahzarhoon_entrance_fissure")
        t.exec("useTorchOnFissure", t.player.use_on, "torch_lit", fissure)
        t.exec("useTorchOnFissure-objbox", t.chat.continue_, true)
        t.exec("useTorchOnFissure-burns", t.chat.continue_, true)
        t.exec("useTorchOnFissure-rope", t.chat.continue_, true)
        t.ticks(2)
        t.expect("quest.stage.lit_mound", t.quest.expect_stage("lit_mound"))

        t.exec("useRopeOnFissure", t.player.use_on, "rope", fissure)
        t.exec("useRopeOnFissure-objbox", t.chat.continue_, true)
        t.ticks(2)
        t.expect("quest.stage.roped_mound", t.quest.expect_stage("roped_mound"))

        t.exec("searchFissure", t.player.click_loc, "ahzarhoon_entrance_fissure_withrope", 2)
        t.exec("searchFissure-dialog", t.chat.play, {
            "mesbox:You see a small fissure",
            "choose:Yes, I'll give it a go!",
        })
        t.ticks(8)
        local in_cave = tile_now()
        t.check("searchFissure-arrived", in_cave ~= nil and in_cave.z > 9000, "tile " .. where() .. " (want Ah Za Rhoon, z > 9000)")
        t.expect("quest.stage.entered_ah_za_rhoon", t.quest.expect_stage("entered_ah_za_rhoon"))

        t.exec("goto-useChiselOnStone", t.player.goto_tile, 2901, 9378, 0)
        local stone = t.player.by_symbol("loc", "zqsecretstone")
        t.exec("useChiselOnStone", t.player.use_on, "chisel", stone)
        t.exec("useChiselOnStone-objbox", t.chat.continue_, true)
        t.inv.await("zqplaque", 1, 4)
        local _, plaque = t.inv.count("zqplaque")
        t.check("useChiselOnStone-plaque", plaque == 1, "plaque count " .. tostring(plaque))

        t.exec("goto-enterDeeperCave", t.player.goto_tile, 2888, 9374, 0)
        t.exec("enterDeeperCave", t.player.click_loc, "secretrubble", 2, { at = { 2887, 9373 } })
        t.exec("enterDeeperCave-dialog", t.chat.play, {
            "mesbox:You see that there is a narrow gap",
            "choose:Yes, I'll wriggle through.",
        })
        t.ticks(4)
        local deeper = tile_now()
        t.check("enterDeeperCave-arrived", deeper ~= nil and deeper.z < 9344,
            "tile " .. where() .. " (want the lower cavern, z < 9344: rs2:850 lands 2888,9283)")

        -- The rocks roll stat_random(agility, 75, 250) (quest_zombiequeen.rs2:869,
        -- LostCity :849): a miss is the cave-in branch (2 damage, no scroll) and
        -- the player searches again. Every attempt's click and dialogue is real.
        local tattered, attempts = 0, 0
        for attempt = 1, 8 do
            attempts = attempt
            t.exec("searchForTatteredScroll", t.player.click_loc, "secretrubblebook", 2)
            t.exec("searchForTatteredScroll-dialog", t.chat.play, {
                "mesbox:You can see that there is something hidden",
                "choose:Yes, I'm quite sure.",
                "mesbox:You start to slowly move",
            })
            -- chat.play continued "You start to slowly move": the page up now is
            -- the roll's answer -- the scroll's objbox, or the cave-in mesbox.
            local answer = t.chat.kind()
            t.exec("searchForTatteredScroll-" .. (answer == "objbox" and "objbox" or "cavein"), t.chat.continue_, true)
            t.inv.await("zqberviriusscroll", 1, 2)
            tattered = select(2, t.inv.count("zqberviriusscroll"))
            if tattered > 0 then break end
            t.note("attempt " .. attempt .. ": cave-in, page was " .. tostring(answer))
            t.ticks(3)
        end
        t.check("searchForTatteredScroll-scroll", tattered == 1,
            "tattered scroll count " .. tostring(tattered) .. " after " .. attempts .. " search(es)")

        -- The sacks zqsacks 2939,9285: pressed from the open tile west of them.
        t.exec("goto-searchForCrumpledScroll", t.player.goto_tile, 2938, 9285, 0)
        t.exec("searchForCrumpledScroll", t.player.click_loc, "zqsacks", 2, { at = { 2939, 9285 } })
        t.exec("searchForCrumpledScroll-objbox", t.chat.continue_, true)
        t.inv.await("zqrashiliyiascroll", 1, 4)
        local _, crumpled = t.inv.count("zqrashiliyiascroll")
        t.check("searchForCrumpledScroll-scroll", crumpled == 1, "crumpled scroll count " .. tostring(crumpled))

        -- The gallows zqgallows 2934,9325 (2x2): pressed from the open tile west of them.
        t.exec("goto-searchForCorpse", t.player.goto_tile, 2933, 9325, 0)
        t.exec("searchForCorpse", t.player.click_loc, "zqgallows", 2, { at = { 2934, 9325 } })
        -- `mes("You search the gallows...")` + p_delay(2) (rs2:947) runs before the page.
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "gallows page" }, 6)
        t.exec("searchForCorpse-dialog", t.chat.play, {
            "mesbox:You find a human corpse",
            "choose:Yes, I may find something else on the corpse.",
        })
        t.exec("searchForCorpse-objbox1", t.chat.continue_, true)
        t.exec("searchForCorpse-objbox2", t.chat.continue_, true)
        t.exec("searchForCorpse-mesbox", t.chat.continue_, true)
        t.inv.await("zqzadimusbones", 1, 4)
        local _, corpse = t.inv.count("zqzadimusbones")
        t.check("searchForCorpse-corpse", corpse == 1, "zadimus corpse count " .. tostring(corpse))

        -- The guide names no way out of the caverns: its next step is buryCorpse at Tai Bwo
        -- Wannai, and leaving the zone is what writes left_ah_za_rhoon ([queue,exit_ah_za_rhoon],
        -- rs2:1077-1084, any route). Back to the upper cavern by the lower rubble (rs2:850-851:
        -- z <= 9344 lands 2887,9374), then out by the waterfall path (oploc2 zqwaterfallrocks,
        -- rs2:997): "Yes" edges along it, and both its rolls end on the surface. The table raft
        -- (rs2:674) is the other exit and the only one that frames the camera (rs2:738/739); the
        -- guide takes neither, so those two sites are exempt after buryCorpse (seam34,
        -- verbs-cutscene.md).
        t.exec("goto-wriggleBack", t.player.goto_tile, 2888, 9284, 0)
        t.exec("wriggleBack", t.player.click_loc, "secretrubble", 2, { at = { 2886, 9283 } })
        t.exec("wriggleBack-dialog", t.chat.play, {
            "mesbox:You see that there is a narrow gap",
            "choose:Yes, I'll wriggle through.",
        })
        t.ticks(4)
        local upper = tile_now()
        t.check("wriggleBack-arrived", upper ~= nil and upper.z > 9344,
            "tile " .. where() .. " (want the upper cavern, z > 9344: rs2:850 lands 2887,9374)")

        t.exec("goto-leaveCavernsWaterfall", t.player.goto_tile, 2939, 9349, 0)
        t.exec("leaveCavernsWaterfall", t.player.click_loc, "zqwaterfallrocks", 2, { at = { 2940, 9349 } })
        t.exec("leaveCavernsWaterfall-dialog", t.chat.play, {
            "mesbox:You see a huge waterfall blocking your path",
            "choose:Yes, I'll follow the path.",
        })
        t.await({ level = function()
            local tile = tile_now()
            return tile ~= nil and tile.z < 9000
        end, note = "out of the caverns by the waterfall path" }, 20)
        t.ticks(4)
        local out_tile = tile_now()
        t.check("leaveCavernsWaterfall-surface", out_tile ~= nil and out_tile.z < 9000,
            "tile " .. where() .. " (the climb lands 2929,2946 south of the stones; a fall washes out north of them)")
        if out_tile ~= nil and out_tile.z >= 2948 then
            -- The fall (rs2:1015-1033) washes the player out on the walkable north bank.
            t.note("leaveCavernsWaterfall: fell and washed out at " .. where())
            t.exec("goto-riverNorthBank-afterFall", t.player.goto_tile, 2925, 2951, 0)
            if not cross_river("crossRiver-afterWaterfall", false) then
                return
            end
        end
        eat_below(50)

        t.exec("goto-buryCorpse", t.player.goto_tile, 2796, 3088, 0)
        t.ticks(4)
        t.expect("quest.stage.left_ah_za_rhoon", t.quest.expect_stage("left_ah_za_rhoon"))
        t.exec("buryCorpse", t.player.inv_op, "zqzadimusbones", 1)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "burial page" }, 10)
        t.exec("buryCorpse-dialog", t.chat.play, {
            "mesbox:You hear an unearthly moaning",
        })
        t.await({ level = function() return t.chat.kind() == "npc" end, note = "zadimus ghost page" }, 10)
        t.exec("buryCorpse-ghost", t.chat.play, {
            "npc:You have released me",
        })
        t.await({ level = function() return t.chat.kind() == "objbox" end, note = "bone shard objbox" }, 10)
        t.exec("buryCorpse-objbox", t.chat.continue_, true)
        t.exec("buryCorpse-objbox2", t.chat.continue_, true)
        t.await({ level = function() return t.chat.kind() == "none" end, note = "burial dialogue closed" }, 6)
        t.inv.await("zqboneshard", 1, 4)
        local _, shard = t.inv.count("zqboneshard")
        t.check("buryCorpse-shard", shard == 1, "bone shard count " .. tostring(shard))
        -- The raft's camera (cutscene_row_required): off the guide's route, so exempt, naming
        -- the guide step driven instead. gate.py refuses this for a site a guide step reaches.
        local raft_reason = "the guide leaves the caverns by no named step; this test left by the "
            .. "waterfall path (leaveCavernsWaterfall) and drove buryCorpse instead of the table raft "
            .. "([oploc2,zqtableraft], rs2:674)"
        t.cutscene.exempt("quest_zombiequeen.rs2:738", raft_reason)
        t.cutscene.exempt("quest_zombiequeen.rs2:739", raft_reason)

        t.exec("readTattered", t.player.inv_op, "zqberviriusscroll", 1)
        t.await({ level = function() return t.chat.kind() == "mesbox" end, note = "read question page" }, 6)
        t.exec("readTattered-question", t.chat.expect_text, "part of a scroll about someone called Bervirius")
        t.exec("readTattered-continue", t.chat.continue_, true)
        t.await({ level = function() return t.chat.kind() == "options" end, note = "read choice" }, 8)
        t.exec("readTattered-yes", t.chat.choose, "Yes please.")
        t.await({ level = function() return select(2, t.ui.is_modal()) == true end, note = "scroll interface" }, 6)
        t.check("readTattered-open", select(2, t.ui.is_modal()) == true, "tattered scroll interface modal")
        t.key("escape")
        t.await({ level = function() return select(2, t.ui.is_modal()) == false end, note = "scroll closed" }, 6)

        t.exec("readCrumpled", t.player.inv_op, "zqrashiliyiascroll", 1)
        t.await({ level = function() return t.chat.kind() == "mesbox" end, note = "read question page" }, 6)
        t.exec("readCrumpled-question", t.chat.expect_text, "a scroll about Rashiliyia")
        t.exec("readCrumpled-continue", t.chat.continue_, true)
        t.await({ level = function() return t.chat.kind() == "options" end, note = "read choice" }, 8)
        t.exec("readCrumpled-yes", t.chat.choose, "Yes please.")
        t.await({ level = function() return select(2, t.ui.is_modal()) == true end, note = "scroll interface" }, 6)
        t.check("readCrumpled-open", select(2, t.ui.is_modal()) == true, "crumpled scroll interface modal")
        t.key("escape")
        t.await({ level = function() return select(2, t.ui.is_modal()) == false end, note = "scroll closed" }, 6)

        -- Cairn Isle: the rocks and the log bridge, then the squeeze into the Tomb of Bervirius.
        to_cairn_isle("toCairnIsle")
        if not enter_bervirius("searchRocksOnCairn") then
            return
        end

        -- The dolmen zqdolmen 2766,9364 (2x2): pressed from the open tile west of it.
        t.exec("goto-searchDolmen", t.player.goto_tile, 2765, 9364, 0)
        t.exec("searchDolmen", t.player.click_loc, "zqdolmen", 2, { at = { 2766, 9364 } })
        t.exec("searchDolmen-dialog", t.chat.play, {
            "mesbox:The dolmen is intricately decorated",
        })
        t.exec("searchDolmen-pommel", t.chat.continue_, true)
        t.exec("searchDolmen-crystal", t.chat.continue_, true)
        t.exec("searchDolmen-notes", t.chat.continue_, true)
        t.inv.await("zqbevsword", 1, 4)
        local _, pommel = t.inv.count("zqbevsword")
        t.check("searchDolmen-pommel-held", pommel == 1, "sword pommel count " .. tostring(pommel))

        t.exec("useChiselOnPommel", t.player.use_item_on_item, "chisel", "zqbevsword")
        t.exec("useChiselOnPommel-crafted", t.chat.continue_, true)
        t.inv.await("zqbonebeads", 1, 4)
        local _, bone_beads = t.inv.count("zqbonebeads")
        t.check("useChiselOnPommel-beads", bone_beads == 1, "bone beads count " .. tostring(bone_beads))
        t.exec("useChiselOnPommel-close", t.chat.continue_, true)

        t.exec("useWireOnBeads", t.player.use_item_on_item, "bronzecraftwire", "zqbonebeads")
        t.inv.await("zqdeadbeads", 1, 4)
        local _, dead_beads = t.inv.count("zqdeadbeads")
        t.check("useWireOnBeads-necklace", dead_beads == 1, "beads of the dead count " .. tostring(dead_beads))
        t.exec("useWireOnBeads-close", t.chat.continue_, true)
        t.exec("equipBeads", t.player.equip, "zqdeadbeads")

        -- Out of the tomb by the handholds zqhandholds 2765,9376 (rs2:1340-1359): a roll; a slip
        -- drops the player two tiles back inside and he climbs again.
        t.exec("goto-handholds", t.player.goto_tile, 2764, 9375, 0)
        local out_lines, out_ok = {}, false
        for attempt = 1, 5 do
            local cr, cd = t.player.climb({ loc = "zqhandholds", op_name = "Climb", at = { 2765, 9376, 0 },
                dest = { 2765, 2976, 0 }, slack = 1 })
            out_lines[#out_lines + 1] = "climb " .. attempt .. ": " .. tostring(cr) .. " " .. string.sub(tostring(cd), 1, 220)
            if cr == "ok" then
                out_ok = true
                break
            end
            eat_below(50)
            t.ticks(3)
            t.player.walk_to(2764, 9375, 10)
        end
        t.check("climbHandholds", out_ok, table.concat(out_lines, " | ") .. " (want Cairn Isle 2765,2976 +-1)")
        t.ticks(2)

        -- Back over the bridge and the rocks, to the river and across it to the palms.
        from_cairn_isle("fromCairnIsle")
        eat_below(50)
        t.exec("goto-riverSouthBank-toPalms", t.player.goto_tile, 2925, 2947, 0)
        if not cross_river("crossRiver-toPalms", true) then
            return
        end
        t.exec("goto-searchPalms", t.player.goto_tile, 2916, 3095, 0)
        t.exec("searchPalms", t.player.click_loc, "zqquest_hidytree", 2)
        t.ticks(2)
        t.exec("searchDoors", t.player.click_loc, "hillsideclosedl", 2)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "door page" }, 8)
        t.exec("searchDoors-dialog", t.chat.play, {
            "mesbox:Examining the door",
        })

        t.exec("makeKey", t.player.use_item_on_item, "chisel", "zqboneshard")
        t.exec("makeKey-page", t.chat.expect_text, "You successfully make a key")
        t.exec("makeKey-close", t.chat.continue_, true)
        t.inv.await("zqbonekey", 1, 4)
        local _, bone_key = t.inv.count("zqbonekey")
        t.check("makeKey-key", bone_key == 1, "bone key count " .. tostring(bone_key))

        local tomb_door = t.player.by_symbol("loc", "hillsideclosedl")
        t.exec("useKeyOnDoor", t.player.use_on, "zqbonekey", tomb_door)
        t.ticks(3)
        t.expect("quest.stage.unlocked_rashliyia_tomb", t.quest.expect_stage("unlocked_rashliyia_tomb"))

        -- The palms spring back 50 ticks after the search (hide_rashiliyia_doors): re-search, reopen, walk in.
        t.exec("searchPalms-again", t.player.click_loc, "zqquest_hidytree", 2)
        t.ticks(2)
        t.exec("enterDoor", t.player.click_loc, "hillsidedooropenl", 1)
        t.ticks(3)
        t.expect("quest.tomb_entered", select(1, t.world.tile()))
        local tomb_tile = tile_now()
        t.check("enterDoor-inside", tomb_tile ~= nil and tomb_tile.z > 9000,
            "tile " .. where() .. " (want Rashiliyia's tomb, z > 9000: rs2:214 lands 2929,9525)")

        t.exec("useBonesOnDoor-gate", t.player.click_loc, "zombiequeengateclosedl", 1)
        t.ticks(3)
        local gate_tile = tile_now()
        t.check("useBonesOnDoor-pastGate", gate_tile ~= nil and gate_tile.z <= 9515,
            "tile " .. where() .. " (want south of the gate: rs2:299 lands 2929,9515)")
        t.expect("quest.stage.entered_with_beads", t.quest.expect_stage("entered_with_beads"))

        -- The rocks roll stat_random(agility, 150, 252): a fall lands back at the top, so click again.
        for attempt = 1, 6 do
            t.exec("climbDownRocks", t.player.click_loc, "zq_rashrocks", 1)
            t.ticks(6)
            local rocks_tile = tile_now()
            if rocks_tile ~= nil and rocks_tile.z < 9512 then break end
            t.note("climb attempt " .. attempt .. " fell or did not move")
            eat_below(40)
        end
        local below_tile = tile_now()
        t.check("climbDownRocks-below", below_tile ~= nil and below_tile.z < 9512, "tile " .. where())
        local tomb_door_loc = t.player.by_symbol("loc", "thzq_tombrooml1")
        local walk_result
        for hop = 1, 12 do
            walk_result = t.player.walk_to(2893, 9483, 25)
            eat_below(40)
            local hop_tile = tile_now()
            if hop_tile ~= nil and math.abs(hop_tile.x - 2893) <= 2 and math.abs(hop_tile.z - 9483) <= 2 then break end
        end
        local door_tile = tile_now()
        t.check("walkToTombDoor", door_tile ~= nil and math.abs(door_tile.x - 2893) <= 3 and math.abs(door_tile.z - 9483) <= 3,
            "walk_to -> " .. tostring(walk_result) .. " hp=" .. tostring(hp_now()) .. " tile " .. where())
        t.exec("useBonesOnDoor1", t.player.use_on, "bones", tomb_door_loc, { at = { 2892, 9480 } })
        t.exec("useBonesOnDoor1-page", t.chat.expect_text, "two recesses left")
        t.exec("useBonesOnDoor1-close", t.chat.continue_, true)
        t.exec("useBonesOnDoor2", t.player.use_on, "bones", t.player.by_symbol("loc", "thzq_tombrooml2"), { at = { 2892, 9480 } })
        t.exec("useBonesOnDoor2-page", t.chat.expect_text, "one recess left")
        t.exec("useBonesOnDoor2-close", t.chat.continue_, true)
        t.exec("useBonesOnDoor3", t.player.use_on, "bones", t.player.by_symbol("loc", "thzq_tombrooml3"), { at = { 2892, 9480 } })
        t.exec("useBonesOnDoor3-page", t.chat.expect_text, "All the recesses are filled")
        t.exec("useBonesOnDoor3-close", t.chat.continue_, true)
        t.exec("useBonesOnDoor3-skeletons", t.chat.play, {
            "mesbox:The door seems to change slightly",
        })
        t.ticks(3)
        t.expect("quest.stage.unlocked_tombdoor", t.quest.expect_stage("unlocked_tombdoor"))
        local fight_tile = tile_now()
        t.check("useBonesOnDoor-inside", fight_tile ~= nil and fight_tile.z > 9480, "tile " .. where() .. " (want the dolmen room north of the door)")
        eat_below(60)
        t.exec("searchDolmenForFight", t.player.click_loc, "zqrashdolmen", 2)
        t.exec("searchDolmenForFight-shake", t.chat.play, {
            "mesbox:You touch the dolmen",
        })
        local forms = { "zq_mainzombie1", "zq_mainzombie2", "zq_mainzombie3" }
        for form = 1, 3 do
            local name = "killNazastarool" .. form
            t.exec(name .. "-present", t.npc.await_present, forms[form], 20, 40)
            local _, food_before = t.inv.count("lobster")
            t.exec(name .. "-attack", t.player.attack, forms[form], 2, 15, { eat = { item = "lobster", below = 35 } })
            local _, dead_detail = t.exec(name .. "-dead", t.npc.await_dead_engaged, 240, 40,
                { eat = { item = "lobster", below = 35 } })
            -- The fight's margin: lowest hp >= a quarter of max AND food left.
            local lowest = tonumber(tostring(dead_detail):match("lowest hp (%d+)/"))
            local _, hp_read = t.skill.read("hitpoints")
            local max_hp = type(hp_read) == "table" and hp_read.base_level or nil
            local _, food_left = t.inv.count("lobster")
            t.check(name .. "-margin", lowest ~= nil and max_hp ~= nil and lowest * 4 >= max_hp and (tonumber(food_left) or 0) >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", lobsters " .. tostring(food_before) .. " -> "
                    .. tostring(food_left) .. " (margin: lowest hp >= a quarter of max AND food left)")
            t.ticks(4)
            t.exec(name .. "-page", t.chat.drain, { max_pages = 4 })
            eat_below(60)
        end
        t.ticks(3)
        t.exec("pickupCorpse", t.player.click_obj, "zqcorpse", 3)
        t.inv.await("zqcorpse", 1, 6)
        local _, corpse_count = t.inv.count("zqcorpse")
        t.check("pickupCorpse-held", corpse_count == 1, "Rashiliyia corpse count " .. tostring(corpse_count))
        t.ticks(3)
        t.expect("quest.stage.retrieved_corpse", t.quest.expect_stage("retrieved_corpse"))
        -- The fight room is the pocket north of the carved door: the door's op1 walks back out of it.
        local door_back_result, door_back_detail
        for _, door_symbol in ipairs({ "thzq_tombrooml3", "thzq_tombrooml1", "thzq_tombrooml2" }) do
            if t.world.loc_near(door_symbol, 8) == "ok" then
                door_back_result, door_back_detail = t.player.click_loc(door_symbol, 1, { at = { 2892, 9480 } })
                break
            end
        end
        t.ticks(4)
        local back_tile = tile_now()
        t.check("leaveBossRoom", back_tile ~= nil and back_tile.z <= 9481 and back_tile.z >= 9479,
            "door op1 -> " .. tostring(door_back_result) .. " " .. tostring(door_back_detail) .. " tile " .. where())
        for attempt = 1, 6 do
            t.exec("climbUpRocks", t.player.click_loc, "zq_rashrocks", 1)
            t.ticks(8)
            local up_tile = tile_now()
            if up_tile ~= nil and up_tile.z > 9512 then break end
            t.note("climb up attempt " .. attempt .. " fell or did not move")
            eat_below(40)
        end
        local up_now = tile_now()
        t.check("climbUpRocks-top", up_now ~= nil and up_now.z > 9512, "tile " .. where())
        t.exec("exitGate", t.player.click_loc, "zombiequeengateclosedl", 1)
        t.ticks(4)
        local gate_out = tile_now()
        t.check("exitGate-past", gate_out ~= nil and gate_out.z >= 9518, "tile " .. where() .. " (want north of the gate: rs2:305 lands 2929,9518)")
        local exit_door = t.player.by_symbol("loc", "hillsideexitclosedl")
        t.exec("useKeyOnExit", t.player.use_on, "zqbonekey", exit_door)
        t.ticks(8)
        local outside = tile_now()
        t.check("useKeyOnExit-outside", outside ~= nil and outside.z < 9000,
            "tile " .. where() .. " (want the hillside 2916,3093: rs2:245)")
        t.exec("walk-offHillside", t.player.walk_to, 2916, 3095, 10)

        -- Back across the river and the bridge to Cairn Isle, into the tomb again.
        t.exec("goto-riverNorthBank-toCairn", t.player.goto_tile, 2925, 2951, 0)
        if not cross_river("crossRiver-toCairn", false) then
            return
        end
        eat_below(50)
        to_cairn_isle("toCairnIsleAgain")
        if not enter_bervirius("enterCairnAgain") then
            return
        end
        t.exec("goto-useCorpseOnDolmen", t.player.goto_tile, 2765, 9364, 0)
        local snap_result, xp_snapshot = t.skill.snapshot()
        t.check("useCorpseOnDolmen-xpBefore", snap_result == "ok", "skill.snapshot before the hand-in -> " .. tostring(snap_result))
        t.exec("useCorpseOnDolmen", t.player.use_on, "zqcorpse", t.player.by_symbol("loc", "zqdolmen"))
        t.exec("useCorpseOnDolmen-remains", t.chat.expect_text, "carefully place Rashiliyia")
        t.exec("useCorpseOnDolmen-remains-close", t.chat.continue_, true)
        t.await({ level = function() return t.chat.kind() == "npc" end, note = "Rashiliyia speaks" }, 10)
        t.exec("useCorpseOnDolmen-gratitude", t.chat.play, {
            "npc:You have my gratitude",
            "npc:My hatred and bitterness",
        })
        t.ticks(4)
        local _, corpse_left = t.inv.count("zqcorpse")
        t.check("useCorpseOnDolmen-consumed", corpse_left == 0, "Rashiliyia corpse count after the dolmen " .. tostring(corpse_left))
        t.ticks(6)
        local _, reward_xp = t.scroll.reward_xp("crafting")
        t.check("reward.scroll_xp", type(reward_xp) == "number", "scroll crafting xp line -> " .. tostring(reward_xp))
        t.expect("reward.crafting_xp_up", t.skill.expect_gain("crafting", 3875, xp_snapshot))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
