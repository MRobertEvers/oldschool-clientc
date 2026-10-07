-- Death on the Isle. Guide: Quest Helper deathontheisle (DeathOnTheIsle.java); content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_deathontheisle/ (deathontheisle.rs2,
-- doti_investigation.rs2, doti_theatre.rs2, aldarin_ferry.rs2, configs/deathontheisle.spawn).
-- Sources: OSRS Wiki Death on the Isle (oldid 15241076), quick guide (oldid 14976503),
-- Transcript:Death on the Isle (oldid 15246180).
--
-- Setup stages only the prerequisites (Children of the Sun, Thieving 34, Agility 32 -- the quest's
-- own requirements) and a modest combat block (attack 45, strength 50, defence 45, hitpoints 45) for
-- Adala's bounded fight against her 60-hitpoint form (wiki: "Combat level 40
-- recommended"; no dialogue in the quest branches on a combat stat -- grep stat( / combat in the
-- quest's .rs2). Coins: Antonia's 20-coin fare from the Sunset Coast.
--
-- Travel: Aldarin is an island with no on-foot route. Regulus Cento's quetzal (Varrock -> Civitas
-- illa Fortis, Children of the Sun), on foot to the Sunset Coast, Antonia's boat to Aldarin
-- (wiki: "pay 20 coins to take a boat from the Sunset Coast; via Antonia";
-- tools/data/shortest_path/transports/ships.tsv:63 1494,2985 -> 1443,2977).
--
-- Villa Lucens is walled (doti_barrier guest gaps refuse until the quest is complete): the player
-- enters through the Head Butler (staff entrance) and every cellar / backstage / shortcut is pressed.

local function tile_str(tl)
    if tl == nil then return "nil" end
    return tl.x .. "," .. tl.z .. "," .. tl.level
end

