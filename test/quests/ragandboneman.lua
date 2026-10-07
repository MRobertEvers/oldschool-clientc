-- Rag and Bone Man I: start with the Odd Old Man, buy 8 jugs of vinegar from Fortunato, kill the
-- eight creatures for their guaranteed bones (ragandboneman_drops.rs2), put each bone in a pot of
-- vinegar, boil the pots one at a time in the pot-boiler (ragandboneman_vinegar.rs2), hand in.
-- Sources: docs/quests/ladders/ragandboneman.notes.md (wiki oldid 28793800be; no LostCity quest).
-- Bring-alongs (guide item list): coins, pots, logs, tinderbox, food. Everything else is obtained.
-- Door rule (b71): every crossing on foot is pressed, in and out --
--   * the Odd Old Man and the pot-boiler (3361,3505) stand behind the Varrock members' gate
--     fai_varrock_member_gatel 3319,3468 (reach.py 3206,3233 -> 3360,3507: NEEDS-DOOR via it at
--     30/80/160; 3206,3233 -> 3318,3468 REACH closed-doors 389; 3321,3468 -> 3360,3507 REACH 100),
--     crossed by pass_door on every visit, in and out;
--   * the rams are in the pen behind fai_varrock_gate_l 3254,3347 (doubledoors.loc:318; reach.py
--     3253,3346 -> 3250,3349 NEEDS-DOOR via it; 3253,3346 -> the unicorns/bears/rats REACH);
--   * Karamja is an island: seaman_lorris's paid crossing (sailors.rs2 karamja_sailor_pay, 30
--     coins) + the sarimshipplank_off gangplank out, the customs officer's boat (customs_officer.rs2
--     customs_pay, 30 coins) + karamjashipplank_off back. The monkey and the volcano are on the free
--     side of the Karamja members' gate (reach.py 2956,3146 -> 2878,3157 REACH 91);
--   * the volcano pot hole (volcano.rs2 [oploc1,volcano_entrance], +6400) and its rope
--     (climbing_rope2 -> 0_44_49_40_30 = 2856,3166) by climb.
-- Kit: 70 coins = 8 jugs + two 30-coin crossings; 8 logs for the eight boils. Inventory 20 before
-- Fortunato so the 8-jug sale fits (ragandboneman_fortunato.rs2:71 caps the sale at the free space);
-- the 8 emptied jugs are dropped. No dialogue branches on a stat (grep stat/combat in
-- quest_ragandboneman/scripts: only the reward stat_advance), so the attack/strength staging is a
-- margin only.
return {
    id = "ragandboneman",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 40", "::setlevel strength 40", "::setlevel defence 30", "::setlevel hitpoints 60",
        "::give coins 70", "::give pot_empty 8", "::give tinderbox 1", "::give lobster 2", "::give logs 8",
        "::passive goblin_unarmed_melee_2", "::passive goblin_unarmed_melee_3",
        "::passive goblin_unarmed_melee_4", "::passive goblin_unarmed_melee_5", "::passive goblin_unarmed_melee_6",
        "::passive goblin_unarmed_melee_7", "::passive goblin_unarmed_melee_8",
        "::passive giantrat1_2", "::passive giantrat1_3", "::passive giantrat",
        "::passive medium_frog", "::passive goblin", "::passive goblin_armed", "::passive goblin_helmet",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp714_rag_quest",
            constants = { not_started = 0, collecting = 1, complete = 4 },
            display = "Rag and Bone Man I",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text()
            local r, tile = t.world.tile()
            if r ~= "ok" or type(tile) ~= "table" then
                return tostring(r)
            end
            return tostring(tile.x) .. "," .. tostring(tile.z) .. "," .. tostring(tile.level)
        end
        -- The Varrock members' gate (doubledoors.loc), pressed on every crossing.
        local function member_gate_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3318, 3468, 0)
            t.exec(name, t.player.pass_door, { closed = "fai_varrock_member_gatel",
                open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        end
        local function member_gate_out(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3321, 3468, 0)
            t.exec(name, t.player.pass_door, { closed = "fai_varrock_member_gatel",
                open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3321, 3468 }, far = { 3318, 3468 } })
        end
        -- Margin row (brief): lowest hp >= a quarter of max AND food left.
        local function fight_margin(name, detail)
            local lowest = tonumber(tostring(detail):match("lowest hp (%d+)/"))
            local _, hitpoints = t.skill.read("hitpoints")
            local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
            local food_r, food = t.inv.count("lobster")
            t.check(name, lowest ~= nil and max_hp ~= nil and food_r == "ok" and lowest * 4 >= max_hp and food >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", lobsters " .. tostring(food)
                .. " (margin: lowest hp >= a quarter of max AND food left)")
        end
        local function kill(step, npc, bone)
            t.exec(step, t.player.attack, npc, 2, 60)
            local ar, ad = t.npc.await_dead_engaged(120, 4, { eat = { item = "lobster", below = 30 } })
            t.expect(step .. ".dead", ar, ad)
            fight_margin(step .. ".margin", ad)
            t.ticks(2)
            local cr, cd = t.player.click_obj("rag_" .. bone .. "_bone")
            local _, have = t.inv.count("rag_" .. bone .. "_bone")
            t.check("pickupBone." .. bone, have ~= nil and have >= 1, "click_obj -> " .. tostring(cr) .. " " .. tostring(cd) .. " bones=" .. tostring(have))
            t.exec("useBonesOnVinegar." .. bone, t.player.use_item_on_item, "rag_" .. bone .. "_bone", "rag_pot_vinegar")
            t.exec("useBonesOnVinegar." .. bone .. ".have", t.inv.await, "rag_pot_" .. bone .. "_bone", 1, 5)
        end

        -- talkToOddOldMan: through the Varrock members' gate
        member_gate_in("oddOldMan.memberGateIn")
        t.exec("goto-oddOldMan", t.player.goto_tile, 3360, 3507, 0)
        t.ticks(2)
        t.exec("talkToOddOldMan", t.player.talk_to, "rag_odd_old_man")
        local r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToOddOldMan.menu", r, d)
        t.exec("talkToOddOldMan.help", t.chat.choose, "Anything I can do to help?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToOddOldMan.yesno", r, d)
        t.exec("talkToOddOldMan.yes", t.chat.choose, "Yes")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("talkToOddOldMan.end", r, d)
        t.expect("quest.stage.collecting", t.quest.expect_stage("collecting"))

        member_gate_out("oddOldMan.memberGateOut")

        -- talkToFortunato: first talk sets the flag, the second sells the jugs by dialogue
        t.exec("goto-fortunato", t.player.goto_tile, 3085, 3251, 0)
        t.ticks(2)
        t.exec("talkToFortunato", t.player.talk_to, "rag_wine_merchant")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("talkToFortunato.first", r, d)
        local _, wine = t.var.varbit("varb2047_rag_wine")
        t.check("talkToFortunato.flag", wine == 1, "varb2047_rag_wine=" .. tostring(wine))
        t.exec("talkToFortunato.again", t.player.talk_to, "rag_wine_merchant")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToFortunato.menu", r, d)
        t.exec("talkToFortunato.yes", t.chat.choose, "Yes")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToFortunato.buyMenu", r, d)
        t.exec("talkToFortunato.buy8", t.chat.choose, "Buy eight jugs of vinegar.")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("talkToFortunato.end", r, d)
        t.exec("talkToFortunato.jugs", t.inv.await, "rag_vinegar", 8, 10)
        local _, coins = t.inv.count("coins")
        t.check("talkToFortunato.coins", coins == 62, "coins=" .. tostring(coins) .. " (70 - 8 jugs at 1gp)")

        -- makePotOfVinegar x8
        for i = 1, 8 do
            t.exec("makePotOfVinegar" .. i, t.player.use_item_on_item, "rag_vinegar", "pot_empty")
            t.exec("makePotOfVinegar" .. i .. ".have", t.inv.await, "rag_pot_vinegar", i, 5)
        end

        for i = 1, 8 do
            t.exec("dropEmptyJug" .. i, t.player.drop, "jug_empty")
            t.ticks(1)
        end
        local _, jugs_left = t.inv.count("jug_empty")
        t.check("dropEmptyJugs", jugs_left == 0, "jug_empty left " .. tostring(jugs_left) .. " (want 0: room for the bones)")

        -- Karamja (an island): seaman_lorris's ship from Port Sarim, the gangplank ashore.
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
        t.check("seaman.sail", sail_r == "ok", "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. tostring(sail_d))
        t.exec("seaman.arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
        local deck_r, deck = t.world.tile()
        local coins1_r, coins1 = t.inv.count("coins")
        t.check("seaman.onDeckAtMusaPoint", deck_r == "ok" and deck.level == 1
                and math.abs(deck.x - 2956) <= 2 and math.abs(deck.z - 3143) <= 2
                and coins0_r == "ok" and coins1_r == "ok" and coins0 - coins1 == 30,
            "tile " .. tile_text() .. " (want the deck 2956,3143,1), coins " .. tostring(coins0)
                .. " -> " .. tostring(coins1) .. " (want -30)")
        t.exec("musaPoint.disembark", t.player.climb, { loc = "sarimshipplank_off", op_name = "Cross",
            at = { 2956, 3144, 1 }, src = { 2956, 3143 }, dest = { 2956, 3146, 0 }, slack = 1 })

        -- killMonkey near Musa Point
        t.exec("goto-killMonkey", t.player.goto_tile, 2878, 3157, 0)
        t.ticks(3)
        kill("killMonkey", "monkey", "monkey")

        -- enterKaramjaDungeon: the volcano pot hole (+6400 from where the player stands), killBat
        t.exec("goto-enterKaramjaDungeon", t.player.goto_tile, 2856, 3167, 0)
        t.exec("enterKaramjaDungeon", t.player.climb, { loc = "volcano_entrance", op = 1, op_name = "Climb-down",
            at = { 2856, 3168, 0 }, src = { 2856, 3167 }, dest = { 2856, 9567, 0 }, slack = 2 })
        t.expect("enterKaramjaDungeon.message", t.msg.expect("You climb down through the pot hole."))
        t.ticks(3)
        kill("killBat", "bat", "giant_bat")

        -- Out by the hanging rope (volcano.rs2 [oploc1,climbing_rope2] -> the rim 2856,3166)
        t.exec("goto-leaveKaramjaDungeon", t.player.goto_tile, 2855, 9569, 0)
        t.exec("leaveKaramjaDungeon", t.player.climb, { loc = "climbing_rope2", op = 1, op_name = "Climb",
            at = { 2856, 9569, 0 }, src = { 2855, 9569 }, dest = { 2856, 3166, 0 } })
        t.expect("leaveKaramjaDungeon.rim", t.msg.expect("You appear on the volcano rim."))

        -- Back to the mainland: the customs officer's boat (customs_officer.rs2 customs_pay, 30 coins)
        t.exec("goto-customs", t.player.goto_tile, 2953, 3147, 0)
        local coins2_r, coins2 = t.inv.count("coins")
        t.exec("talkToCustoms", t.player.talk_to, "customs_officer", 1)
        t.exec("talkToCustoms-dialog", t.chat.play, {
            "npc:Can I help you?",
            "options",
            "choose:Can I journey on this ship?",
            "player:Can I journey on this ship?",
            "npc:You need to be searched",
            "options",
            "choose:Search away, I have nothing to hide.",
            "player:Search away, I have nothing to hide.",
            "npc:it's all legal",
            "options",
            "choose:Ok.",
            "player:Ok.",
        })
        t.expect("customs.paid", t.msg.expect("You pay 30 coins and board the ship."))
        local back_r, back_d = t.await({
            level = function() return t.chat.kind() == "mesbox" end,
            note = "customs: the arrival mesbox after p_delay(2) + p_telejump",
        }, 15)
        t.check("customs.sailed", back_r == "ok", "await(chat.kind() == mesbox) -> " .. tostring(back_r) .. " " .. tostring(back_d))
        t.exec("customs.arrive", t.chat.play, { "mesbox:The ship arrives at Port Sarim." })
        local pier_r, pier = t.world.tile()
        local coins3_r, coins3 = t.inv.count("coins")
        t.check("customs.onDeckAtSarim", pier_r == "ok" and pier.x == 3032 and pier.z == 3217 and pier.level == 1
                and coins2_r == "ok" and coins3_r == "ok" and coins2 - coins3 == 30,
            "tile " .. tile_text() .. " (want the deck 3032,3217,1), coins " .. tostring(coins2)
                .. " -> " .. tostring(coins3) .. " (want -30)")
        t.exec("disembarkSarim", t.player.climb, { loc = "karamjashipplank_off", op = 1, op_name = "Cross",
            at = { 3031, 3217, 1 }, dest = { 3029, 3217, 0 } })

        -- killGoblin, killFrog: open ground east and south of Lumbridge
        t.exec("goto-killGoblin", t.player.goto_tile, 3246, 3243, 0)
        t.ticks(3)
        kill("killGoblin", "goblin_unarmed_melee_1", "goblin")
        t.exec("goto-killFrog", t.player.goto_tile, 3208, 3175, 0)
        t.ticks(3)
        kill("killFrog", "medium_frog_nodrops", "medium_frog")

        -- killRam: the pen behind fai_varrock_gate_l 3254,3347, in and out by its gate
        t.exec("goto-ramPenIn", t.player.goto_tile, 3254, 3346, 0)
        t.exec("ramPenIn", t.player.pass_door, { closed = "fai_varrock_gate_l", open = "fai_varrock_gate_lc",
            at = { 3254, 3347, 0 }, near = { 3254, 3346 }, far = { 3254, 3348 } })
        t.ticks(2)
        kill("killRam", "ramunsheered", "ram")
        t.exec("goto-ramPenOut", t.player.goto_tile, 3254, 3348, 0)
        t.exec("ramPenOut", t.player.pass_door, { closed = "fai_varrock_gate_l", open = "fai_varrock_gate_lc",
            at = { 3254, 3347, 0 }, near = { 3254, 3348 }, far = { 3254, 3346 } })

        -- killUnicorn, killBear, killGiantRat: open ground east of the pen
        t.exec("goto-killUnicorn", t.player.goto_tile, 3284, 3352, 0)
        t.ticks(3)
        kill("killUnicorn", "unicorn", "unicorn")
        t.exec("goto-killBear", t.player.goto_tile, 3294, 3350, 0)
        t.ticks(3)
        kill("killBear", "darkbear", "bear")
        t.exec("goto-killGiantRat", t.player.goto_tile, 3291, 3376, 0)
        t.ticks(3)
        kill("killGiantRat", "giantrat1", "giant_rat")

        -- the boiler, one pot at a time (behind the Varrock members' gate again)
        member_gate_in("potBoiler.memberGateIn")
        t.exec("goto-potBoiler", t.player.goto_tile, 3360, 3504, 0)
        t.ticks(2)
        local order = { "goblin", "medium_frog", "giant_rat", "ram", "unicorn", "bear", "monkey", "giant_bat" }
        for _, kind in ipairs(order) do
            t.exec("placeLogs." .. kind, t.player.use_on, "logs", t.player.by_symbol("loc", "rag_multi_potboiler"))
            t.ticks(2)
            t.exec("useBoneOnBoiler." .. kind, t.player.use_on, "rag_pot_" .. kind .. "_bone", t.player.by_symbol("loc", "rag_multi_potboiler"))
            t.ticks(2)
            t.exec("lightLogs." .. kind, t.player.use_on, "tinderbox", t.player.by_symbol("loc", "rag_multi_potboiler"))
            t.ticks(25)
            local _, st = t.var.varbit("varb2046_rag_boiler")
            t.check("waitForCooking." .. kind, st == 4, "varb2046_rag_boiler=" .. tostring(st) .. " (4 = boiled)")
            t.exec("removePot." .. kind, t.player.click_loc, "rag_multi_potboiler", 1)
            t.exec("removePot." .. kind .. ".have", t.inv.await, "rag_polished_" .. kind .. "_bone", 1, 8)
        end

        local polished = 0
        for _, kind in ipairs(order) do
            local _, n = t.inv.count("rag_polished_" .. kind .. "_bone")
            polished = polished + (n or 0)
        end
        t.check("repeatSteps", polished == 8, "polished bones in the backpack after eight boiler cycles: " .. tostring(polished))

        -- talkToFinish / giveBones
        t.exec("goto-giveBones", t.player.goto_tile, 3360, 3507, 0)
        t.ticks(2)
        local _, snap = t.skill.snapshot()
        t.exec("giveBones", t.player.talk_to, "rag_odd_old_man")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("giveBones.chat", r, d)
        t.quest.expect_complete()
        t.expect("reward.cooking", t.skill.expect_gain("cooking", 500, snap))
        t.expect("reward.prayer", t.skill.expect_gain("prayer", 500, snap))
        t.finish(0)
    end,
}
