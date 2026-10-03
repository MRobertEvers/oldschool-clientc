-- Ghosts Ahoy. Authored against OSRS-Content quest_ghostsahoy/scripts/
-- ahoy_hub.rs2, ahoy_book.rs2, ahoy_manual.rs2, ahoy_robes.rs2, ahoy_shared.rs2
-- and configs/ghostsahoy.constant, read in full (2026-09-28). Guide:
-- docs/quests/ghosts_ahoy.md (Gate D closed, lobster/dye/Rune-Draw all real).
--
-- Fixture start: fresh_lumbridge.ini, beside Hans at 3206,3233,0.
--
-- Coordinates below are decoded straight from ghostsahoy.constant's
-- ^ahoy_*_coord fields (plane_zoneX_zoneZ_localX_localZ -> x=zoneX*64+localX,
-- z=zoneZ*64+localZ) or read off m56_55.jl2 for the rock-jump chain and
-- gangplank locs.

return {
    id = "ghostsahoy",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000, -- Rune-Draw: ~12 ticks a game, up to 100 games
    setup = {
        "::clearinv",
        "::ghostsahoy", -- resets ahoy_*, completes prieststart/priestperil, worn amulet + 40 ecto-tokens
        "::give amulet_of_ghostspeak 1", -- spare, handed to the Crone for enchanting
        "::give ectotoken 100",
        "::give silk 1",
        "::give costumeneedle 1", -- covers both needle and thread for the boat repair
        "::give knife 1",
        "::give spade 1",
        "::give oak_longbow 1",
        "::give bucket_ectoplasm 1", -- dyes the bedsheet green
        "::give bucket_milk 1", -- nettle tea
        "::give bowl_water 1", -- nettle tea (a filled bowl is a brought-along supply, not quest deliverable)
        "::give leather_gloves 1", -- worn before picking nettles
        "::give reddye 1",
        "::give bluedye 1",
        "::give yellowdye 1",
        "::give orangedye 1",
        "::give greendye 1",
        "::give purpledye 1",
        "::give coins 1000", -- Rune-Draw stakes, 25/game
        "::give logs 1",
        "::give tinderbox 1", -- lights the fire the nettle-water is boiled over
        "::give rune_scimitar 1", -- worn for the giant lobster
        "::setlevel agility 25",
        "::setlevel cooking 20",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 40",
        "::setlevel hitpoints 40",
        "::complete quest_priestinperil", -- quest_cheat.rs2's arm is quest_priestinperil; quest_priestperil / quest_priest had no arm and did nothing
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb217_ahoy_questvar",
            constants = {
                not_started = 0,
                talked_velorina = 1,
                talked_necrovarus = 2,
                told_of_crone = 3,
                gathering_items = 4,
                need_amulet = 5,
                amulet_enchanted = 6,
                necrovarus_defeated = 7,
                complete = 8,
            },
            row = "quest_ghostsahoy",
            display = "Ghosts Ahoy",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- Wear the tools that must be worn: frees two backpack slots and
        -- satisfies the nettle-picking glove check and arms the lobster fight.
        t.exec("equip.gloves", t.player.equip, "leather_gloves")
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")

        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        -- Port Phasmatys' west Energy Barrier: maps/m57_54.jl2 row
        -- `0 11 52: 57722 10 3` -- ahoy_town_barrier_multi at 3659,3508,
        -- facing south, spanning 3659-3660, set into the town wall along
        -- z=3508 with a ghost guard on each wall tile beside it. The town
        -- (Velorina, the inn, Gravingas, the docks) is SOUTH of it, the
        -- Ectofuntus NORTH. Every crossing is the barrier itself (Quest
        -- Helper's enterPhas* ObjectSteps at 3660,3508), never a ::goto:
        -- op1 "Pass" is the ghost guard's toll talk (wiki Transcript:Ghost
        -- guard), op4 "Pay-toll(2-Ecto)" pays without it, and from inside
        -- either op lets you out free (ahoy_hub.rs2 [label,ahoy_barrier_pass]).
        local function barrier(step, op, dialog, inward)
            if inward then
                t.exec("goto-" .. step, t.player.goto_tile, 3660, 3510, 0)
            else
                t.exec("goto-" .. step, t.player.goto_tile, 3660, 3506, 0)
            end
            t.exec(step, t.player.click_loc, "ahoy_town_barrier_multi", op, { at = { 3659, 3508 } })
            if dialog ~= nil then
                t.exec(step .. "-dialog", t.chat.play, dialog)
            end
            -- p_teleport lands a tick behind the chat line.
            local through, here = false, nil
            for _ = 1, 4 do
                local tr, tile = t.world.tile()
                here = tile
                if tr == "ok" and tile ~= nil
                    and ((inward and tile.z < 3508) or ((not inward) and tile.z > 3508)) then
                    through = true
                    break
                end
                t.ticks(1)
            end
            t.check(step .. ".through", through, (inward and "inside" or "outside") .. " Port Phasmatys at "
                .. tostring(here and here.x) .. "," .. tostring(here and here.z))
        end
        local guard_toll = {
            "npc:All visitors to Port Phasmatys must pay",
            "choose:I would like to enter Port Phasmatys - here's 2 Ectotokens.",
            "player:I would like to enter Port Phasmatys",
        }

        -- ---------------------------------------------------------------
        -- talkToVelorina: accept the quest.
        -- ---------------------------------------------------------------
        barrier("enterPhas", 1, guard_toll, true)
        t.exec("goto-talkToVelorina", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorina", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorina-dialog", t.chat.play, {
            "player:Why, what is the matter?",
            "npc:I am trapped here, unable to pass on",
            "choose:Yes, I will help.",
            "player:Yes, I will help.",
            "npc:Will you speak to Necrovarus for me",
            "player:Yes.",
        })
        t.exec("quest.stage.talked_velorina", t.quest.expect_stage, "talked_velorina")

        -- talkToNecrovarus: refused.
        barrier("exitPhas", 1, nil, false)
        t.exec("goto-talkToNecrovarus", t.player.goto_tile, 3660, 3516, 0)
        t.exec("talkToNecrovarus", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("talkToNecrovarus-dialog", t.chat.play, {
            "player:Velorina asked me to speak to you",
            "npc:Pass on? Never!",
        })
        t.exec("quest.stage.talked_necrovarus", t.quest.expect_stage, "talked_necrovarus")

        -- talkToVelorinaAfterNecro: sent to the Old Crone.
        -- The toll by op4 Pay-toll this time: no guard talk, same charge.
        local tok_res0, tok_before = t.inv.count("ectotoken")
        barrier("enterPhasAfterNecro", 4, nil, true)
        local tok_res1, tok_after = t.inv.count("ectotoken")
        t.check("enterPhasAfterNecro.toll", tok_res0 == "ok" and tok_res1 == "ok"
            and tok_before ~= nil and tok_after ~= nil and tok_before - tok_after == 2,
            "ecto-tokens " .. tostring(tok_before) .. " -> " .. tostring(tok_after))
        t.exec("goto-talkToVelorinaAfterNecro", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorinaAfterNecro", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorinaAfterNecro-dialog", t.chat.play, {
            "player:Necrovarus refused to let anyone pass on.",
            "npc:There is an old woman who once served him",
        })
        t.exec("quest.stage.told_of_crone", t.quest.expect_stage, "told_of_crone")

        -- talkToCrone x1: cup handed over.
        barrier("exitPhasForCrone", 1, nil, false)
        t.exec("goto-talkToCrone", t.player.goto_tile, 3461, 3558, 0)
        t.exec("talkToCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCrone-dialog", t.chat.play, {
            "player:I'm here about Necrovarus.",
            "npc:Bring me a cup, with a little milk.",
        })
        t.exec("inv.cup", t.inv.expect_has, "chinacup_empty", 1)

        -- Pick nettles (m56_54.jl2 local 37,55 -- the nearest clump to the
        -- Ectofuntus/Crone area the map actually has), steep, boil, pour,
        -- add milk.
        t.exec("goto-pickNettles", t.player.goto_tile, 3621, 3511, 0)
        t.exec("pickNettles", t.player.click_loc, "nettles", 1)
        -- click_loc's `ok` is the server's sentence, not the container update
        -- (QUEST_AUTHORING trap 24) -- wait for the backpack, not a bare read.
        t.exec("inv.nettles_picked", t.inv.await, "nettles_picked", 1, 5)
        t.exec("nettles.steep", t.player.use_item_on_item, "bowl_water", "nettles_picked")
        t.exec("inv.nettlewater", t.inv.expect_has, "bowl_nettlewater", 1)

        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        -- Firemaking is a self-continuing action (p_opobj(4) re-arms the
        -- attempt every action cycle until the stat_random roll lands) --
        -- poll a few ticks for the resulting `fire` loc rather than a bare
        -- read right after the first "You attempt to light the logs." page.
        local fire, fire_d = "not_found", nil
        local fire_tries = 0
        -- 40 ticks, not 8: the attempt re-rolls every cycle and a low
        -- Firemaking level can miss for many in a row (seam27 s27gh_full1:
        -- 8 ticks ran out once the barrier crossings shifted the rolls).
        while fire ~= "ok" and fire_tries < 40 do
            fire, fire_d = t.world.loc_near("fire", 10)
            if fire ~= "ok" then
                t.ticks(1)
                fire_tries = fire_tries + 1
            end
        end
        t.step("world.fire_near", fire == "ok" and "PASS" or "FAIL",
            fire == "ok" and ("fire at " .. tostring(fire_d.tile_x) .. "," .. tostring(fire_d.tile_z)) or tostring(fire_d))
        t.exec("useTeaOnFire", t.player.use_on, "bowl_nettlewater", fire_d)
        t.exec("inv.nettletea", t.inv.expect_has, "bowl_nettletea", 1)

        t.exec("useTeaOnCup", t.player.use_item_on_item, "chinacup_empty", "bowl_nettletea")
        t.exec("inv.tea_in_cup", t.inv.expect_has, "chinacup_of_nettletea", 1)
        t.exec("useMilkOnTea", t.player.use_item_on_item, "chinacup_of_nettletea", "bucket_milk")
        t.exec("inv.milky_tea", t.inv.expect_has, "chinacup_of_nettletea_milky", 1)

        -- talkToCroneAgainForShip: tea delivered, toy boat handed over,
        -- questvar advances to gathering_items.
        t.exec("goto-talkToCroneAgainForShip", t.player.goto_tile, 3461, 3558, 0)
        t.exec("talkToCroneAgainForShip", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToCroneAgainForShip-dialog", t.chat.play, {
            "player:I'm here about Necrovarus.",
            "npc:It all comes back to me now.",
            "npc:you'll need three things",
            "choose:I'll get started.",
        })
        t.exec("quest.stage.gathering_items", t.quest.expect_stage, "gathering_items")
        t.exec("inv.toyboat", t.inv.expect_has, "ahoy_toy_boat", 1)

        -- =================================================================
        -- Robes branch: bedsheet, petition (10 signatures), bone key,
        -- harbour door, coffin.
        -- =================================================================
        barrier("enterPhasForRobe", 1, guard_toll, true)
        t.exec("goto-talkToInnkeeper", t.player.goto_tile, 3681, 3496, 0)
        t.exec("talkToInnkeeper", t.player.talk_to, "ahoy_ghost_innkeeper", 1)
        t.exec("talkToInnkeeper-dialog", t.chat.play, {
            "player:Do you have any jobs I can do?",
            "npc:I've a spare bedsheet",
        })
        t.exec("inv.bedsheet", t.inv.expect_has, "ahoy_bedsheet", 1)

        t.exec("useSlimeOnSheet", t.player.use_item_on_item, "bucket_ectoplasm", "ahoy_bedsheet")
        t.exec("inv.bedsheetgreen", t.inv.expect_has, "ahoy_bedsheetgreen", 1)
        t.exec("equip.bedsheet", t.player.equip, "ahoy_bedsheetgreen")

        t.exec("goto-talkToGravingas", t.player.goto_tile, 3660, 3499, 0)
        t.exec("talkToGravingas", t.player.talk_to, "protester_ghostspeak_multi", 1)
        t.exec("talkToGravingas-dialog", t.chat.play, {
            "player:I've heard Velorina's sad story",
            "npc:Take this petition and get the townsfolk to sign it",
        })
        t.exec("inv.petition", t.inv.expect_has, "ahoy_petition", 1)

        -- talkToVillagers x10: signaturecounter 1 -> 11 (10 successful signs).
        t.exec("goto-talkToVillagers", t.player.goto_tile, 3661, 3497, 0)
        local sign_n = 0
        while sign_n < 10 do
            sign_n = sign_n + 1
            local vr, vd = t.player.talk_to("ahoy_ghost_villager", 1)
            t.step("talkToVillagers." .. sign_n, vr == "ok" and "PASS" or "FAIL", vd)
            if vr ~= "ok" then
                t.blocked("talkToVillagers: ahoy_ghost_villager talk_to answered " .. tostring(vr) .. " -- " .. tostring(vd))
                return
            end
            local pr, pd = t.chat.play({
                "player:Would you sign this petition",
                "npc:Why, of course.",
            })
            t.step("talkToVillagers.dialog." .. sign_n, pr == "ok" and "PASS" or "FAIL", pd)
            if pr ~= "ok" then
                t.blocked("talkToVillagers: signing dialog #" .. sign_n .. " answered " .. tostring(pr) .. " -- " .. tostring(pd))
                return
            end
        end
        local sig_res, sig_val = t.var.server("varb209_ahoy_signaturecounter")
        t.check("petition.full", sig_res == "ok" and sig_val ~= nil and sig_val >= 11,
            "ahoy_signaturecounter = " .. tostring(sig_val) .. " (" .. tostring(sig_res) .. ")")

        -- showPetitionToNecro: ashes, bone key drops on the ground.
        barrier("exitPhasForNecro", 1, nil, false)
        t.exec("goto-showPetitionToNecro", t.player.goto_tile, 3660, 3516, 0)
        t.exec("showPetitionToNecro", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("showPetitionToNecro-dialog", t.chat.play, {
            "player:The townsfolk have signed this petition",
            "npc:How DARE you incite my flock against me!",
        })
        -- click_obj answers `ok` with a NIL detail (QUEST_AUTHORING trap 12/
        -- section 8's hollow list) -- call it directly, never through
        -- t.exec, and it already waits for the backpack count to rise.
        local key_res, key_detail = t.player.click_obj("ahoy_bone_key")
        t.step("takeKey", key_res == "ok" and "PASS" or "FAIL", tostring(key_detail))
        t.exec("inv.bonekey", t.inv.expect_has, "ahoy_bone_key", 1)

        -- useKeyOnDoor + takeRobes: upstairs at the Ectofuntus.
        t.exec("goto-useKeyOnDoor", t.player.goto_tile, 3660, 3514, 1)
        t.exec("useKeyOnDoor", t.player.click_loc, "ahoy_harbour_door", 1)
        t.exec("takeRobes", t.player.click_loc, "ahoy_coffin", 1)
        -- click_loc's `ok` is the server's sentence, not the container
        -- update (trap 24) -- confirmed live 2026-09-28: the chat log showed
        -- "You search the coffin and find Necrovarus's own magical robes."
        -- on the SAME row that a bare expect_has read straight after called
        -- absent. Poll instead of a bare read.
        t.exec("inv.robes", t.inv.await, "ahoy_robes_of_necrovarus", 1, 5)

        -- =================================================================
        -- Manual branch: Ak-Haranu, Robin's Rune-Draw (real game, drawn
        -- until Robin's debt reaches 100), manual.
        -- =================================================================
        barrier("enterPhasForManual", 4, nil, true)
        t.exec("goto-bringBowToAkHaranu", t.player.goto_tile, 3689, 3499, 0)
        t.exec("bringBowToAkHaranu", t.player.talk_to, "ahoy_akharanu_multi", 1)
        t.exec("bringBowToAkHaranu-dialog", t.chat.play, {
            "player:I have an oak longbow.",
            "npc:Okay, wait here",
        })
        local bow_res0, bow_val0 = t.var.server("varb212_ahoy_subquest_bow")
        t.check("bow.talked_akharanu", bow_res0 == "ok" and bow_val0 ~= nil and bow_val0 >= 1,
            "ahoy_subquest_bow = " .. tostring(bow_val0) .. " (" .. tostring(bow_res0) .. ")")

        t.exec("goto-talkToRobin", t.player.goto_tile, 3672, 3491, 0)

        -- Robin's Rune-Draw, played for real until he owes 100 and signs the
        -- bow (ahoy_manual.rs2 [proc,ahoy_runedraw_round]).  t.game.runedraw
        -- plays ONE game from the page talk_to leaves up and picks every
        -- Draw/Hold by the exact best reply to Robin's fixed rule: +0.094 a
        -- game, 15.4 games to a debt of 100 on average, P(>100 games) 0.0002
        -- (QD.game banner, chat.lua).  The cap is that tail, not a hope.
        local rd_games = 0
        while true do
            local bow_res, bow_val = t.var.server("varb212_ahoy_subquest_bow")
            if bow_res == "ok" and bow_val ~= nil and bow_val >= 2 then
                break
            end
            rd_games = rd_games + 1
            if rd_games > 100 then
                t.blocked("Rune-Draw: Robin's debt never reached 100 in 100 games under the exact policy (P = 0.0002)")
                return
            end
            local tr, td = t.player.talk_to("ahoy_robin", 1)
            t.step("talkToRobin." .. rd_games, tr == "ok" and "PASS" or "FAIL", td)
            if tr ~= "ok" then
                t.blocked("talkToRobin: talk_to answered " .. tostring(tr) .. " -- " .. tostring(td))
                return
            end
            local gr, game = t.game.runedraw()
            t.step("runedraw.game." .. rd_games, gr == "ok" and "PASS" or "FAIL",
                gr == "ok" and game.text or tostring(game))
            if gr ~= "ok" then
                t.blocked("Rune-Draw: game " .. rd_games .. " answered " .. tostring(gr) .. " -- " .. tostring(game))
                return
            end
            local debt_res, debt_val = t.quest._read_content("varp7172_ahoy_robin_debt")
            t.step("runedraw.debt." .. rd_games, debt_res == "ok" and "PASS" or "FAIL",
                "ahoy_robin_debt = " .. tostring(debt_val) .. " (" .. tostring(debt_res) .. ") after game " .. rd_games)
        end
        t.exec("quest.stage.bow_signed", t.var.expect, "varb212_ahoy_subquest_bow", 2)

        t.exec("goto-bringSignedBow", t.player.goto_tile, 3689, 3499, 0)
        t.exec("bringSignedBow", t.player.talk_to, "ahoy_akharanu_multi", 1)
        t.exec("bringSignedBow-dialog", t.chat.play, {
            "player:Here's your signed oak longbow.",
            "npc:As promised, here is the translation manual.",
        })
        t.exec("inv.manual", t.inv.expect_has, "ahoy_translation_manual", 1)

        -- =================================================================
        -- Book branch: repair the toy boat, dye its flag, trade for the
        -- chest key, open the captain's chest (scrap 1), jump the rocks to
        -- the third chest (scrap 2), fight the giant lobster and loot its
        -- chest (scrap 3), combine the map, sail to Dragontooth Island, dig.
        -- =================================================================
        t.exec("repairShip", t.player.use_item_on_item, "silk", "ahoy_toy_boat")
        t.exec("inv.repaired", t.inv.expect_has, "ahoy_toy_boat_repaired", 1)

        -- Onto the wreck: the ship is beached and walkable, no gangplank
        -- needed for the deck itself (generic ladder system, section 2/70).
        barrier("exitPhasForWreck", 1, nil, false)
        t.exec("goto-shipDeck", t.player.goto_tile, 3619, 3545, 1)

        t.exec("goto-checkMast", t.player.goto_tile, 3618, 3542, 2)
        t.exec("checkMast", t.player.click_loc, "ahoy_mast", 1)
        local top_res, top_val = t.quest._read_content("varp6828_ahoy_flag_top")
        local bot_res, bot_val = t.quest._read_content("varp6829_ahoy_flag_bottom")
        local sku_res, sku_val = t.quest._read_content("varp6830_ahoy_flag_skull")
        local mast_ok = top_res == "ok" and bot_res == "ok" and sku_res == "ok"
        t.step("mast.target", mast_ok and "PASS" or "FAIL",
            "top=" .. tostring(top_val) .. " bottom=" .. tostring(bot_val) .. " skull=" .. tostring(sku_val))

        local ahoy_colour_dye = { "reddye", "bluedye", "yellowdye", "orangedye", "greendye", "purpledye" }
        local ahoy_colour_word = { "red", "blue", "yellow", "orange", "green", "purple" }

        -- The mast's three colours are rolled per player and may repeat
        -- (top=5 bottom=1 skull=1 in s24gh_run1), and setup brings ONE of
        -- each dye: bring the repeats the way a player buys a second pot.
        local dye_need = {}
        for _, v in ipairs({ top_val, bot_val, sku_val }) do
            dye_need[v] = (dye_need[v] or 0) + 1
        end
        for v, n in pairs(dye_need) do
            if n > 1 then
                t.cheat("::give " .. ahoy_colour_dye[v] .. " " .. (n - 1))
                t.exec("supply.dye." .. ahoy_colour_dye[v], t.inv.await, ahoy_colour_dye[v], n, 5)
            end
        end

        t.exec("dyeFlags.top", t.player.use_item_on_item, ahoy_colour_dye[top_val], "ahoy_toy_boat_repaired")
        t.exec("dyeFlags.top.dialog", t.chat.play, {
            "choose:Top half",
            "mesbox:You dye the top of the flag " .. ahoy_colour_word[top_val],
        })
        t.exec("dyeFlags.bottom", t.player.use_item_on_item, ahoy_colour_dye[bot_val], "ahoy_toy_boat_repaired")
        t.exec("dyeFlags.bottom.dialog", t.chat.play, {
            "choose:Bottom half",
            "mesbox:You dye the bottom of the flag " .. ahoy_colour_word[bot_val],
        })
        t.exec("dyeFlags.skull", t.player.use_item_on_item, ahoy_colour_dye[sku_val], "ahoy_toy_boat_repaired")
        t.exec("dyeFlags.skull.dialog", t.chat.play, {
            "choose:Skull emblem",
            "mesbox:You dye the skull emblem " .. ahoy_colour_word[sku_val],
        })

        t.exec("goto-tradeOldMan", t.player.goto_tile, 3617, 3544, 1)
        t.exec("tradeOldMan", t.player.talk_to, "ahoy_oldman", 1)
        t.exec("tradeOldMan-dialog", t.chat.play, {
            "player:Is this your toy boat?",
            "npc:Here -- take the key to my chest",
        })
        t.exec("inv.chestkey", t.inv.expect_has, "ahoy_chest_key", 1)

        t.exec("goto-openCaptainChest", t.player.goto_tile, 3619, 3545, 1)
        local chest_target, chest_target_r = t.player.by_symbol("loc", "ahoy_chest_locked")
        t.step("world.chest_locked_symbol", chest_target ~= nil and "PASS" or "FAIL", tostring(chest_target_r))
        -- [oplocu,ahoy_chest_locked] is the unlock trigger (last_useitem =
        -- ahoy_chest_key), a USE-ON, never a plain click_loc.
        t.exec("useKeyOnChest", t.player.use_on, "ahoy_chest_key", chest_target)
        t.exec("openCaptainChest", t.player.click_loc, "ahoy_chest_locked", 1)
        t.exec("inv.scrap1", t.inv.await, "ahoy_map_scrap_1", 1, 5)

        -- Rocks: cross the gangplank onto them, then jump the chain of
        -- invisible stepping stones to the third chest (agility 25 checked
        -- live; every click lands regardless of the roll, per the source).
        -- The Captain's Room is closed by a real cache door (ahoy_harbour_door
        -- at 3615,3543,1, m56_55.jl2 `1 31 23: 5244 0 2`): open it on the way
        -- to the plank. TRAVEL (Quest Helper has no door step here).
        t.exec("openCaptainRoomDoor", t.player.click_loc, "ahoy_harbour_door", 1)
        t.exec("goAcrossPlank", t.player.click_loc, "ahoy_gangplank_shipwreck_on", 1)
        -- openThirdChest: jump the rock chain (Quest Helper setLinePoints
        -- 3604,3550 -> ... -> 3605,3564; the copies are m56_55.jl2's 16115
        -- rows). Each rock is named by its tile: the nearest copy is the one
        -- underfoot.
        local rocks_out = {
            { 3602, 3550 }, { 3599, 3552 }, { 3597, 3552 }, { 3595, 3554 }, { 3595, 3556 },
            { 3597, 3559 }, { 3597, 3561 }, { 3599, 3564 }, { 3601, 3564 },
        }
        local function jump_chain(label, chain)
            for i, rock in ipairs(chain) do
                local name = label .. "." .. i
                t.exec(name, t.player.click_loc, "ahoy_rock_invisible", 1, { at = { rock[1], rock[2] } })
                -- p_teleport(loc_coord) lands a tick behind the jump's chat line.
                local landed = false
                for _ = 1, 4 do
                    local tr, tile = t.world.tile()
                    if tr == "ok" and tile ~= nil and tile.x == rock[1] and tile.z == rock[2] then
                        landed = true
                        break
                    end
                    t.ticks(1)
                end
                t.check(name .. ".landed", landed, "on the rock at " .. rock[1] .. "," .. rock[2])
                if not landed then
                    return false
                end
            end
            return true
        end
        if not jump_chain("rockjump", rocks_out) then
            t.blocked("rockjump: a jump did not land on its named rock")
            return
        end
        t.exec("openThirdChest", t.player.click_loc, "ahoy_chest_closed", 1)
        t.exec("inv.scrap2", t.inv.await, "ahoy_map_scrap_2", 1, 5)

        -- Back the same way: the chain in reverse to the plank's rock.
        local rocks_back = {
            { 3599, 3564 }, { 3597, 3561 }, { 3597, 3559 }, { 3595, 3556 }, { 3595, 3554 },
            { 3597, 3552 }, { 3599, 3552 }, { 3602, 3550 }, { 3604, 3550 },
        }
        if not jump_chain("rockback", rocks_back) then
            t.blocked("rockback: a jump did not land on its named rock")
            return
        end
        t.exec("goAcrossPlankBack", t.player.click_loc, "ahoy_gangplank_shipwreck_off", 1)
        t.exec("goto-lowerHull", t.player.goto_tile, 3618, 3542, 0)
        t.exec("searchChestForLobster", t.player.click_loc, "ahoy_chest_closed", 1)
        -- The lobster is npc_add'ed after the mesbox (ahoy_book.rs2
        -- ~ahoy_spawn_lobster): play the page, then wait for the spawn.
        t.exec("searchChestForLobster.mes", t.chat.play, { "mesbox:You are attacked by a giant lobster!" })
        local lob_res, lob_row = t.npc.await_present("giant_lobster", 10, 15)
        t.step("lobster.found", lob_res == "ok" and "PASS" or "FAIL", tostring(lob_res) .. " " .. tostring(lob_row))
        if lob_res ~= "ok" then
            t.blocked("killLobster: giant_lobster not found in the pool after the chest search")
            return
        end
        t.exec("killLobster.attack", t.player.attack, "giant_lobster", 2, 20)
        -- 30 hp at attack/strength 40 with a rune scimitar: ~75 ticks
        -- (build/quest_gate/s26gh_lob2: dead after 76). 60 was too short.
        t.exec("killLobster.dead", t.npc.await_dead_engaged, 150, 6)

        t.exec("searchChestAfterLobster", t.player.click_loc, "ahoy_chest_open", 1)
        t.exec("inv.scrap3", t.inv.await, "ahoy_map_scrap_3", 1, 5)

        t.exec("useMapsTogether", t.player.use_item_on_item, "ahoy_map_scrap_1", "ahoy_map_scrap_2")
        t.exec("inv.map_complete", t.inv.expect_has, "ahoy_map_complete", 1)

        -- Off the wreck, to the Phasmatys dock, sail to Dragontooth Island.
        barrier("enterPhasForDigging", 1, guard_toll, true)
        t.exec("goto-phasDock", t.player.goto_tile, 3703, 3487, 0)
        t.exec("takeRowingBoat", t.player.talk_to, "ahoy_ghost_captain_1", 1)
        t.exec("takeRowingBoat-dialog", t.chat.play, {
            "choose:Pay 25 ecto-tokens for a return trip.",
            "player:Take me to Dragontooth Island,",
            "npc:Hold on tight.",
        })
        local dt_res, dt_tile = t.world.tile()
        t.step("world.dragontooth", dt_res == "ok" and "PASS" or "FAIL", tostring(dt_tile))

        -- Walk the island (Quest Helper digForBook DigStep 3803,3530, south
        -- of the dock), not a ::goto. walk_to answers a bare ok (trap f):
        -- check the tile it reached.
        local walk_res = t.player.walk_to(3803, 3530)
        local dig_res, dig_tile = t.world.tile()
        t.check("walk.digspot", walk_res == "ok" and dig_res == "ok" and dig_tile ~= nil
            and dig_tile.x == 3803 and dig_tile.z == 3530,
            "walk_to " .. tostring(walk_res) .. "; at " .. tostring(dig_tile and dig_tile.x)
                .. "," .. tostring(dig_tile and dig_tile.z))
        -- The spade dig has no named driver verb (DigStep) -- drive it
        -- through the real held-item op on the spade itself (general_use/
        -- spade.rs2's dispatch chain reaches ~ahoy_try_dig).
        t.exec("digForBook", t.player.inv_op, "spade", 1)
        t.exec("inv.book", t.inv.await, "ahoy_book_of_haricanto", 1, 5)

        -- Free return: walk back to the Ghost captain at the island's dock
        -- (m59_55.spawn 3792,3560; Quest Helper returnToPhas 3791,3559).
        local back_res = t.player.walk_to(3791, 3558)
        local back_tr, back_tile = t.world.tile()
        t.check("walk.dragontooth_dock", back_res == "ok" and back_tr == "ok" and back_tile ~= nil
            and back_tile.x == 3791 and back_tile.z == 3558,
            "walk_to " .. tostring(back_res) .. "; at " .. tostring(back_tile and back_tile.x)
                .. "," .. tostring(back_tile and back_tile.z))
        t.exec("returnToPhas", t.player.talk_to, "ahoy_ghost_captain_1", 1)
        t.exec("returnToPhas-dialog", t.chat.play, {
            "player:Take me back to Port Phasmatys, please.",
            "npc:Righto.",
        })

        -- =================================================================
        -- Independent hand-ins (robes, manual, book all carried at once),
        -- ghostspeak amulet, enchantment, Necrovarus, completion.
        -- =================================================================
        -- Landed on the Phasmatys dock, inside the town: out by the barrier.
        barrier("exitPhasForCroneAgain", 1, nil, false)
        t.exec("goto-returnToCrone", t.player.goto_tile, 3461, 3558, 0)
        t.exec("returnToCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("returnToCrone-dialog", t.chat.play, {
            "npc:The Book of Haricanto",
            "npc:The translation manual will let me read the rite.",
            "npc:Necrovarus's own robes.",
            "npc:Now bring me an ordinary ghostspeak amulet",
        })
        t.exec("quest.stage.need_amulet", t.quest.expect_stage, "need_amulet")

        t.exec("bringCroneAmulet", t.player.talk_to, "ahoy_crone", 1)
        t.exec("bringCroneAmulet-dialog", t.chat.play, {
            "mesbox:The Old Crone dons the robes",
            "npc:your amulet is enchanted",
        })
        t.exec("quest.stage.amulet_enchanted", t.quest.expect_stage, "amulet_enchanted")
        t.exec("inv.enchanted_amulet", t.inv.expect_has, "amulet_of_ghostspeak_enchanted", 1)

        t.exec("equip.enchanted_amulet", t.player.equip, "amulet_of_ghostspeak_enchanted")

        t.exec("goto-talkToNecroAfterCurse", t.player.goto_tile, 3660, 3516, 0)
        t.exec("talkToNecroAfterCurse", t.player.talk_to, "ahoy_necrovarus", 1)
        t.exec("talkToNecroAfterCurse-dialog", t.chat.play, {
            "player:Let any ghost who so wishes pass on",
            "mesbox:A beam of green light radiates out from your amulet",
            "npc:My power over this town",
            "mesbox:Necrovarus's hold over Port Phasmatys shatters.",
        })
        t.exec("quest.stage.necrovarus_defeated", t.quest.expect_stage, "necrovarus_defeated")

        local snap_res, snap = t.skill.snapshot()
        t.step("skill.snapshot", snap_res == "ok" and "PASS" or "FAIL", "before hand-in")

        -- enterPhasFinal: free once Necrovarus is commanded (Quest Helper
        -- GhostsAhoy.java:421), the guard says why (Transcript:Ghost guard).
        local tok_res2, tok_before2 = t.inv.count("ectotoken")
        barrier("enterPhasFinal", 1, { "npc:All visitors to Port Phasmatys must pay" }, true)
        local tok_res3, tok_after2 = t.inv.count("ectotoken")
        t.check("enterPhasFinal.free", tok_res2 == "ok" and tok_res3 == "ok" and tok_before2 == tok_after2,
            "ecto-tokens " .. tostring(tok_before2) .. " -> " .. tostring(tok_after2))
        t.exec("goto-talkToVelorinaFinal", t.player.goto_tile, 3678, 3510, 0)
        t.exec("talkToVelorinaFinal", t.player.talk_to, "ahoy_velorina", 1)
        t.exec("talkToVelorinaFinal-dialog", t.chat.play, {
            "player:Necrovarus's curse is broken",
            "npc:Thank you, thank you a thousand times over.",
            "npc:Please, take this Ectophial",
        })
        t.quest.expect_complete()

        t.exec("reward.prayer_xp", t.skill.expect_gain, "prayer", 2400, snap)
        t.exec("reward.ectophial", t.inv.expect_has, "ectophial", 1)

        -- Postquest: free barrier passage both ways, ectophial teleport.
        -- The completed barrier resolves to ahoy_town_barrier_post_quest,
        -- whose one op is op4 "Pass" (all.loc; wiki Energy Barrier oldid
        -- 15134513): out, then back in, no tokens either way.
        local tok_res4, tok_before3 = t.inv.count("ectotoken")
        barrier("barrierPostquest", 4, nil, false)
        barrier("barrierPostquestIn", 4, { "npc:you have done the ghosts of our town a service" }, true)
        local tok_res5, tok_after3 = t.inv.count("ectotoken")
        t.check("barrierPostquest.free", tok_res4 == "ok" and tok_res5 == "ok" and tok_before3 == tok_after3,
            "ecto-tokens " .. tostring(tok_before3) .. " -> " .. tostring(tok_after3))

        t.exec("ectophial.empty", t.player.inv_op, "ectophial", 1)
        t.exec("inv.ectophial_empty", t.inv.await, "ectophial_empty", 1, 5)
        local ec_res, ec_tile = t.world.tile()
        t.step("world.ectophial_arrival", ec_res == "ok" and "PASS" or "FAIL", tostring(ec_tile))
        t.exec("inv.ectophial_refilled", t.inv.await, "ectophial", 1, 5)

        t.finish(0)
    end,
}
