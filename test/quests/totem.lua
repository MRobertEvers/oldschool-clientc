-- Tribal Totem: Kangai Mau (Brimhaven) -> GPDT depot label swap -> Wizard Cromperty teleport ->
-- KURT combination door -> trapped stairs -> chest -> hand-in. Rows named after the guide steps
-- (tools/quest_gate/ladder.py totem). Adapted from the parity3c scratch driver.
--
-- Travel (owner rule 2026-10-03, every closed space crossed by its own click; first goto included):
--   * Lumbridge -> Brimhaven: seaman_lorris at Port Sarim (30 coins, sailors.rs2 karamja_sailor_pay ->
--     deck 2956,3143,1), the gangplank ashore, the Karamja members' gate membergatel 2816,3182 (reach.py:
--     Musa Point -> Brimhaven NEEDS-DOOR), then the Shrimp and Parrot (poshdooropen 2794,3180, the map's
--     open leaf) to Kangai Mau at 2791,3182.
--   * Brimhaven -> Ardougne: captain_barnaby_karamja (2773,3229) has an Ardougne op in the cache but NO
--     script in the content (no [opnpc3,captain_barnaby_karamja]), so the way back is the Musa Point
--     customs_officer (customs_officer.rs2 customs_pay, 30 coins -> Port Sarim deck 3032,3217,1), the
--     gangplank, and the Taverley members' gate membergater 2933,3320 (the only walk to Ardougne),
--     then overland to the GPDT crates.
--   * Cromperty's room is walled (castledoubledoorr 2678,3324): pass_door in, and out when the fall
--     through the trapped stairs sends the player back to him (the sewer ladder_from_cellar 2632,9694
--     -> 2632,3295); the mansion's front door tribaltotemdoor 2635,3321 is locked from the street, so
--     the second teleport is the only way back in. Out of the mansion: chest room, pocket doors, the
--     stairs down, the combodoor, the front door, the yard gate metalgateclosedl 2635,3307.
--   * Ardougne -> Brimhaven: captain_barnaby 2679,3275 (ardougne_east_thin.rs2, 30 coins -> deck
--     2775,3234,1), the gangplank, then on foot to Kangai Mau through the open restaurant doorway.
-- Setup gives 90 coins: three 30-coin fares.
return {
    id = "totem",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel thieving 30",
        "::give coins 90", -- three 30-coin boat fares: Port Sarim -> Karamja, Brimhaven -> Ardougne, Ardougne -> Brimhaven
    },

    run = function(t)
        local function stage(want, label)
            local r, v = t.var.server("varp200_totemquest")
            t.check(label, r == "ok" and v == want, "%varp200_totemquest = " .. tostring(v) .. ", want " .. want)
        end
        local function bits(want_bit0, want_bit1, label)
            local r, v = t.var.server("varp6195_handelmort_traps_disabled")
            local b0 = (v or 0) % 2
            local b1 = math.floor((v or 0) / 2) % 2
            t.check(label, r == "ok" and b0 == want_bit0 and b1 == want_bit1,
                "handelmort_traps_disabled = " .. tostring(v) .. " (bit0 " .. b0 .. ", bit1 " .. b1 .. ")")
        end
        local names = { "a", "b", "c", "d" }
        local function tile_text(r, tt)
            if r ~= "ok" or type(tt) ~= "table" then
                return tostring(r) .. " " .. tostring(tt)
            end
            return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
        end
        local function walk(name, x, z, ticks)
            t.exec(name, t.player.walk_to, x, z, ticks or 40)
        end
        -- A paid sailing: talk, the fare's own chat, the arrival mesbox after p_delay(2) + telejump.
        local function sail_wait(name)
            local r, d = t.await({
                level = function() return t.chat.kind() == "mesbox" end,
                note = name .. ": the arrival mesbox after p_delay(2) + telejump",
            }, 15)
            t.step(name .. ".sail", r == "ok" and "PASS" or "FAIL",
                "await(chat.kind() == mesbox) -> " .. tostring(r) .. " " .. tostring(d))
        end
        local function deck_check(name, x, z, coins_before)
            local dr, deck = t.world.tile()
            local cr, coins = t.inv.count("coins")
            t.check(name, dr == "ok" and deck.level == 1 and math.abs(deck.x - x) <= 2 and math.abs(deck.z - z) <= 2
                    and cr == "ok" and coins_before - coins == 30,
                "tile " .. tile_text(dr, deck) .. " (want the deck " .. x .. "," .. z .. ",1), coins " .. tostring(coins_before)
                    .. " -> " .. tostring(coins) .. " (want -30)")
        end
        local function coins_now()
            local _, c = t.inv.count("coins")
            return c
        end

        t.quest.bind({
            varp = "varp200_totemquest",
            constants = { not_started = 0, started = 1, crate_marked = 2, crate_delivered = 3, teleported = 4, complete = 5 },
            row = "quest_tribaltotem",
            display = "Tribal Totem",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- 1.1 talkToKangaiMau: Port Sarim -> Musa Point -> the Karamja members' gate -> the restaurant
        t.exec("goto-seaman", t.player.goto_tile, 3028, 3221, 0)
        local coins0 = coins_now()
        t.exec("talkToSeaman", t.player.talk_to, "seaman_lorris", 1)
        t.exec("talkToSeaman-dialog", t.chat.play, {
            "npc:Do you want to go on a trip to Karamja?",
            "npc:The trip will cost you 30 coins.",
            "options",
            "choose:Yes please.",
            "player:Yes please.",
        })
        t.expect("seaman.paidMessage", t.msg.expect("pay the 30 coins and board the ship"))
        sail_wait("seaman")
        t.exec("seaman.arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
        deck_check("seaman.onDeckAtMusaPoint", 2956, 3143, coins0)
        t.exec("musaPoint.disembark", t.player.climb, { loc = "sarimshipplank_off", op_name = "Cross",
            at = { 2956, 3144, 1 }, src = { 2956, 3143 }, dest = { 2956, 3146, 0 }, slack = 1 })
        t.exec("goto-karamjaGate", t.player.goto_tile, 2818, 3182, 0)
        t.exec("karamjaGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
            near = { 2817, 3182 }, far_ok = function(tile) return tile.x <= 2815 end,
            far_desc = "west of the gate on the Brimhaven side, x <= 2815" })
        -- The Shrimp and Parrot: its doorway poshdooropen 2794,3180 is placed open by the map (the
        -- restaurant is z >= 3181); Kangai Mau stands at 2791,3182 inside.
        local function restaurant_in(name)
            walk(name .. ".atDoorway", 2794, 3179, 60)
            local orr, od = t.world.loc_near("poshdooropen", 3)
            t.check(name .. ".doorStandsOpen", orr == "ok" and od.tile_x == 2794 and od.tile_z == 3180 and od.level == 0,
                "poshdooropen -> " .. (orr == "ok" and (od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr)))
            walk(name .. ".through", 2794, 3182, 20)
            local r, tt = t.world.tile()
            t.check(name .. ".inside", r == "ok" and tt.z >= 3181 and tt.level == 0, "at " .. tile_text(r, tt) .. " (want inside, z >= 3181)")
        end
        local function restaurant_out(name)
            walk(name .. ".atDoorway", 2794, 3182, 20)
            walk(name .. ".through", 2794, 3179, 20)
            local r, tt = t.world.tile()
            t.check(name .. ".outside", r == "ok" and tt.z <= 3179 and tt.level == 0, "at " .. tile_text(r, tt) .. " (want street, z <= 3179)")
        end
        restaurant_in("talkToKangaiMau.restaurantIn")
        t.exec("talkToKangaiMau", t.player.talk_to, "kangai_mau", 1)
        t.exec("talkToKangaiMau-dialog", t.chat.play, {
            "npc:Hello. I Kangai Mau",
            "choose:I'm in search of adventure!",
            "player:I'm in search of adventure!",
            "npc:Adventure is something",
            "npc:I need someone to go on a mission",
            "npc:We need it back.",
            "choose:Ok, I will get it back.",
            "player:Ok, I will get it back.",
            "npc:Best of luck",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- 1.2 investigateCrate: back out of the restaurant, Captain Barnaby's boat (Brimhaven -> Ardougne,
        -- 30 coins, Talk-to dialogue), the gangplank down to the jetty, overland to the GPDT crates
        restaurant_out("investigateCrate.restaurantOut")
        t.exec("goto-barnabyKaramja", t.player.goto_tile, 2772, 3229, 0)
        local coins1 = coins_now()
        t.exec("talkToBarnabyKaramja", t.player.talk_to, "captain_barnaby_karamja", 1)
        t.exec("talkToBarnabyKaramja.d1", t.chat.drain, { stop_at = "options" })
        t.exec("talkToBarnabyKaramja.c1", t.chat.choose, "I'd like to go to Ardougne.")
        t.exec("talkToBarnabyKaramja.d2", t.chat.drain, {})
        t.expect("barnabyKaramja.sailMessage", t.msg.expect("You board the ship and sail to Ardougne."))
        t.ticks(4)
        deck_check("barnabyKaramja.onDeckAtArdougne", 2683, 3263, coins1)
        t.exec("ardougne.disembark", t.player.climb, { loc = "brimhavenshipplank_off", op_name = "Cross",
            at = { 2683, 3264, 1 }, dest = { 2683, 3266, 0 }, slack = 2 })
        t.exec("goto-investigateCrate", t.player.goto_tile, 2649, 3271, 0)
        t.exec("investigateCrate", t.player.click_loc, "horncrate", 2)
        t.exec("investigateCrate.text", t.chat.expect_text, "There is a label on this crate")
        t.exec("investigateCrate.drain", t.chat.drain, {})
        t.exec("investigateCrate.label", t.inv.await, "tribal_totem_label", 1, 6)

        -- 1.3 useLabel
        local crate = t.player.by_symbol("loc", "teleportcrate")
        t.exec("useLabel", t.player.use_on, "tribal_totem_label", crate)
        t.exec("useLabel.drain", t.chat.drain, {})
        t.ticks(2)
        stage(2, "quest.stage.crate_marked")
        t.exec("useLabel.consumed", t.inv.expect_absent, "tribal_totem_label")

        -- 1.4 talkToEmployee
        t.exec("talkToEmployee", t.player.talk_to, "rpdt_employee", 1)
        t.exec("talkToEmployee-dialog", t.chat.play, {
            "npc:Welcome to RPDT!",
            "options",
            "choose:So, when are you going to deliver this crate?",
            "player:So, when are you going to deliver this crate?",
            "npc:Well... I guess we could do it now...",
        })
        stage(3, "quest.stage.crate_delivered")

        -- 1.5 talkToCromperty
        -- His room (x 2679-2686, z 3318-3327) is walled; its double door castledoubledoorr/l is on the
        -- west wall (2678,3324 / 2678,3325): open space outside, the door by its own click.
        local function cromperty_in(name)
            t.exec(name, t.player.pass_door, { closed = "castledoubledoorr", open = "opencastledoubledoorr",
                at = { 2678, 3324, 0 }, near = { 2677, 3324 }, far = { 2680, 3324 } })
        end
        local function cromperty_out(name)
            t.exec(name, t.player.pass_door, { closed = "castledoubledoorr", open = "opencastledoubledoorr",
                at = { 2678, 3324, 0 }, near = { 2680, 3324 }, far = { 2677, 3324 } })
        end
        local function cromperty_teleport(name, first)
            t.exec(name, t.player.talk_to, "ardounge_wizard", 1)
            t.exec(name .. ".d1", t.chat.drain, { stop_at = "options" })
            t.exec(name .. ".c1", t.chat.choose, "So what have you invented?")
            t.exec(name .. ".d2", t.chat.drain, { stop_at = "options" })
            t.exec(name .. ".c2", t.chat.choose, "Can I be teleported please?")
            t.exec(name .. ".d3", t.chat.drain, { stop_at = "options" })
            t.exec(name .. ".c3", t.chat.choose, "Yes, that sounds good. Teleport me!")
            t.exec(name .. ".d4", t.chat.drain, {})
            t.ticks(8)
            local tr, tile = t.world.tile()
            t.check(name .. ".landed", tr == "ok" and tile.x == 2638 and tile.z == 3321 and tile.level == 0,
                "tile " .. tile_text(tr, tile))
        end
        t.exec("goto-talkToCromperty", t.player.goto_tile, 2677, 3324, 0)
        cromperty_in("talkToCromperty.doorIn")
        cromperty_teleport("talkToCromperty")
        stage(4, "quest.stage.teleported")

        -- 1.6 enterPassword / 1.7 solvePassword (KURT: K=10 right, U=6 left, R=9 left, T=7 left)
        local function entrance_door(name)
            t.exec(name, t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2636, 3323, 0 }, near = { 2637, 3323 }, far = { 2635, 3323 } })
        end
        entrance_door("enterPassword.entranceDoor")
        t.exec("enterPassword", t.player.click_loc, "combodoor", 1)
        t.exec("enterPassword.await", t.ui.await_open, "tribal_door")
        for i = 1, 4 do
            t.exec("enterPassword.start." .. names[i], t.ui.expect_text, "tribal_door:tribal" .. names[i], "A")
        end
        local _, enter = t.ui.widget("tribal_door:tribalenter")
        t.ui.invoke(enter, 1)
        t.exec("solvePassword.wrong.msg", t.msg.await, "This combination is incorrect.")
        bits(0, 0, "solvePassword.wrong.bit0_clear")
        t.exec("solvePassword.reopen", t.player.click_loc, "combodoor", 1)
        t.exec("solvePassword.reopen.await", t.ui.await_open, "tribal_door")
        for i = 1, 4 do
            local target = string.byte("KURT", i) - string.byte("A")
            local _, w = t.ui.widget("tribal_door:tribal" .. names[i] .. "_right")
            for _ = 1, target do t.ui.invoke(w, 1); t.ticks(1) end
        end
        t.exec("solvePassword.k", t.ui.expect_text, "tribal_door:tribala", "K")
        t.exec("solvePassword.u", t.ui.expect_text, "tribal_door:tribalb", "U")
        t.exec("solvePassword.r", t.ui.expect_text, "tribal_door:tribalc", "R")
        t.exec("solvePassword.t", t.ui.expect_text, "tribal_door:tribald", "T")
        local _, enter2 = t.ui.widget("tribal_door:tribalenter")
        t.ui.invoke(enter2, 1)
        t.exec("solvePassword.right.msg", t.msg.await, "The combination seems correct!")
        bits(1, 0, "solvePassword.right.bit0_set")
        t.exec("solvePassword.walk", t.player.click_loc, "combodoor", 1)
        t.ticks(4)
        local _, kt = t.world.tile()
        t.check("solvePassword.inside", kt and kt.x <= 2633 and kt.level == 0, "tile " .. tostring(kt and (kt.x .. "," .. kt.z)))

        -- 1.8 climbStairs: the trap first, then investigate (op 2), then climb
        t.exec("goto-climbStairs", t.player.goto_tile, 2631, 3325, 0)
        t.exec("climbStairs.trap", t.player.click_loc, "totemtrapstairs", 1)
        t.exec("climbStairs.trap.click", t.msg.await, "you hear a click")
        t.exec("climbStairs.trap.fall", t.msg.await, "You have fallen through a trap!")
        t.ticks(6)
        local _, ft = t.world.tile()
        t.check("climbStairs.trap.landed", ft and ft.x == 2640 and ft.z == 9719 and ft.level == 0,
            "tile " .. tostring(ft and (ft.x .. "," .. ft.z .. "," .. ft.level)))
        -- The pit is the Ardougne sewer (2640,9719): on foot to ladder_from_cellar 2632,9694 (42 tiles,
        -- reach.py REACH), up to 2632,3295, overland to Cromperty's door; the mansion's front door is
        -- locked from the street, so he teleports the player in a second time (rpdt_teleport repeats).
        t.exec("climbStairs.sewerLadder", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
            at = { 2632, 9694, 0 }, src = { 2632, 9695 }, dest = { 2632, 3295, 0 }, slack = 1 })
        t.ticks(2)
        t.exec("goto-talkToCromperty2", t.player.goto_tile, 2677, 3324, 0)
        cromperty_in("talkToCromperty2.doorIn")
        cromperty_teleport("talkToCromperty2")
        entrance_door("climbStairs.entranceDoor2")
        t.exec("climbStairs.combodoor2", t.player.click_loc, "combodoor", 1)
        t.ticks(4)
        local _, kt3 = t.world.tile()
        t.check("climbStairs.inside2", kt3 and kt3.x <= 2633 and kt3.level == 0, "tile " .. tostring(kt3 and (kt3.x .. "," .. kt3.z)))
        t.exec("climbStairs.investigate", t.player.click_loc, "totemtrapstairs", 2)
        t.exec("climbStairs.investigate.text", t.chat.expect_text, "Your trained senses as a thief")
        t.exec("climbStairs.investigate.drain", t.chat.drain, {})
        bits(1, 1, "climbStairs.bit1_set")
        t.exec("climbStairs.climb", t.player.click_loc, "totemtrapstairs", 1)
        t.exec("climbStairs.climb.msg", t.msg.await, "You climb up the stairs.")
        t.ticks(3)
        local _, ut = t.world.tile()
        t.check("climbStairs.landed", ut and ut.x == 2631 and ut.z == 3321 and ut.level == 1,
            "tile " .. tostring(ut and (ut.x .. "," .. ut.z .. "," .. ut.level)))

        -- 1.9 searchChest
        -- Upstairs: the landing pocket (2631-2633, z 3320-3323) is walled; poshdoor 2632,3319 opens on
        -- the hall, and the chest room (z >= 3320) behind poshdoor 2638,3319.
        local function pocket_out(name)
            t.exec(name, t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2632, 3319, 1 }, near = { 2632, 3320 }, far = { 2632, 3318 } })
        end
        local function pocket_in(name)
            t.exec(name, t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2632, 3319, 1 }, near = { 2632, 3318 }, far = { 2632, 3320 } })
        end
        local function chest_in(name)
            t.exec(name, t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2638, 3319, 1 }, near = { 2638, 3318 }, far = { 2638, 3320 } })
        end
        local function chest_out(name)
            t.exec(name, t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2638, 3319, 1 }, near = { 2638, 3320 }, far = { 2638, 3318 } })
        end
        pocket_out("searchChest.pocketOut")
        chest_in("searchChest.chestIn")
        t.exec("searchChest.open", t.player.click_loc, "totemshutchest", 1)
        t.exec("searchChest.open.msg", t.msg.await, "You open the chest.", 15)
        t.exec("searchChest", t.player.click_loc, "totemopenchest", 1)
        t.exec("searchChest.text", t.chat.expect_text, "Inside the chest you find the tribal totem.")
        t.exec("searchChest.drain", t.chat.drain, {})
        t.exec("searchChest.totem", t.inv.await, "tribal_totem", 1, 6)

        -- 1.10 talkToKangaiMauAgain
        -- Out of the chest room and the pocket, down the stairs, through the combodoor, out the front
        -- door and the yard gate, then Captain Barnaby's boat back to Brimhaven.
        chest_out("talkToKangaiMauAgain.chestOut")
        pocket_in("talkToKangaiMauAgain.pocketIn")
        t.exec("talkToKangaiMauAgain.stairsDown", t.player.climb, { loc = "stairstop", op = 1, op_name = "Climb-down",
            at = { 2631, 3322, 1 }, dest = { 2632, 3321, 0 }, slack = 1 })
        t.ticks(2)
        -- From the west side the combodoor (solved) only steps onto its own tile (totem_walk_door with
        -- entering=false): the walk off it is the player's.
        t.exec("talkToKangaiMauAgain.combodoorOut", t.player.click_loc, "combodoor", 1)
        t.ticks(3)
        walk("talkToKangaiMauAgain.combodoorOut.walk", 2635, 3323, 20)
        local _, ct = t.world.tile()
        t.check("talkToKangaiMauAgain.inHall", ct and ct.x >= 2635 and ct.level == 0, "tile " .. tostring(ct and (ct.x .. "," .. ct.z)))
        t.exec("talkToKangaiMauAgain.frontDoorOut", t.player.click_loc, "tribaltotemdoor", 1, { at = { 2635, 3321 } })
        t.ticks(3)
        walk("talkToKangaiMauAgain.frontDoorOut.walk", 2635, 3319, 20)
        local _, yt = t.world.tile()
        t.check("talkToKangaiMauAgain.inYard", yt and yt.z <= 3320 and yt.level == 0, "tile " .. tostring(yt and (yt.x .. "," .. yt.z)))
        t.exec("talkToKangaiMauAgain.yardGateOut", t.player.pass_door, { closed = "metalgateclosedl", open = "metalgateopenl",
            at = { 2635, 3307, 0 }, near = { 2635, 3308 }, far = { 2635, 3306 } })
        t.exec("goto-captainBarnaby", t.player.goto_tile, 2678, 3275, 0)
        local coins2 = coins_now()
        t.exec("talkToBarnaby", t.player.talk_to, "captain_barnaby", 1)
        t.exec("talkToBarnaby-dialog", t.chat.play, {
            "npc:Do you want to go on a trip to Brimhaven?",
            "npc:The trip will cost you 30 coins.",
            "options",
            "choose:Yes please.",
            "player:Yes please.",
        })
        t.expect("barnaby.paidMessage", t.msg.expect("pay the 30 coins and board the ship"))
        sail_wait("barnaby")
        t.exec("barnaby.arrive", t.chat.play, { "mesbox:The ship arrives at Brimhaven." })
        deck_check("barnaby.onDeckAtBrimhaven", 2775, 3234, coins2)
        t.exec("brimhaven.disembark", t.player.climb, { loc = "ardougneshipplank_off", op_name = "Cross",
            at = { 2774, 3234, 1 }, dest = { 2772, 3234, 0 }, slack = 2 })
        t.exec("goto-restaurant", t.player.goto_tile, 2801, 3180, 0)
        restaurant_in("talkToKangaiMauAgain.restaurantIn")
        local _, snap = t.skill.snapshot()
        t.exec("talkToKangaiMauAgain", t.player.talk_to, "kangai_mau", 1)
        t.exec("talkToKangaiMauAgain-dialog", t.chat.drain, {})
        t.ticks(2)
        t.exec("reward.totem_gone", t.inv.expect_absent, "tribal_totem")
        t.exec("reward.swordfish", t.inv.await, "swordfish", 5, 6)
        t.exec("reward.thieving_xp", t.skill.expect_gain, "thieving", 1775, snap)
        stage(5, "quest.stage.complete")
        t.quest.expect_complete()
        t.finish(0)
    end,
}
