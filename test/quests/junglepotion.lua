-- Jungle Potion -- the five herb searches around Tai Bwo Wannai, each
-- cleaned and handed to Trufitus.
--
-- Travel (owner rule 2026-10-03; first goto included, owner 2026-10-05):
-- every goto departs from and lands on an open outdoor tile, and everything
-- closed is crossed by its own click:
--   * Karamja is an island: seaman_lorris's paid crossing at Port Sarim
--     (sailors.rs2 karamja_sailor_talk -> karamja_sailor_pay: 30 coins,
--     p_telejump(1_46_49_12_7) = the deck at Musa Point 2956,3143,1), the
--     gangplank ashore (gangplank.rs2 [oploc1,sarimshipplank_off] ->
--     2956,3146,0), then the Karamja members' gate (membergatel 2816,3182,
--     gates.rs2 walk-through): reach.py Musa Point -> Tai Bwo Wannai reads
--     NEEDS-DOOR via membergatel at margins 80/160, so it is the only way.
--     Tai Bwo Wannai and every herb site are then REACH closed-doors on foot.
--   * The herb searches land BESIDE their loc, never on it (the palm
--     ardrigal_palm_full 2870,3115 is solid under 2871,3116).
--   * The Rogues Purse cave: pothole_cave_entrance 2824,3118 (op2 Search,
--     quest_junglepotion_locs.rs2:44-52 -> p_telejump 0_44_148_14_48 =
--     2830,9520,0, another map frame) and jp_caverocksout 2830,9522 (op1,
--     :54-56 -> p_telejump 0_44_48_7_48 = 2823,3120,0), both by
--     t.player.climb graded on the landing.
-- No fight. No staged combat stat (Herblore 99 only, the snake weed roll):
-- no dialogue on the route branches on the combat level.
return {
    id = "junglepotion",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_druidicritual",
        "::setlevel herblore 99",
        "::passive jogre",
        -- The palm's peninsula is full of aggressive harpie bug swarms (a
        -- scratch probe's t.npc.pack at 2872,3116: a dozen
        -- slayer_harpiebugswarm, two targeting the player, who fell from 10
        -- to 0 hp in five ticks). The guide has no fight there; the old
        -- goto onto the solid palm tile only hid them. Type passive
        -- (gaps-combat: `::passive <npc_symbol>`), never fought.
        "::passive slayer_harpiebugswarm",
        "::give coins 30", -- seaman_lorris's fare to Musa Point (sailors.rs2 karamja_sailor_pay)
    },
    run = function(t)
        t.quest.bind({
            varp = "varp175_junglepotion",
            constants = { not_started = 0, get_snake_weed = 1, found_snake_weed = 2, get_ardrigal = 3,
                found_ardrigal = 4, get_sito_foil = 5, found_sito_foil = 6, get_volencia_moss = 7,
                found_volencia_moss = 8, get_rogues_purse = 9, found_rogues_purse = 10,
                found_all_herbs = 11, complete = 12, complete_after_spoken = 13 },
            display = "Jungle Potion",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text(r, tt)
            if r ~= "ok" or type(tt) ~= "table" then
                return tostring(r) .. " " .. tostring(tt)
            end
            return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
        end

        -- A herb search (quest_junglepotion_locs.rs2:9-42 -> ~junglepotion_pick_herb
        -- :105-125): op2 on the guide's copy, pressed by tile. Only the snake
        -- weed is a roll (~junglepotion_snake_roll, a "You find nothing this
        -- time." mesbox on a miss); every press is an attempt folded into the
        -- outcome row, which is graded on the objbox, the stage, the herb in
        -- the pack and the copy turning into its empty loc (loc_change 100).
        local function search_herb(step, loc, at, unid, empty, stage)
            local presses, answers, got = 0, {}, false
            for i = 1, 30 do
                local cr, cd = t.player.click_loc(loc, 2, { at = { at[1], at[2] } })
                presses = i
                t.ticks(1)
                local kind = t.chat.kind()
                answers[#answers + 1] = "press " .. i .. ": " .. tostring(cr) .. " (" .. tostring(cd) .. ") page " .. tostring(kind)
                if kind == "objbox" then
                    got = true
                    break
                elseif kind ~= "none" then
                    t.chat.continue_()
                    t.ticks(1)
                end
            end
            local shown = answers
            if #answers > 3 then
                shown = { answers[1], "...", answers[#answers - 1], answers[#answers] }
            end
            t.check(step .. ".search", got,
                "searched " .. loc .. " at " .. at[1] .. "," .. at[2] .. ",0 (op2 Search) " .. presses
                    .. " time(s) until the objbox: " .. table.concat(shown, "; "))
            t.exec(step .. "-item", t.chat.expect_item, unid)
            t.exec(step .. "-text", t.chat.expect_text, "You find a herb.")
            t.chat.continue_()
            t.ticks(2)
            t.exec(step, t.quest.expect_stage, stage)
            t.exec(step .. "-held", t.inv.expect_has, unid, 1)
            local er, ed = t.world.loc_near(empty, 3, { at = { at[1], at[2], 0 }, slack = 1 })
            t.check(step .. ".emptied", er == "ok" and type(ed) == "table",
                "loc_near(" .. empty .. " at " .. at[1] .. "," .. at[2] .. ",0, slack 1) -> " .. tostring(er) .. " "
                    .. (type(ed) == "table" and (tostring(ed.tile_x) .. "," .. tostring(ed.tile_z) .. "," .. tostring(ed.level)) or tostring(ed))
                    .. " (the searched copy turned into its empty loc)")
        end

        -- The Rogues Purse cave, in (op2 Search + the yes page) and out (op1).
        local function enter_cave(name, first)
            if first then
                -- Open ground beside the rocks (2824,3118 is solid); the
                -- re-entry starts on the hand holds' landing, this same tile.
                t.exec("goto-" .. name, t.player.goto_tile, 2823, 3120, 0)
            end
            t.exec(name, t.player.climb, { loc = "pothole_cave_entrance", op = 2, op_name = "Search",
                at = { 2824, 3118, 0 }, dest = { 2830, 9520, 0 }, slack = 1,
                chat = {
                    first and "mesbox:You search the rocks... You find an entrance into some caves." or "mesbox:You search the rocks",
                    "choose:Yes, I'll enter the cave.",
                    "mesbox:You decide to enter the caves.",
                } })
            t.ticks(2)
        end
        local function leave_cave(name)
            t.exec(name, t.player.climb, { loc = "jp_caverocksout", op = 1, op_name = "Climb",
                at = { 2830, 9522, 0 }, dest = { 2823, 3120, 0 }, slack = 1,
                chat = { "mesbox:You attempt to climb the rocks back out." } })
            t.ticks(2)
        end

        -- ---------------------------------------------------------------
        -- To Tai Bwo Wannai: Port Sarim, the ship, the gangplank, the gate.
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
            level = function()
                return t.chat.kind() == "mesbox"
            end,
            note = "seaman: the arrival mesbox after p_delay(2) + telejump",
        }, 15)
        t.step("seaman.sail", sail_r == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. tostring(sail_d))
        t.exec("seaman.arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
        local deck_r, deck = t.world.tile()
        local coins1_r, coins1 = t.inv.count("coins")
        t.check("seaman.onDeckAtMusaPoint", deck_r == "ok" and deck.level == 1
                and math.abs(deck.x - 2956) <= 2 and math.abs(deck.z - 3143) <= 2
                and coins0_r == "ok" and coins1_r == "ok" and coins0 - coins1 == 30,
            "tile " .. tile_text(deck_r, deck) .. " (want the deck 2956,3143,1), coins " .. tostring(coins0)
                .. " -> " .. tostring(coins1) .. " (want -30)")
        t.exec("musaPoint.disembark", t.player.climb, { loc = "sarimshipplank_off", op_name = "Cross",
            at = { 2956, 3144, 1 }, src = { 2956, 3143 }, dest = { 2956, 3146, 0 }, slack = 1 })
        t.exec("goto-karamjaGate", t.player.goto_tile, 2818, 3182, 0)
        t.exec("karamjaGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
            near = { 2817, 3182 }, far_ok = function(tile) return tile.x <= 2815 end,
            far_desc = "west of the gate on the Brimhaven side, x <= 2815" })
        t.exec("goto-trufitus", t.player.goto_tile, 2809, 3085, 0)
        t.exec("startQuest", t.player.talk_to, "trufitus", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Greetings Bwana!",
            "npc:Welcome to our humble village.",
            "choose:It's a nice village, where is everyone?",
            "player:It's a nice village, where is everyone?",
            "npc:My people are afraid to stay in the village.",
            "npc:You may be able to help with this.",
            "choose:Me? How can I help?",
            "player:Me? How can I help?",
            "npc:I need to make a special brew!",
            "npc:I can only guide you so far",
            "npc:In return for this great favour",
            "choose:It sounds like just the challenge for me.",
            "player:It sounds like just the challenge for me.",
            "choose:Yes.",
            "npc:That is excellent Bwana!",
            "npc:Snake Weed.",
            "npc:It grows near the vines",
            "npc:the ground turns soft",
        })
        t.ticks(2)
        t.expect("quest.stage.get_snake_weed", t.quest.expect_stage("get_snake_weed"))

        -- getSnakeWeed: search the marshy jungle vine
        t.exec("goto-getSnakeWeed", t.player.goto_tile, 2763, 3043, 0)
        search_herb("getSnakeWeed", "snake_vine_full", { 2763, 3044 }, "unidentified_snake_weed",
            "snake_vine_empty", "found_snake_weed")

        -- dirty herb is declined by Trufitus
        t.exec("goto-trufitus2", t.player.goto_tile, 2809, 3085, 0)
        local trufitus = t.player.by_symbol("npc", "trufitus")
        t.exec("useDirtySnake", t.player.use_on, "unidentified_snake_weed", trufitus)
        t.exec("useDirtySnake-dialog", t.chat.play, { "npc:that herb is so dirty" })
        t.exec("stage-still-found", t.quest.expect_stage, "found_snake_weed")
        t.exec("cleanSnakeWeed", t.player.inv_op, "unidentified_snake_weed", 1)
        t.ticks(3)
        t.exec("cleanSnakeWeed-held", t.inv.expect_has, "snake_weed", 1)
        t.exec("returnSnakeWeed", t.player.use_on, "snake_weed", trufitus)
        t.exec("returnSnakeWeed-objbox-item", t.chat.expect_item, "snake_weed")
        t.exec("returnSnakeWeed-objbox-text", t.chat.expect_text, "You give the Snake Weed to Trufitus.")
        t.exec("returnSnakeWeed-dialog", t.chat.play, {
            "*",
            "npc:Great, you have the Snake Weed!",
            "npc:To the east you will find a small peninsula",
        })
        t.exec("quest.stage.get_ardrigal", t.quest.expect_stage, "get_ardrigal")
        t.exec("snake-consumed", t.inv.expect_absent, "snake_weed")

        -- getArdrigal: search the palm (2870,3115, solid under 2871,3116):
        -- stand on the open tile east of it.
        t.exec("goto-getArdrigal", t.player.goto_tile, 2872, 3116, 0)
        search_herb("getArdrigal", "ardrigal_palm_full", { 2870, 3115 }, "unidentified_ardrigal",
            "ardrigal_palm_empty", "found_ardrigal")
        t.exec("cleanArdrigal", t.player.inv_op, "unidentified_ardrigal", 1)
        t.ticks(3)
        t.exec("cleanArdrigal-held", t.inv.expect_has, "ardrigal", 1)

        t.exec("goto-trufitus3", t.player.goto_tile, 2809, 3085, 0)
        t.exec("returnArdrigal", t.player.talk_to, "trufitus", 1)
        t.exec("returnArdrigal-dialog", t.chat.play, {
            "npc:Hello Bwana, have you been able to get the Ardrigal?",
            "choose:Of course!",
            "player:Of course!",
        })
        t.exec("returnArdrigal-objbox-item", t.chat.expect_item, "ardrigal")
        t.exec("returnArdrigal-objbox-text", t.chat.expect_text, "You give the Ardrigal to Trufitus.")
        t.exec("returnArdrigal-tail", t.chat.play, {
            "*",
            "npc:Great, you have the Ardrigal!",
            "npc:You are doing well Bwana.",
        })
        t.exec("quest.stage.get_sito_foil", t.quest.expect_stage, "get_sito_foil")
        t.exec("ardrigal-consumed", t.inv.expect_absent, "ardrigal")

        -- getSitoFoil: search the scorched earth
        t.exec("goto-getSitoFoil", t.player.goto_tile, 2791, 3046, 0)
        search_herb("getSitoFoil", "sito_soil_full", { 2791, 3047 }, "unidentified_sito_foil",
            "sito_soil_empty", "found_sito_foil")
        t.exec("cleanSitoFoil", t.player.inv_op, "unidentified_sito_foil", 1)
        t.ticks(3)
        t.exec("cleanSitoFoil-held", t.inv.expect_has, "sito_foil", 1)

        t.exec("goto-trufitus4", t.player.goto_tile, 2809, 3085, 0)
        trufitus = t.player.by_symbol("npc", "trufitus")
        t.exec("returnSitoFoil", t.player.use_on, "sito_foil", trufitus)
        t.exec("returnSitoFoil-objbox-item", t.chat.expect_item, "sito_foil")
        t.exec("returnSitoFoil-objbox-text", t.chat.expect_text, "You give the Sito Foil to Trufitus.")
        t.exec("returnSitoFoil-tail", t.chat.play, {
            "*",
            "npc:Well done Bwana, just two more herbs to collect.",
            "npc:The next herb is called Volencia Moss.",
            "npc:It prefers rocks of high metal content",
        })
        t.exec("quest.stage.get_volencia_moss", t.quest.expect_stage, "get_volencia_moss")
        t.exec("sitoFoil-consumed", t.inv.expect_absent, "sito_foil")

        -- getVolenciaMoss: search the rock
        t.exec("goto-getVolenciaMoss", t.player.goto_tile, 2851, 3035, 0)
        search_herb("getVolenciaMoss", "volencia_moss_rock_full", { 2851, 3036 }, "unidentified_volencia_moss",
            "volencia_moss_rock_empty", "found_volencia_moss")
        t.exec("cleanVolenciaMoss", t.player.inv_op, "unidentified_volencia_moss", 1)
        t.ticks(3)
        t.exec("cleanVolenciaMoss-held", t.inv.expect_has, "volencia_moss", 1)

        t.exec("goto-trufitus5", t.player.goto_tile, 2809, 3085, 0)
        trufitus = t.player.by_symbol("npc", "trufitus")
        t.exec("returnVolenciaMoss", t.player.use_on, "volencia_moss", trufitus)
        t.exec("returnVolenciaMoss-objbox-item", t.chat.expect_item, "volencia_moss")
        t.exec("returnVolenciaMoss-objbox-text", t.chat.expect_text, "You give the Volencia Moss to Trufitus.")
        t.exec("returnVolenciaMoss-tail", t.chat.play, {
            "*",
            "npc:Ah Volencia Moss, beautiful.",
            "npc:caverns in the northern part of this island.",
        })
        t.exec("quest.stage.get_rogues_purse", t.quest.expect_stage, "get_rogues_purse")
        t.exec("volenciaMoss-consumed", t.inv.expect_absent, "volencia_moss")

        -- The cave: in, out by the hand holds, in again (both ways graded).
        enter_cave("enterCave", true)
        leave_cave("climbOut")
        enter_cave("reenterCave", false)

        -- getRoguePurseHerb: search the fungus covered wall (a travel hop
        -- between two open tiles of the one cave passage, REACH 28).
        t.exec("goto-getRoguePurseHerb", t.player.goto_tile, 2831, 9499, 0)
        search_herb("getRoguePurseHerb", "rogues_purse_cave_full", { 2831, 9500 }, "unidentified_rogues_purse",
            "rogues_purse_cave_empty", "found_rogues_purse")
        t.exec("cleanRoguePurse", t.player.inv_op, "unidentified_rogues_purse", 1)
        t.ticks(3)
        t.exec("cleanRoguePurse-held", t.inv.expect_has, "rogues_purse", 1)
        t.exec("goto-caveExit", t.player.goto_tile, 2830, 9520, 0)
        leave_cave("climbOut2")

        t.exec("goto-trufitus6", t.player.goto_tile, 2809, 3085, 0)
        local snap_result, before = t.skill.snapshot()
        t.exec("returnRoguePurse", t.player.talk_to, "trufitus", 1)
        t.exec("returnRoguePurse-dialog", t.chat.play, {
            "npc:Greetings Bwana, have you been successful in getting the Rogues Purse?",
            "choose:Of course!",
            "player:Of course!",
        })
        t.exec("returnRoguePurse-objbox-item", t.chat.expect_item, "rogues_purse")
        t.exec("returnRoguePurse-objbox-text", t.chat.expect_text, "You give the Rogues Purse to Trufitus.")
        t.exec("returnRoguePurse-tail", t.chat.play, {
            "*",
            "npc:Most excellent Bwana!",
            "npc:Many blessings on you!",
            "mesbox:shows you some techniques in Herblore",
        })
        t.ticks(4)
        t.exec("scroll-dismiss", t.chat.drain, { max_pages = 3 })
        t.quest.expect_complete()
        t.exec("roguesPurse-consumed", t.inv.expect_absent, "rogues_purse")
        t.check("reward.herblore", t.skill.expect_gain("herblore", 775, before))

        t.exec("talkAfter", t.player.talk_to, "trufitus", 1)
        t.exec("talkAfter-dialog", t.chat.play, {
            "npc:My greatest respects Bwana",
            "npc:looks good for my people.",
            "npc:With some blessings we will be safe here.",
            "npc:You should deliver the good news to Bwana Timfraku",
        })
        t.ticks(2)
        t.exec("quest.stage.complete_after_spoken", t.quest.expect_stage, "complete_after_spoken")
        t.finish(0)
    end,
}