return {
    id = "deathontheisle",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::setlevel thieving 34",
        "::setlevel agility 32",
        "::setlevel attack 45",
        "::setlevel strength 50",
        "::setlevel defence 45",
        "::setlevel hitpoints 45",
        "::complete quest_childrenofthesun",
        "::give coins 20",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb11210_doti",
            constants = {
                not_started = 0, uniform = 4, uniform2 = 6, patzi2 = 8, butler = 10, butler2 = 12,
                inside = 14, intros = 15, cellar = 16, wine = 18, body = 19, guards = 20, clues = 22,
                pockets = 26, accuse = 27, suspects = 28, adala = 32, guards2 = 33, theatre = 34,
                theatre2 = 36, cellar2 = 38, snitch = 40, naiatli = 42, onstage = 45, finish = 49,
                complete = 50,
            },
            row = "quest_deathontheisle",
            display = "Death on the Isle",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- A talk whose page count depends on the stage: check the opener, then click to each menu.
        local function talk(step, npc, first, choices, opts)
            t.exec(step, t.player.talk_to, npc, 1, opts)
            if first then
                t.exec(step .. "-open", t.chat.play, { first })
            end
            for i, row in ipairs(choices or {}) do
                t.exec(step .. "-menu" .. i, t.chat.drain, { stop_at = "options", max_pages = 60 })
                t.exec(step .. "-choose" .. i, t.chat.choose, row)
            end
            t.exec(step .. "-end", t.chat.drain, { max_pages = 80 })
            t.ticks(2)
        end
        local function at_tile(name, want, slack)
            local _, here = t.world.tile()
            local ok = here ~= nil and here.level == want[3] and math.abs(here.x - want[1]) <= (slack or 0)
                and math.abs(here.z - want[2]) <= (slack or 0)
            t.check(name, ok, "player at " .. tile_str(here) .. ", want " .. want[1] .. "," .. want[2] .. ","
                .. want[3] .. " (slack " .. tostring(slack or 0) .. ")")
        end

        -- ================================================================
        -- 0. Travel: Regulus Cento's quetzal to Civitas, Antonia's boat to Aldarin.
        -- ================================================================
        t.exec("goto-talkToRegulus", t.player.goto_tile, 3281, 3413, 0)
        t.exec("talkToRegulus", t.player.talk_to, "vmq2_quetzal_keeper_varrock", 1)
        t.exec("talkToRegulus-dialog", t.chat.play, {
            "npc:Nilsal, adventurer. Do you wis",
            "choose:Let's do it!",
            "player:Let's do it!",
            "npc:Then hold on tight. Varlamore ",
        })
        t.await({
            level = function()
                local _, here = t.world.tile()
                return here ~= nil and here.x < 2000
            end,
            note = "landed in Civitas illa Fortis",
        }, 10)
        at_tile("talkToRegulus-arrived", { 1697, 3140, 0 }, 1)
        t.ticks(3)

        -- Antonia (wiki: Sunset Coast 1495,2985). The Sunset Coast is on foot from Civitas.
        t.exec("goto-antonia", t.player.goto_tile, 1494, 2984, 0)
        t.exec("sailToAldarin", t.player.talk_to, "sunset_coast_to_aldarin_sailor", 1)
        t.exec("sailToAldarin-dialog", t.chat.play, {
            "npc:Interested in sailing to the island of Aldarin",
            "choose:Yes please.",
            "player:Yes please.",
        })
        t.await({
            level = function()
                local _, here = t.world.tile()
                return here ~= nil and here.x < 1460 and here.z > 2960
            end,
            note = "landed on Aldarin",
        }, 10)
        at_tile("sailToAldarin-arrived", { 1443, 2977, 0 }, 1)
        local _, coins_left = t.inv.count("coins")
        t.check("sailToAldarin-fare", coins_left == 0, "coins after the 20-coin fare: " .. tostring(coins_left))
        t.ticks(3)

        -- ================================================================
        -- Getting the right fit
        -- ================================================================
        t.exec("goto-talkToPatziToStartQuest", t.player.goto_tile, 1414, 2936, 0)
        t.exec("patzi.present", t.npc.await_present, "doti_patzi", 15, 10)
        talk("talkToPatziToStartQuest", "doti_patzi_core", "npc:Good day! I don't suppose I could trouble",
            { "Yes." })
        t.expect("quest.stage.uniform", t.quest.expect_stage("uniform"))

        -- The house north of Patzi, through its south window (wiki House window 1401,2967; Agility 32).
        -- The Wandering Guard refuses the climb when he is in view (deathontheisle.rs2
        -- [oploc1,doti_house_window]); his patrol is visible, so the press waits for him to step away.
        t.exec("goto-enterUniformHouse", t.player.goto_tile, 1401, 2966, 0)
        local entered = false
        for attempt = 1, 10 do
            local gr, gtile = t.npc.nearest("doti_wanderingguard", 6)
            if gr == "ok" and gtile ~= nil and gtile.x ~= nil and math.abs(gtile.x - 1401) <= 3
                and math.abs(gtile.z - 2966) <= 3 then
                t.note("enterUniformHouse attempt " .. attempt .. ": the Wandering Guard is at "
                    .. gtile.x .. "," .. gtile.z .. "; waiting for him to step away")
                t.ticks(6)
            else
                local r = t.exec("enterUniformHouse" .. (attempt > 1 and ("-retry" .. attempt) or ""),
                    t.player.cross_trap, { loc = "doti_house_window", op_name = "Enter", at = { 1401, 2967, 0 },
                        src = { 1401, 2966 }, dest = { 1401, 2967 }, attempts = 1 })
                if r == "ok" then
                    entered = true
                    break
                end
                if t.chat.kind() ~= "none" then
                    t.chat.drain({ max_pages = 6 })
                end
                t.ticks(6)
            end
        end
        if not entered then
            t.blocked("content: the house window never let the player in (Wandering Guard in view 10 times)")
            return
        end

        t.exec("stealUniformFromWardrobe", t.player.click_loc, "doti_butler_rack", 1)
        t.exec("stealUniformFromWardrobe-box", t.chat.drain, { max_pages = 4 })
        t.exec("stealUniformFromWardrobe-items", t.inv.await_all,
            { doti_butleruniform = 1, doti_butleruniform_legs = 1 }, 6)

        t.exec("leaveUniformHouse", t.player.cross_trap, { loc = "doti_house_window", op_name = "Enter",
            at = { 1401, 2967, 0 }, src = { 1401, 2967 }, dest = { 1401, 2966 }, attempts = 2 })

        -- Back to Patzi. Adala heads inside (8) on the uniform conversation; the quick guide's
        -- "Make sure to finish the dialogue" is the second talk (8 -> 10).
        t.exec("goto-talkToPatziAfterStealingUniform", t.player.goto_tile, 1414, 2936, 0)
        t.exec("talkToPatziAfterStealingUniform", t.player.talk_to, "doti_patzi_core", 1)
        t.exec("talkToPatziAfterStealingUniform-dialog", t.chat.play, {
            "npc:managed to find a uniform",
            "player:I have indeed.",
            "npc:It was a bit of a lucky spot",
            "player:Lucky indeed.",
            "npc:Just good fortune.",
            "mesbox:Adala heads inside.",
        })
        t.ticks(2)
        -- Walk off mid-conversation: the state is 8 (Adala gone), the staff entrance not yet heard.
        t.chat.close()
        t.ticks(2)
        t.expect("quest.stage.patzi2", t.quest.expect_stage("patzi2"))
        talk("continueTalkingToPatzi", "doti_patzi_core", "npc:Oh, she's in a rush, isn't she?")
        t.expect("quest.stage.butler", t.quest.expect_stage("butler"))

        -- Wear the uniform; the Head Butler at the staff entrance holds everything else.
        t.exec("wear-uniformTop", t.player.equip, "doti_butleruniform")
        t.ticks(2)
        t.exec("wear-uniformLegs", t.player.equip, "doti_butleruniform_legs")
        t.ticks(2)
        t.exec("goto-equipButlersOutfitAndHeadInside", t.player.goto_tile, 1425, 2919, 0)
        t.exec("headbutler.present", t.npc.await_present, "doti_headbutler", 10, 10)
        talk("equipButlersOutfitAndHeadInside", "doti_headbutler_core", "npc:Oh, nilsal, to you.", { "I am." })
        t.await({
            level = function()
                local _, here = t.world.tile()
                return here ~= nil and here.x >= 1427
            end,
            note = "through the staff entrance",
        }, 8)
        at_tile("equipButlersOutfitAndHeadInside-inside", { 1428, 2919, 0 }, 0)
        t.expect("quest.stage.inside", t.quest.expect_stage("inside"))
        local _, heldv = t.var.server("varb14283_holding_inventory_location")
        t.check("equipButlersOutfitAndHeadInside-held", heldv == 5,
            "varb14283_holding_inventory_location = " .. tostring(heldv) .. " (Quest Helper inVilla = 5)")

        -- ================================================================
        -- Inside Villa Lucens
        -- ================================================================
        t.exec("goto-headInsideAndTalkToPatzi", t.player.goto_tile, 1446, 2935, 0)
        talk("headInsideAndTalkToPatzi", "doti_patzi_core", "npc:You made it in")
        t.expect("quest.stage.intros", t.quest.expect_stage("intros"))

        t.exec("goto-introduceYourselfToConstantinius", t.player.goto_tile, 1448, 2933, 0)
        talk("introduceYourselfToConstantinius", "doti_constantinius", "npc:So, this is grand isn't it?")
        talk("introduceYourselfToCozyac", "doti_cozyac", "player:Good day, sir.")
        talk("introduceYourselfToPavo", "doti_pavo", "npc:Excuse me! You there!")
        t.exec("goto-introduceYourselfToXocotla", t.player.goto_tile, 1441, 2931, 0)
        talk("introduceYourselfToXocotla", "doti_xocotla", "player:Good day to you.")
        t.exec("goto-returnToPatzi", t.player.goto_tile, 1446, 2935, 0)
        talk("returnToPatzi", "doti_patzi_core", "npc:I saw you make your way around the party!")
        t.expect("quest.stage.cellar", t.quest.expect_stage("cellar"))

        -- The wine cellar (entrance 1447,2938; landing beside the exit stairs 1446,9338).
        t.exec("goto-enterTheCellar", t.player.goto_tile, 1447, 2937, 0)
        t.exec("enterTheCellar", t.player.climb, { loc = "aldarin_cellar_entrance", op = 1, op_name = "Enter",
            at = { 1447, 2938, 0 }, dest = { 1446, 9338, 0 } })
        t.expect("quest.stage.wine", t.quest.expect_stage("wine"))
        t.exec("goto-getWine", t.player.goto_tile, 1440, 9314, 0)
        t.exec("getWine", t.player.click_loc, "doti_aldarin_red_amphora", 1)
        t.exec("getWine-dialog", t.chat.play, {
            "player:Pri...pum Re...",
            "player:The label's faded",
            "mesbox:startled by the sound of a glass shattering",
            "player:that's going to stain",
        })
        t.expect("quest.stage.body", t.quest.expect_stage("body"))

        t.exec("goto-investigateMan", t.player.goto_tile, 1454, 9325, 0)
        t.exec("investigateMan", t.player.talk_to, "doti_livius_dead", 1)
        t.exec("investigateMan-dialog", t.chat.play, {
            "player:Is everything okay?",
            "player:Hello?",
            "player:Have you had too much to drink?",
            "npc:You there! Don't move!",
            "player:What the...",
            "npc:You're coming with us.",
        })
        -- "Player is transported to a small room on the ground floor of the villa."
        t.exec("beInterrogatedByThePolice-open", t.chat.play, { "npc:It's always the butler, isn't it." })
        at_tile("beInterrogatedByThePolice-room", { 1438, 2938, 0 }, 0)
        t.exec("beInterrogatedByThePolice", t.chat.drain, { max_pages = 120 })
        t.ticks(3)
        t.expect("quest.stage.clues", t.quest.expect_stage("clues"))
        t.exec("beInterrogatedByThePolice-casefile", t.inv.await, "doti_casefile", 1, 6)
        at_tile("beInterrogatedByThePolice-released", { 1438, 2940, 0 }, 0)

        -- ================================================================
        -- Playing detective: the cellar first (Quest Helper order), then the guests.
        -- ================================================================
        t.exec("goto-enterTheCellarAgain", t.player.goto_tile, 1447, 2937, 0)
        t.exec("enterTheCellarAgain", t.player.climb, { loc = "aldarin_cellar_entrance", op = 1,
            op_name = "Enter", at = { 1447, 2938, 0 }, dest = { 1446, 9338, 0 } })
        local function clue(step, loc, stand, text, var)
            t.exec("goto-" .. step, t.player.goto_tile, stand[1], stand[2], 0)
            t.exec(step, t.player.click_loc, loc, 1)
            t.exec(step .. "-dialog", t.chat.play, { "player:" .. text })
            t.exec(step .. "-noted", t.var.await, var, 1, 6)
        end
        clue("investigateJug", "doti_clue1", { 1444, 9336 }, "I wonder why this jug", "varb11218_doti_clue1")
        clue("investigateSmallBoxInSouthRoom", "doti_clue4", { 1439, 9322 }, "It appears to be a box full",
            "varb11221_doti_clue4")
        clue("investigateBrokenStoolInSouthRoom", "doti_clue5", { 1446, 9316 }, "Why would Constantinius keep",
            "varb11222_doti_clue5")
        clue("investigateWineStorageInEastRoom", "doti_clue3", { 1451, 9334 }, "It's only half full",
            "varb11220_doti_clue3")
        clue("investigateBrokenPotteryInEastRoom", "doti_clue2", { 1455, 9328 }, "It looks like someone's dropped",
            "varb11219_doti_clue2")
        t.exec("goto-investigateLiviusInEastRoom", t.player.goto_tile, 1454, 9325, 0)
        t.exec("investigateLiviusInEastRoom", t.player.talk_to, "doti_livius_dead_named", 1)
        t.exec("investigateLiviusInEastRoom-dialog", t.chat.play, {
            "player:So what's got you then?",
            "mesbox:You inspect the body...",
            "player:A few cuts and scrapes",
            "player:Maybe there's something nearby",
        })
        t.exec("investigateLiviusInEastRoom-noted", t.var.await, "varb11223_doti_bodycheck", 1, 6)
        t.exec("goto-leaveCellar", t.player.goto_tile, 1446, 9338, 0)
        t.exec("leaveCellar", t.player.climb, { loc = "doti_cellar_stair_exit_villa", op = 1, op_name = "Climb-up",
            at = { 1447, 9338, 0 }, dest = { 1448, 2937, 0 } })

        t.exec("goto-investigateConstantinius", t.player.goto_tile, 1448, 2933, 0)
        talk("investigateConstantinius", "doti_constantinius", "npc:Oh poor Livius!")
        talk("investigateCozyac", "doti_cozyac", "player:You didn't happen to know Livius")
        talk("investigatePavo", "doti_pavo", "npc:it's a surprisingly sobering moment")
        t.exec("goto-investigateXocotla", t.player.goto_tile, 1441, 2931, 0)
        talk("investigateXocotla", "doti_xocotla", "npc:Quite an unfortunate turn of events")
        t.exec("goto-interrogatePatziAndAdala", t.player.goto_tile, 1446, 2935, 0)
        talk("interrogatePatziAndAdala", "doti_patzi_core", "npc:Oh, this is exciting isn't it!")
        t.exec("interrogatePatziAndAdala-noted", t.var.await, "varb11234_doti_investigated_patzi", 1, 6)

        t.exec("goto-returnToTheGuards", t.player.goto_tile, 1443, 2933, 0)
        talk("returnToTheGuards", "doti_stradius", "npc:How's your investigation going?",
            { "I think I've found everything there is to find so far. What now?" })
        t.expect("quest.stage.pockets", t.quest.expect_stage("pockets"))

        -- Pickpockets (op3 on the stage-26 forms). The success roll is random (doti_investigation.rs2
        -- [label,di_pickpocket]: stat_random 74/240); a clumsy bump is pressed again, at most 12 times.
        local function pickpocket(step, npc, item)
            local got = false
            for attempt = 1, 12 do
                local name = step .. (attempt > 1 and ("-retry" .. attempt) or "")
                t.exec(name, t.player.talk_to, npc, 3)
                local _, txt = t.chat.text()
                t.exec(name .. "-result", t.chat.drain, { max_pages = 4 })
                t.ticks(1)
                local _, n = t.inv.count(item)
                if (n or 0) >= 1 then
                    got = true
                    break
                end
                t.note(name .. ": " .. tostring(txt))
                t.ticks(2)
            end
            local _, n = t.inv.count(item)
            t.check(step .. "-item", got and (n or 0) == 1, item .. " in the backpack: " .. tostring(n))
        end
        t.exec("goto-pickpocketAdala", t.player.goto_tile, 1445, 2935, 0)
        pickpocket("pickpocketAdala", "doti_adala_mask_inside_pickpocket", "doti_labels")
        pickpocket("pickpocketCozyac", "doti_cozyac_pickpocket", "doti_letter")
        pickpocket("pickpocketPavo", "doti_pavo_pickpocket", "doti_flask")
        t.exec("goto-pickpocketXocotla", t.player.goto_tile, 1441, 2931, 0)
        pickpocket("pickpocketXocotla", "doti_xocotla_pickpocket", "doti_contract")

        local function inspect(step, item, text, var)
            t.exec(step, t.player.inv_op, item, 1)
            t.exec(step .. "-box", t.chat.drain, { max_pages = 3 })
            t.exec(step .. "-noted", t.var.await, var, 1, 6)
        end
        inspect("inspectWineLabels", "doti_labels", "labels", "varb11236_doti_investigated_labels")
        inspect("inspectThreateningNote", "doti_letter", "letter", "varb11237_doti_investigated_letter")
        inspect("inspectDrinkingFlask", "doti_flask", "flask", "varb11235_doti_investigated_flask")
        inspect("inspectShippingContract", "doti_contract", "contract", "varb11238_doti_investigated_contract")

        t.exec("goto-returnStolenItemsToTheGuards", t.player.goto_tile, 1443, 2933, 0)
        talk("returnStolenItemsToTheGuards", "doti_stradius", "player:I've had a little look through some pockets.",
            { "I want to review the evidence a bit more." })
        t.expect("quest.stage.accuse", t.quest.expect_stage("accuse"))
        t.exec("returnStolenItemsToTheGuards-handed", t.inv.expect_absent, "doti_labels")
        talk("talkToGuardsAgainToTellThemYouAreReady", "doti_stradius", "npc:So what are these labels you found?",
            { "I'm ready." })
        t.expect("quest.stage.suspects", t.quest.expect_stage("suspects"))
        at_tile("talkToGuardsAgainToTellThemYouAreReady-upstairs", { 1444, 2932, 2 }, 0)

        -- The top floor: question everyone, accuse Adala, her bounded fight, her confession.
        t.ticks(3)
        talk("interrogateConstantiniusAgain", "doti_constantinius", "player:Constantinius, you knew Livius well",
            { "No." })
        talk("interrogateXocotlaAgain", "doti_xocotla", "player:Xocotla, you've never had any dealings",
            { "No." })
        talk("interrogateCozyacAgain", "doti_cozyac", "player:Cozyac, did you not claim", { "No." })
        talk("interrogatePavoAgain", "doti_pavo", "player:Pavo, you and Livius were drinking partners",
            { "No." })
        talk("accuseAdala", "doti_adala_mask_inside", "player:Patzi, Adala. The two of you weren't even invited",
            { "Accuse Adala." })
        t.exec("fightAdala.present", t.npc.await_present, "doti_adala_boss", 10, 8)
        local _, hp0 = t.skill.read("hitpoints")
        local hp_max = hp0 and hp0.base_level or 40
        t.exec("fightAdala", t.player.attack, "doti_adala_boss", 2, 10)
        local hp_low = hp0 and hp0.level or 40
        local fought = 0
        for i = 1, 400 do
            t.ticks(1)
            fought = i
            local _, hp = t.skill.read("hitpoints")
            if hp and hp.level and hp.level < hp_low then hp_low = hp.level end
            local _, st = t.quest.stage()
            if st ~= nil and st >= 32 then break end
            if i % 25 == 0 then
                local br = t.npc.nearest("doti_adala_boss", 10)
                if br == "ok" then
                    t.player.attack("doti_adala_boss", 2, 4)
                end
            end
        end
        local _, outcome = t.var.varbit("varb11229_doti_adala_fight_outcome")
        t.check("fightAdala.won", outcome == 1,
            "doti_adala_fight_outcome = " .. tostring(outcome) .. " after " .. fought .. " tick(s) (1 = she yielded)")
        t.check("fightAdala.margin", hp_low * 4 >= hp_max,
            "lowest hp " .. tostring(hp_low) .. "/" .. tostring(hp_max)
            .. " (>= a quarter; equipment-free fight inside the villa, no food can be carried -- the Head Butler holds it)")
        t.expect("quest.stage.adala", t.quest.expect_stage("adala"))
        t.ticks(3)
        talk("getAdalasConfession", "doti_adala_mask_inside_post", "npc:Stop! You're too good for me! You win!")
        t.expect("quest.stage.guards2", t.quest.expect_stage("guards2"))
        talk("talkToGuardsAboutAdala", "doti_stradius", "npc:Does this mean we can get on with tonight's play?")
        t.expect("quest.stage.theatre", t.quest.expect_stage("theatre"))

        -- ================================================================
        -- The show must go on: down the villa stairs, the back way round the cliffs.
        -- ================================================================
        t.exec("headDownFromTopFloor", t.player.climb, { loc = "doti_villa_stair_invisible", op = 1,
            op_name = "Climb-down", at = { 1445, 2938, 2 }, dest = { 1445, 2937, 1 } })
        t.exec("headDownFromMiddleFloor", t.player.climb, { loc = "doti_villa_stair_invisible", op = 1,
            op_name = "Climb-down", at = { 1442, 2935, 1 }, dest = { 1442, 2934, 0 } })
        t.exec("goto-climbFirstLooseRocksToTheatre", t.player.goto_tile, 1468, 2918, 0)
        t.exec("climbFirstLooseRocksToTheatre", t.player.cross_trap, { loc = "doti_agility_challenge_a",
            op_name = "Navigate", at = { 1469, 2918, 0 }, src = { 1468, 2918 }, dest = { 1471, 2918 }, attempts = 2 })
        t.expect("quest.stage.theatre2", t.quest.expect_stage("theatre2"))
        t.exec("walk-plateau", t.player.walk_to, 1474, 2922, 15)
        t.exec("climbSecondLooseRocksToTheatre", t.player.cross_trap, { loc = "doti_agility_challenge_c",
            op_name = "Navigate", at = { 1474, 2923, 0 }, src = { 1474, 2922 }, dest = { 1474, 2924 }, attempts = 2 })
        talk("talkToGuardsAtTheatre", "doti_stradius", "npc:Ah, there you are")
        t.exec("talkToGuardsAtTheatre-noted", t.var.await, "varb11249_doti_backstage_intro", 1, 6)

        t.exec("goto-enterBackstage", t.player.goto_tile, 1478, 2926, 0)
        t.exec("enterBackstage", t.player.climb, { loc = "aldarin_backstage_entrance", op = 1, op_name = "Enter",
            at = { 1477, 2927, 0 }, dest = { 1468, 9329, 0 } })
        t.exec("costumer.present", t.npc.await_present, "doti_costumer", 10, 8)
        talk("talkToCostumer", "doti_costumer_vis", "player:Hello there.", { "I'm going to get looking around." })
        t.expect("quest.stage.cellar2", t.quest.expect_stage("cellar2"))

        local function search(step, loc, stand, var)
            t.exec("goto-" .. step, t.player.goto_tile, stand[1], stand[2], 0)
            t.exec(step, t.player.click_loc, loc, 1)
            t.exec(step .. "-box", t.chat.drain, { max_pages = 3 })
            t.exec(step .. "-noted", t.var.await, var, 1, 6)
        end
        search("searchCrateNextToStairs", "doti_poison_crate", { 1468, 9330 }, "varb11251_doti_poison_clue")
        search("searchBookshelf", "doti_changing_bookshelf_in", { 1462, 9331 }, "varb11250_doti_bookshelf_clue")
        search("searchCostumeRack", "doti_damaged_costume", { 1463, 9337 }, "varb11252_doti_clothing_clue")

        t.exec("goto-talkToCostumerAgain", t.player.goto_tile, 1465, 9330, 0)
        talk("talkToCostumerAgain", "doti_costumer_vis", nil, {
            "What can you tell me about the actors?",
            "What's the crate of poison for?",
            "It seems one of the costumes has a stain on it.",
            "Did you know there was a hidden passage in here?",
            "I'm going to get looking around.",
        })
        t.expect("quest.stage.snitch", t.quest.expect_stage("snitch"))

        t.exec("goto-climbUpFromTheatreCellar", t.player.goto_tile, 1468, 9328, 0)
        t.exec("climbUpFromTheatreCellar", t.player.climb, { loc = "doti_cellar_stair_exit_backstage", op = 1,
            op_name = "Climb-up", at = { 1469, 9327, 0 }, dest = { 1478, 2926, 0 } })
        talk("speakToGuards", "doti_stradius", "npc:The play is well underway",
            { "More options...", "Naiatli." })
        t.expect("quest.stage.naiatli", t.quest.expect_stage("naiatli"))
        at_tile("speakToGuards-onStage", { 1467, 2932, 0 }, 0)
        -- Off the stage by Hutza's Leave (transcript "The Stage: Talking to Hutza") and back on through
        -- Stradius outside -- Quest Helper's 42 step for a player outside the theatre.
        t.exec("hutza.present", t.npc.await_present, "doti_hutza_escape", 10, 8)
        talk("leaveStage", "doti_hutza_escape", "npc:Did you want to leave?", { "Yes." })
        at_tile("leaveStage-outside", { 1474, 2925, 0 }, 0)
        talk("talkToStradiusToEnterTheTheatre", "doti_stradius", "player:Okay, I'm going on stage.")
        at_tile("talkToStradiusToEnterTheTheatre-onStage", { 1467, 2932, 0 }, 0)

        -- The stage (doti_theatre.rs2 [label,di_naiatli]; damage 3 / 5 / 8 / 4 from the transcript).
        t.exec("naiatli.present", t.npc.await_present, "doti_naiatli", 10, 8)
        talk("confrontNaiatli", "doti_naiatli", "npc:At last, the vile villain shows themselves!")
        t.expect("quest.stage.onstage", t.quest.expect_stage("onstage"))
        t.exec("confrontNaiatli-regroup", t.var.await, "varb11258_doti_final_fight", 1, 6)
        t.ticks(4)
        talk("confrontNaiatli-trap", "doti_naiatli", "player:Face me!")
        t.exec("attackClodius.present", t.npc.await_present, "doti_backupactor", 10, 8)
        t.exec("attackClodius", t.player.press, "doti_backupactor", 1, 8)
        t.exec("attackClodius-struck", t.var.await, "varb11258_doti_final_fight", 3, 10)
        t.ticks(3)
        t.exec("killNaiatli", t.player.talk_to, "doti_naiatli", 3)
        t.exec("killNaiatli-dialog", t.chat.drain, { max_pages = 10 })
        t.exec("killNaiatli-fled", t.var.await, "varb11258_doti_final_fight", 4, 8)
        t.ticks(4)
        t.exec("killNaiatli-regrouped", t.npc.await_present, "doti_naiatli", 15, 10)
        t.exec("killNaiatli-again", t.player.talk_to, "doti_naiatli", 3)
        t.exec("killNaiatli-again-dialog", t.chat.drain, { max_pages = 10 })
        t.exec("killNaiatli-down", t.var.await, "varb11258_doti_final_fight", 6, 8)
        talk("talkToNaiatli", "doti_naiatli", "player:Naiatli, I know you killed Livius. Why?")
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        at_tile("talkToNaiatli-backInVilla", { 1443, 2933, 0 }, 0)

        -- Reward snapshot before the hand-in.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        t.exec("talkToGuardsToFinishTheQuest", t.player.talk_to, "doti_stradius", 1)
        t.exec("talkToGuardsToFinishTheQuest-dialog", t.chat.play, {
            "player:Stradius, Hutza, it's been a surprising pleasure",
            "npc:I can't believe it was all an accident",
            "npc:you've done well",
            "npc:To be fair to us",
            "player:I suppose I did, but no harm done",
            "npc:You win some and you lose some",
        })
        t.ticks(3)

        t.quest.expect_complete()

        for _, rw in ipairs({ { "thieving", 10000 }, { "agility", 7500 }, { "crafting", 5000 } }) do
            local rr, rd = t.skill.expect_gain(rw[1], rw[2], reward_before)
            t.check("reward." .. rw[1], rr == "ok", rd)
        end

        t.finish(0)
    end,
}
