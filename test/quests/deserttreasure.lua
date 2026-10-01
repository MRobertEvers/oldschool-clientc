-- Desert Treasure I -- relay file, six legs. Leg 1: Asgarnia Smith (Bedabin Camp) through
-- Eblis in the Bandit Camp (guide steps talkToArchaeologist .. talkToEblis).
return {
    id = "deserttreasure",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- The six prerequisite quests the guide requires (Quest Helper getGeneralRequirements).
        "::complete quest_digsite",
        "::complete quest_templeofikov",
        "::complete quest_touristtrap",
        "::complete quest_trollstronghold",
        "::complete quest_priestinperil",
        "::complete quest_waterfall",
        "::complete quest_plaguecity",
        "::setlevel magic 50", -- guide: Magic 50 to start (deserttreasure.rs2 dt_has_requirements)
        -- leg 1 guide requirement: buyDrink wants coins (Bandit Camp beer costs 650).
        "::give coins 1000",
    },
    bind = {
        varp = "deserttreasure",
        constants = {
            not_started = 0, etchings = 1, translating = 2, have_translation = 3,
            read_notes = 4, bandit_camp = 5, heard_diamonds = 6, gather_mirrors = 7,
            complete = 15,
        },
        row = "quest_deserttreasure",
        display = "Desert Treasure I",
        points = 3,
    },
    legs = {
        { name = "etchings_to_eblis", run = function(t)
            -- LEG 1 BEGIN: talkToArchaeologist
            -- Pages a script opens one after another are answered by draining to each
            -- options page, choosing, and draining to the end (long npc lines split into pages).
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 25 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            t.exec("goto-talkToArchaeologist", t.player.goto_tile, 3177, 3043, 0)
            t.exec("talkToArchaeologist", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
            converse("talkToArchaeologist-dialog", {"Do you have any quests?", "Yes, I'll help you."})
            t.ticks(2)
            t.expect("quest.stage.etchings", t.quest.expect_stage("etchings"))
            t.inv.expect_has("four_diamonds_etchings", 1)

            t.exec("goto-talkToExpert", t.player.goto_tile, 3359, 3332, 0)
            t.exec("talkToExpert", t.player.talk_to, "archaeological_expert", 1)
            converse("talkToExpert-dialog", {})
            t.ticks(2)
            t.expect("quest.stage.translating", t.quest.expect_stage("translating"))

            t.exec("talkToExpertAgain", t.player.talk_to, "archaeological_expert", 1)
            converse("talkToExpertAgain-dialog", {})
            t.ticks(2)
            t.expect("quest.stage.have_translation", t.quest.expect_stage("have_translation"))
            t.inv.expect_has("four_diamonds_translation_primer", 1)

            t.exec("goto-bringTranslationToArchaeologist", t.player.goto_tile, 3177, 3043, 0)
            t.exec("bringTranslationToArchaeologist", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
            converse("bringTranslationToArchaeologist-dialog", {})
            t.ticks(2)
            t.expect("quest.stage.read_notes", t.quest.expect_stage("read_notes"))

            t.exec("talkToArchaeologistAgainAfterTranslation", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
            converse("talkToArchaeologistAgainAfterTranslation-dialog", {"Help him."})
            t.ticks(2)
            t.expect("quest.stage.bandit_camp", t.quest.expect_stage("bandit_camp"))

            t.exec("goto-buyDrink", t.player.goto_tile, 3159, 2980, 0)
            t.exec("buyDrink", t.player.talk_to, "fourdiamonds_bartender", 1)
            converse("buyDrink-dialog", {"Buy a drink.", "Buy a beer."})
            t.ticks(2)
            t.inv.await("bandit_brew", 1, 10)
            local _, brew = t.inv.count("bandit_brew")
            local _, coins = t.inv.count("coins")
            t.check("buyDrink.paid", brew == 1 and coins == 350, "bandit_brew=" .. tostring(brew) .. " coins=" .. tostring(coins) .. " after the 650-coin beer")

            t.exec("talkToBartender", t.player.talk_to, "fourdiamonds_bartender", 1)
            converse("talkToBartender-dialog", {"I heard about four diamonds..."})
            t.ticks(2)
            t.expect("quest.stage.heard_diamonds", t.quest.expect_stage("heard_diamonds"))

            t.exec("goto-talkToEblis", t.player.goto_tile, 3184, 2985, 0)
            t.exec("talkToEblis", t.player.talk_to, "fourdiamonds_elder", 1)
            converse("talkToEblis-dialog", {"Tell me of the four diamonds of Azzanadra.", "Yes."})
            t.ticks(2)
            t.expect("quest.stage.gather_mirrors", t.quest.expect_stage("gather_mirrors"))

            local _, tile = t.world.tile()
            local _, level = t.world.level()
            local _, stage = t.quest.stage()
            local _, brew = t.inv.count("bandit_brew")
            t.check("leg.1.state", level == 0 and stage == 7 and brew == 1,
                "player at " .. tostring(tile.x) .. "," .. tostring(tile.z) .. " level " .. tostring(level) .. ", deserttreasure stage " .. tostring(stage) .. " (gather_mirrors), backpack: coins 350, bandit_brew " .. tostring(brew))
            -- LEG 1 END
        end },
        { name = "smoke_dungeon_to_cross", run = function(t)
            -- LEG 2 BEGIN: enterSmokeDungeon
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 25 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            -- Brought along for this leg (guide items of steps 2.9-2.19): levels, light, face cover,
            -- ice gloves, a melee weapon, food, lockpicks. Ingredients Eblis wants (stage 7 -> 10,
            -- deserttreasure.rs2:286) are handed over as certs so the backpack holds them.
            t.cheat("::setlevel firemaking 50") -- guide: Firemaking 50 to light the torches (dt_fm_req)
            t.cheat("::setlevel thieving 53") -- guide: Thieving 53 for the Bandit Camp chest (dt_thieve_req)
            t.cheat("::setlevel attack 70")
            t.cheat("::setlevel strength 70")
            t.cheat("::setlevel defence 60")
            t.cheat("::setlevel hitpoints 80")
            t.cheat("::give cert_magic_logs 12") -- Eblis: 12 magic logs
            t.cheat("::give cert_steel_bar 6")   -- Eblis: 6 steel bars
            t.cheat("::give cert_molten_glass 6") -- Eblis: 6 molten glass
            t.cheat("::give bones 1")
            t.cheat("::give ashes 1")
            t.cheat("::give charcoal 1")
            t.cheat("::give bloodrune 1")
            t.cheat("::give tinderbox 1")      -- guide: Tinderbox
            t.cheat("::give gasmask 1")        -- guide: Facemask (dt_smoke_protected, rs2:45)
            t.cheat("::give ice_gloves 1")     -- guide: Ice gloves
            t.cheat("::give rune_scimitar 1")  -- guide: melee gear
            t.cheat("::give shark 8")          -- food for Fareed
            t.cheat("::give lockpick 5")       -- guide: as many lockpicks as you can
            t.ticks(3)

            -- Hand the ingredients to Eblis (opnpcu, rs2:338-385), then speak to him: stage 7 -> 10.
            t.exec("goto-handInIngredients", t.player.goto_tile, 3184, 2985, 0)
            for _, item in ipairs({ "cert_magic_logs", "cert_steel_bar", "cert_molten_glass", "bones", "ashes", "charcoal", "bloodrune" }) do
                t.exec("handIn-" .. item, t.player.use_on, item, t.player.by_symbol("npc", "fourdiamonds_elder"))
                converse("handIn-" .. item .. "-dialog", {})
                t.ticks(1)
            end
            t.exec("talkToEblisMirrors", t.player.talk_to, "fourdiamonds_elder", 1)
            converse("talkToEblisMirrors-dialog", {})
            t.ticks(2)
            local _, stage_now = t.quest.stage()
            t.check("mirrors.ready", stage_now == 10, "deserttreasure stage read from the server: " .. tostring(stage_now) .. " (dt_mirrors_ready)")

            t.exec("wear-gasmask", t.player.equip, "gasmask")
            t.exec("wear-icegloves", t.player.equip, "ice_gloves")
            t.exec("wear-scimitar", t.player.equip, "rune_scimitar")

            t.exec("goto-enterSmokeDungeon", t.player.goto_tile, 3310, 2964, 0)
            t.exec("enterSmokeDungeon", t.player.click_loc, "sword_haunted_well", 1)
            t.ticks(6)
            local _, dungeon_tile = t.world.tile()
            t.check("enterSmokeDungeon.below", dungeon_tile.z > 9000, "player tile after the well: " .. tostring(dungeon_tile.x) .. "," .. tostring(dungeon_tile.z))

            -- Torches, NE first (guide lightTorch1 note), then the others; each burns 250 ticks.
            local function light(name, symbol, x, z, count_var)
                t.exec("goto-" .. name, t.player.goto_tile, x, z - 1, 0)
                for attempt = 1, 8 do
                    t.exec(name .. (attempt > 1 and ("-retry" .. attempt) or ""), t.player.use_on, "tinderbox", t.player.by_symbol("loc", symbol), { at = { x, z } })
                    local lit = t.var.await_server(count_var, 1, 6)
                    if lit == "ok" then break end
                end
                t.expect(name .. ".lit", t.var.await_server(count_var, 1, 2))
            end
            light("lightTorch1", "4d_standing_torch1_unlit", 3323, 9398, "fd_torch_count1")
            light("lightTorch2", "4d_standing_torch2_unlit", 3321, 9355, "fd_torch_count2")
            light("lightTorch4", "4d_standing_torch3_unlit", 3204, 9350, "fd_torch_count3")
            light("lightTorch3", "4d_standing_torch4_unlit", 3207, 9395, "fd_torch_count4")

            t.exec("goto-openChest", t.player.goto_tile, 3248, 9363, 0)
            for attempt = 1, 4 do
                t.exec("openChest" .. (attempt > 1 and ("-retry" .. attempt) or ""), t.player.click_loc, "fd_firedungeon_shutchest", 1)
                t.ticks(4)
                local _, have = t.inv.count("fd_firekey")
                if (have or 0) > 0 then break end
            end
            t.inv.expect_has("fd_firekey", 1)

            t.exec("goto-enterFareedRoom", t.player.goto_tile, 3303, 9376, 0)
            t.exec("enterFareedRoom", t.player.click_loc, "fd_fw_metalgateclosed_r", 1)
            t.ticks(3)
            local _, keys = t.inv.count("fd_firekey")
            t.check("useWarmKey", keys == 0, "the gate's op1 consumed the warm key (rs2:1026-1031), warm keys left: " .. tostring(keys))

            t.exec("killFareed", t.player.attack, "firediamond_firewarrior", 2, 20)
            t.exec("killFareed.dead", t.npc.await_dead_engaged, 400, 60, { eat = { item = "shark", below = 40 } })
            t.ticks(3)

            t.exec("goto-talkToRasolo", t.player.goto_tile, 2531, 3418, 0)
            t.exec("talkToRasolo", t.player.talk_to, "shadow_warrior_rasool", 1)
            converse("talkToRasolo-dialog", { "Ask about the Diamonds of Azzanadra", "Yes" })
            t.ticks(2)

            t.exec("goto-getCross", t.player.goto_tile, 3169, 2965, 0)
            local function cross_count() local _, c = t.inv.count("fd_sword_cross"); return c or 0 end
            for attempt = 1, 14 do
                if cross_count() > 0 then break end
                t.exec("getCross-try" .. attempt, t.player.click_loc, "fd_bandit_shutchest", 1)
                local _, kind = t.chat.drain({ stop_at = "options", max_pages = 6 })
                if kind == "options" then
                    t.exec("getCross-yes" .. attempt, t.chat.choose, "Yes")
                    t.ticks(1)
                    t.chat.drain({ max_pages = 6 })
                end
                t.ticks(8)
            end
            t.inv.expect_has("fd_sword_cross", 1)

            local _, tile = t.world.tile()
            local _, level = t.world.level()
            local _, stage = t.quest.stage()
            t.check("leg.2.state", level == 0 and cross_count() == 1,
                "player at " .. tostring(tile.x) .. "," .. tostring(tile.z) .. " level " .. tostring(level) .. ", deserttreasure stage " .. tostring(stage) .. ", gilded cross in the backpack")
            -- LEG 2 END
        end },
        { name = "rasolo_to_ruantun", run = function(t)
            -- LEG 3 BEGIN: returnCross
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 25 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            -- Brought along for this leg: food for Damis, a silver bar for Ruantun (guide items).
            t.cheat("::give shark 8")      -- food for Damis
            t.cheat("::give silver_bar 1") -- guide: Silver bar for Ruantun's pot
            t.ticks(3)

            t.exec("goto-returnCross", t.player.goto_tile, 2531, 3418, 0)
            t.exec("returnCross", t.player.talk_to, "shadow_warrior_rasool", 1)
            converse("returnCross-dialog", {})
            t.ticks(2)
            t.inv.expect_has("fd_ring_visibility", 1)
            t.exec("wear-ring", t.player.equip, "fd_ring_visibility")
            t.ticks(2)

            t.exec("goto-enterShadowDungeon", t.player.goto_tile, 2547, 3419, 0)
            local down_ok = false
            for _, sym in ipairs({ "fd_shadowladder1", "fd_shadow_ladder_there", "deserttreasure_shadowladder" }) do
                t.exec("enterShadowDungeon-" .. sym, t.player.click_loc, sym, 1)
                t.ticks(6)
                local _, at = t.world.tile()
                if at.z > 5000 then down_ok = true break end
            end
            local _, below = t.world.tile()
            t.check("enterShadowDungeon.below", down_ok, "player tile after the ladder: " .. tostring(below.x) .. "," .. tostring(below.z))

            t.exec("waitForDamis", t.player.goto_tile, 2738, 5090, 0)
            t.exec("waitForDamis.spawn", t.npc.await_present, "fd_damis_normal", 25, 40)

            t.exec("killDamis1", t.player.attack, "fd_damis_normal", 2, 20)
            t.exec("killDamis1.dead", t.npc.await_dead_engaged, 400, 60, { eat = { item = "shark", below = 40 } })
            t.exec("killDamis2.spawn", t.npc.await_present, "fd_damis_tougher", 25, 20)
            -- the first form's claim on the player lingers for a few ticks ("I'm already under attack.")
            for attempt = 1, 6 do
                t.ticks(5)
                local res = t.player.attack("fd_damis_tougher", 2, 20)
                if res == "ok" then break end
            end
            t.exec("killDamis2", t.player.attack, "fd_damis_tougher", 2, 20)
            t.exec("killDamis2.dead", t.npc.await_dead_engaged, 500, 60, { eat = { item = "shark", below = 40 } })
            t.ticks(3)
            local _, shadow_stage = t.var.server("dt_shadow_stage")
            t.check("killDamis.stage", shadow_stage ~= nil, "dt_shadow_stage read a tick after the corpse stage: " .. tostring(shadow_stage))

            t.exec("pickUpShadowDiamond", t.player.click_obj, "fd_dark_diamond", 3)
            t.inv.await("fd_dark_diamond", 1, 10)
            local _, dcount = t.inv.count("fd_dark_diamond")
            t.check("pickUpShadowDiamond.have", (dcount or 0) == 1, "shadow diamond in the backpack: " .. tostring(dcount))

            t.exec("goto-talkToMalak", t.player.goto_tile, 3495, 3479, 0)
            t.exec("talkToMalak", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
            converse("talkToMalak-dialog", { "I am looking for a special Diamond...", "Agree to this arrangement." })
            t.ticks(2)
            local _, blood = t.var.server("dt_blood_stage")
            t.check("talkToMalak.agreed", blood ~= nil and blood >= 2, "dt_blood_stage read from the server: " .. tostring(blood))

            t.exec("askAboutKillingDessous", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
            converse("askAboutKillingDessous-dialog", {})
            t.ticks(2)

            t.exec("goto-enterSewer", t.player.goto_tile, 3118, 3245, 0)
            t.exec("enterSewer-open", t.player.click_loc, "vampire_trap1", 1)
            t.ticks(3)
            t.exec("enterSewer", t.player.click_loc, "vampire_trap2", 1)
            t.ticks(6)
            local _, sewer = t.world.tile()
            t.check("enterSewer.below", sewer.z > 9000, "player tile after the trapdoor: " .. tostring(sewer.x) .. "," .. tostring(sewer.z))

            t.exec("goto-talkToRuantun", t.player.goto_tile, 3112, 9688, 0)
            t.exec("talkToRuantun", t.player.talk_to, "malak", 1)
            converse("talkToRuantun-dialog", {})
            t.ticks(2)
            t.inv.expect_has("fd_silver_pot", 1)

            local _, tile = t.world.tile()
            local _, level = t.world.level()
            local _, stage = t.quest.stage()
            t.check("leg.3.end", level == 0 and stage ~= nil,
                "player at " .. tostring(tile.x) .. "," .. tostring(tile.z) .. " level " .. tostring(level) .. ", deserttreasure stage " .. tostring(stage) .. ", backpack: fd_silver_pot, fd_dark_diamond, sharks; worn gasmask, ice_gloves, rune_scimitar, ring of visibility")
            -- LEG 3 END
        end },
        { name = "entrana_to_icegate", run = function(t)
            -- LEG 4 BEGIN: blessPot
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 25 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            -- Leg 3 left ~16 sharks; free five slots so this leg's items fit in 28 slots.
            for _ = 1, 5 do t.player.drop("shark") t.ticks(1) end
            local _, sharks_left = t.inv.count("shark")
            t.check("leg.4.pack", (sharks_left or 0) >= 8, "sharks kept for Dessous after dropping five: " .. tostring(sharks_left))

            -- Brought along for this leg (guide items): garlic powder, spice, a cake for the troll child.
            t.cheat("::give fd_crushed_garlic 1") -- guide: Garlic powder (crushed garlic)
            t.cheat("::give spicespot 1")         -- guide: Spice
            t.cheat("::give cake 1")              -- guide: Cake for the Troll Child
            t.ticks(3)

            t.exec("goto-blessPot", t.player.goto_tile, 2851, 3349, 0)
            t.exec("blessPot", t.player.talk_to, "high_priest_of_entrana", 1)
            converse("blessPot-dialog", {})
            t.ticks(2)
            t.inv.expect_has("fd_silver_pot_blessed", 1)

            t.exec("goto-talkToMalakWithPot", t.player.goto_tile, 3495, 3479, 0)
            t.exec("talkToMalakWithPot", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
            converse("talkToMalakWithPot-dialog", {})
            t.ticks(2)
            t.inv.expect_has("fd_silver_pot_blood_blessed", 1)

            t.exec("addPowder", t.player.use_item_on_item, "fd_crushed_garlic", "fd_silver_pot_blood_blessed")
            t.inv.await("fd_silver_pot_blood_garlic_blessed", 1, 8)
            t.inv.expect_has("fd_silver_pot_blood_garlic_blessed", 1)
            t.exec("addSpice", t.player.use_item_on_item, "spicespot", "fd_silver_pot_blood_garlic_blessed")
            t.inv.await("fd_silver_pot_blood_garlic_spiced_blessed", 1, 8)
            t.inv.expect_has("fd_silver_pot_blood_garlic_spiced_blessed", 1)

            t.exec("goto-usePotOnGrave", t.player.goto_tile, 3567, 3402, 0) -- the free tile west of the tomb, inside the graveyard fence (deserttreasure.rs2:697)
            t.exec("usePotOnGrave", t.player.use_on, "fd_silver_pot_blood_garlic_spiced_blessed", t.player.by_symbol("loc", "vampire_big_grave_noblood"))
            t.exec("usePotOnGrave.spawn", t.npc.await_present, "blooddiamond_vampirewarrior", 25, 20)
            t.exec("stepOffDessous", t.player.goto_tile, 3567, 3403, 0) -- Dessous spawned on the tile the player pressed from
            t.ticks(2)

            t.exec("killDessous", t.player.attack, "blooddiamond_vampirewarrior", 2, 20)
            t.exec("killDessous.dead", t.npc.await_dead_engaged, 500, 60, { eat = { item = "shark", below = 40 } })
            t.ticks(3)
            local _, blood = t.var.server("dt_blood_stage")
            t.check("killDessous.stage", blood ~= nil and blood >= 3, "dt_blood_stage read a tick after the corpse stage: " .. tostring(blood))

            t.exec("goto-talkToMalakForDiamond", t.player.goto_tile, 3495, 3479, 0)
            t.exec("talkToMalakForDiamond", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
            converse("talkToMalakForDiamond-dialog", {})
            t.ticks(2)
            t.inv.expect_has("fd_blood_diamond", 1)

            t.exec("goto-giveCakeToTroll", t.player.goto_tile, 2835, 3739, 0)
            t.exec("giveCakeToTroll", t.player.use_on, "cake", t.player.by_symbol("npc", "fourdiamonds_troll_child_crying"))
            converse("giveCakeToTroll-dialog", {})
            t.ticks(2)
            t.inv.expect_absent("cake")

            t.exec("talkToChildTroll", t.player.talk_to, "fourdiamonds_troll_child_okay", 1)
            converse("talkToChildTroll-dialog", { "Yes" })
            t.ticks(2)
            local _, ice = t.var.server("dt_ice_stage")
            t.check("talkToChildTroll.agreed", ice ~= nil and ice >= 2, "dt_ice_stage read from the server: " .. tostring(ice))

            t.exec("enterIceGate", t.player.click_loc, "icegate_left", 1)
            t.ticks(4)

            local _, tile = t.world.tile()
            local _, level = t.world.level()
            local _, stage = t.quest.stage()
            t.check("leg.4.end", level == 0 and stage ~= nil and tile.x > 2838,
                "player at " .. tostring(tile.x) .. "," .. tostring(tile.z) .. " level " .. tostring(level) .. ", deserttreasure stage " .. tostring(stage) .. ", backpack: fd_blood_diamond, fd_dark_diamond, sharks; worn gasmask, ice_gloves, rune_scimitar, ring of visibility; past the ice gate, cold drain active")
            -- LEG 4 END
        end },
        { name = "ice_trolls", run = function(t)
            -- LEG 5 BEGIN: killIceTrolls
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 25 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            -- Brought along for this leg (guide: Fire spells, Spiked boots): runes and levels for fire blast
            -- (the cold drain saps magic every 10 ticks), and the boots the ice ledge asks for.
            t.cheat("::setlevel magic 99")   -- guide: fire spells (Kamil and the ice blocks)
            t.cheat("::give airrune 300")    -- guide: fire blast runes
            t.cheat("::give firerune 500")
            t.cheat("::give deathrune 100")
            t.cheat("::give death_spikedboots 1") -- guide: Spiked boots
            t.cheat("::setlevel hitpoints 99")  -- guide: Ice trolls are level 120 (survive the fight)
            t.cheat("::setlevel defence 99")
            t.cheat("::setlevel attack 99")
            t.cheat("::setlevel strength 99")
            t.cheat("::give shark 20")          -- guide: food for the troll fights
            t.ticks(3)

            local trolls = { "trollrescue_icetroll_melee1", "trollrescue_icetroll_melee2", "trollrescue_icetroll_melee3",
                "trollrescue_icetroll_melee4", "trollrescue_icetroll_melee5", "trollrescue_icetroll_melee6", "trollrescue_icetroll_melee7" }
            -- Single-way combat: seven wandering aggressive trolls keep renewing the player's claim and every
            -- press on the hunted one answers "I'm already under attack." (docs/quest_authoring/gaps-combat.md).
            for _, sym in ipairs(trolls) do t.cheat("::passive " .. sym) end
            t.ticks(2)
            t.exec("goto-killIceTrolls", t.player.goto_tile, 2854, 3733, 0)
            local killed = 0
            for _ = 1, 14 do
                local _, k = t.var.server("fd_icewarrior_trollskilled")
                killed = k or 0
                if killed >= 5 then break end
                for _, sym in ipairs(trolls) do
                    local r = t.player.cast("fire_blast", sym, 14)
                    if r == "ok" then
                        t.npc.await_dead_engaged(400, 50, { eat = { item = "shark", below = 40 } })
                        t.ticks(2)
                        break
                    end
                end
            end
            local _, k = t.var.server("fd_icewarrior_trollskilled")
            t.check("killIceTrolls", (k or 0) >= 5, "fd_icewarrior_trollskilled read from the server: " .. tostring(k))

            t.exec("goto-enterTrollCave", t.player.goto_tile, 2866, 3719, 0)
            t.exec("enterTrollCave", t.player.click_loc, "trollrescue_troll_cave_entrance", 1)
            t.ticks(4)
            local _, cave_tile = t.world.tile()
            t.check("enterTrollCave.in", cave_tile ~= nil and cave_tile.x >= 2870, "in the cave at " .. tostring(cave_tile and cave_tile.x) .. "," .. tostring(cave_tile and cave_tile.z))

            t.exec("goto-killKamil", t.player.goto_tile, 2863, 3754, 0)
            t.exec("killKamil", t.player.cast, "fire_blast", "icediamond_icewarrior", 14)
            -- a fire blast can kill Kamil inside the cast's own settle (hp no bar -> gone), so read the
            -- death from the server and recast while he lives.
            local kamil_dead = 0
            for attempt = 1, 8 do
                t.ticks(4)
                local _, kd = t.var.server("fd_icewarrior_dead")
                kamil_dead = kd or 0
                if kamil_dead == 1 then break end
                local cr = t.player.cast("fire_blast", "icediamond_icewarrior", 14)
                t.note("cast -> " .. tostring(cr) .. "; fd_icewarrior_dead " .. tostring(kamil_dead))
            end
            t.check("killKamil.dead", kamil_dead == 1, "fd_icewarrior_dead read from the server: " .. tostring(kamil_dead))
            t.ticks(3)
            local _, ice = t.var.server("dt_ice_stage")
            t.check("killKamil.stage", ice ~= nil and ice >= 3, "dt_ice_stage read a tick after the corpse stage: " .. tostring(ice))

            t.exec("wearSpikedBoots", t.player.equip, "death_spikedboots")
            t.exec("goto-climbOnToLedge", t.player.goto_tile, 2837, 3803, 0)
            t.exec("climbOnToLedge", t.player.click_loc, "trollrescue_blankmodel", 1)
            t.ticks(4)
            local _, ledge_level = t.world.level()
            t.check("climbOnToLedge.up", ledge_level == 1, "level after the ledge: " .. tostring(ledge_level))

            t.exec("goto-goThroughPathGate", t.player.goto_tile, 2853, 3811, 1)
            t.exec("goThroughPathGate", t.player.click_loc, "icegate_right_small", 1)
            t.ticks(4)
            local _, gate_level = t.world.level()
            t.check("goThroughPathGate.up", gate_level == 2, "level after the path gate: " .. tostring(gate_level))

            t.exec("goto-breakIce1", t.player.goto_tile, 2829, 3808, 2)
            local dadfree = 0
            for attempt = 1, 6 do
                local cr, cd = t.player.cast("fire_blast", "fd_trollblock1", 14)
                t.ticks(4)
                local _, df = t.var.server("fd_icewarrior_dadfree")
                dadfree = df or 0
                t.note("cast -> " .. tostring(cr) .. " " .. tostring(cd) .. "; dadfree " .. tostring(dadfree))
                if dadfree == 1 then break end
            end
            t.check("breakIce1", dadfree == 1, "fd_icewarrior_dadfree read from the server: " .. tostring(dadfree))
            local mumfree = 0
            for attempt = 1, 6 do
                local cr, cd = t.player.cast("fire_blast", "fd_trollblock2", 14)
                t.ticks(4)
                local _, mf = t.var.server("fd_icewarrior_mumfree")
                mumfree = mf or 0
                t.note("cast -> " .. tostring(cr) .. " " .. tostring(cd) .. "; mumfree " .. tostring(mumfree))
                if mumfree == 1 then break end
            end
            t.check("breakIce2", mumfree == 1, "fd_icewarrior_mumfree read from the server: " .. tostring(mumfree))

            -- the freed parent is a multinpc of the block (troll_block_2 -> fd_troll_mum by fd_icewarrior_mumfree)
            -- the killed block respawns as the freed parent after the npc respawn delay
            t.exec("talkToTrolls.respawn", t.npc.await_present, "fd_troll_mum", 25, 150)
            t.ticks(3)
            t.exec("talkToTrolls", t.player.talk_to, "fd_troll_mum", 1)
            converse("talkToTrolls-dialog", {})
            t.ticks(3)
            local _, ice2 = t.var.server("dt_ice_stage")
            t.check("talkToTrolls.reunion", ice2 ~= nil and ice2 >= 4, "dt_ice_stage read from the server: " .. tostring(ice2))

            t.exec("talkToChildTrollAfterFreeing", t.player.talk_to, "fourdiamonds_troll_child_okay", 1)
            converse("talkToChildTrollAfterFreeing-dialog", {})
            t.ticks(2)
            t.inv.expect_has("fd_icediamond", 1)

            local _, tile = t.world.tile()
            local _, level = t.world.level()
            local _, stage = t.quest.stage()
            t.check("leg.5.state", tile ~= nil and stage ~= nil,
                "player at " .. tostring(tile.x) .. "," .. tostring(tile.z) .. " level " .. tostring(level) .. ", deserttreasure stage " .. tostring(stage) .. ", backpack: fd_icediamond, fd_blood_diamond, fd_dark_diamond, sharks; worn death_spikedboots plus leg 4 gear")
            -- LEG 5 END
        end },
        { name = "pyramid", run = function(t)
            -- LEG 6 BEGIN: placeSmoke
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end
            local function count(item) local _, c = t.inv.count(item); return c or 0 end
            local function pos()
                local _, tile = t.world.tile()
                local _, level = t.world.level()
                return tile, level
            end

            -- Guide: bring food for the pyramid. Leg 5 ended with the backpack's sharks spent and
            -- hitpoints worn down by the troll fights, so restock and eat back to full.
            t.cheat("::give shark 10")
            t.ticks(2)
            for _ = 1, 10 do
                local _, hp = t.skill.read("hitpoints")
                if hp.level >= hp.base_level - 10 then break end
                t.player.inv_op("shark", 1)
                t.ticks(3)
            end
            local _, hp_now = t.skill.read("hitpoints")
            t.check("leg.6.food", hp_now.level >= hp_now.base_level - 10 and count("shark") >= 4,
                "hitpoints " .. tostring(hp_now.level) .. "/" .. tostring(hp_now.base_level) .. ", sharks " .. count("shark"))

            -- The smoke diamond: leg 2 killed Fareed but never picked his diamond up. The smoke gate
            -- (deserttreasure.rs2 dt_smoke_gate_open, the stage-complete branch) puts it back for its killer.
            t.exec("goto-smokeDiamondGate", t.player.goto_tile, 3303, 9376, 0)
            if count("fd_diamond_fire") == 0 then
                t.exec("smokeDiamondGate", t.player.click_loc, "fd_fw_metalgateclosed_r", 1)
                t.ticks(3)
                t.exec("pickUpSmokeDiamond", t.player.click_obj, "fd_diamond_fire", 3)
                t.ticks(2)
            end
            t.inv.expect_has("fd_diamond_fire", 1)

            -- Guide: place each diamond in its obelisk (rs2 oplocu on desert_treasure_oblix_a..d, the
            -- multiloc children of oblix1..4); the guide's coordinates are the obelisk tiles.
            local function place(step, item, symbol, x, z, column)
                t.exec("goto-" .. step, t.player.goto_tile, x - 1, z - 1, 0)
                for attempt = 1, 4 do
                    local _, placed = t.var.server(column)
                    if placed == 1 then break end
                    t.exec(step .. (attempt > 1 and ("-retry" .. attempt) or ""), t.player.use_on, item,
                        t.player.by_symbol("loc", symbol), { at = { x - 1, z - 1 } })
                    t.ticks(3)
                end
                local _, placed = t.var.server(column)
                t.check(step .. ".placed", placed == 1, column .. " read from the server: " .. tostring(placed) .. ", " .. item .. " left: " .. count(item))
            end
            place("placeSmoke", "fd_diamond_fire", "desert_treasure_oblix2", 3245, 2910, "fd_column_fire")
            place("placeShadow", "fd_dark_diamond", "desert_treasure_oblix4", 3221, 2886, "fd_column_shadow")
            place("placeIce", "fd_icediamond", "desert_treasure_oblix3", 3245, 2886, "fd_column_ice")
            place("placeBlood", "fd_blood_diamond", "desert_treasure_oblix1", 3221, 2910, "fd_column_blood")
            t.ticks(2)
            local _, stage_after = t.var.server("deserttreasure")
            t.check("placeBlood.stage", stage_after == 13, "all four pillars set: deserttreasure (server) = " .. tostring(stage_after) .. " (dt_pyramid = 13)")

            -- The pyramid: each ladder is pressed where the guide puts it. A floor's trap (1 in 30 every
            -- 6 ticks) or a fall drops the player outside, so the descent loops from wherever it stands.
            local floors = {
                { step = "goDownFromFirstFloor", level = 3, x = 2909, z = 4964, loc = "desert_laddertop3_2" },
                { step = "goDownFromSecondFloor", level = 2, x = 2846, z = 4973, loc = "desert_laddertop2_1" },
                { step = "goDownFromThirdFloor", level = 1, x = 2784, z = 4941, loc = "desert_laddertop1_0" },
            }
            local function in_temple_floor()
                local tile = pos()
                return tile ~= nil and tile.z >= 9269
            end
            for attempt = 1, 12 do
                if in_temple_floor() then break end
                local tile, level = pos()
                if tile.z < 4000 then
                    t.exec("goto-enterPyramid" .. (attempt > 1 and ("-retry" .. attempt) or ""), t.player.goto_tile, 3233, 2895, 0)
                    t.exec("enterPyramid" .. (attempt > 1 and ("-retry" .. attempt) or ""), t.player.click_loc, "desert_laddertop", 1)
                    t.ticks(4)
                else
                    for _, f in ipairs(floors) do
                        if level == f.level then
                            t.exec("goto-" .. f.step .. "-try" .. attempt, t.player.goto_tile, f.x, f.z, f.level)
                            t.exec(f.step .. "-try" .. attempt, t.player.click_loc, f.loc, 1)
                            t.ticks(4)
                            break
                        end
                    end
                end
            end
            local tile, level = pos()
            t.check("pyramidDescent", tile.z >= 9269 and level == 0, "on the bottom floor at " .. tostring(tile.x) .. "," .. tostring(tile.z) .. " level " .. tostring(level))

            -- The floor's own hazards (rs2 dt_pyramid_hazard) follow the player for 200 ticks: a mummy or a
            -- scarab swarm is fought for real before the door and again before Azzanadra.
            local function clear_hazards(tag)
                for _, symbol in ipairs({ "deserttreasure_mummy_1", "scarab_swarm" }) do
                    for round = 1, 4 do
                        local found, row = t.npc.nearest(symbol, 12)
                        if found ~= "ok" or not row then break end
                        t.exec("hazard-" .. tag .. "-" .. symbol .. round, t.player.attack, symbol, 2, 12)
                        t.exec("hazard-" .. tag .. "-" .. symbol .. round .. ".dead", t.npc.await_dead_engaged, 300, 60, { eat = { item = "shark", below = 40 } })
                        t.ticks(2)
                    end
                end
            end
            clear_hazards("floor")
            -- The door is reached from the north side (rs2 dt_ancient_temple_door_open: z >= 9324 goes in).
            t.exec("goto-enterMiddleOfPyramid", t.player.goto_tile, 3234, 9326, 0)
            clear_hazards("door")
            t.exec("enterMiddleOfPyramid", t.player.click_loc, "dt_ancient_temple_door_open", 1)
            t.ticks(3)
            local after_door = pos()
            t.check("enterMiddleOfPyramid.through", after_door.z < 9324, "carried through the temple door to " .. tostring(after_door.x) .. "," .. tostring(after_door.z))
            clear_hazards("temple")

            local snapshot = select(2, t.skill.snapshot())
            t.exec("talkToAzz", t.player.talk_to, "azzanadra_real", 1)
            converse("talkToAzz-dialog", {})
            t.ticks(4)
            -- Reward lines read off the scroll (deserttreasure.rs2:1957).
            local rewards_result, rewards_detail = t.scroll.rewards()
            local rewards_text = "nil"
            local rewards_ok = false
            if rewards_result == "ok" and type(rewards_detail) == "table" and type(rewards_detail.lines) == "table" then
                rewards_text = table.concat(rewards_detail.lines, " | ")
                rewards_ok = rewards_text:find("20006.9 Magic XP", 1, true) ~= nil
                    and rewards_text:find("Ancient Magicks", 1, true) ~= nil
                    and rewards_text:find("Smoke Dungeon", 1, true) ~= nil
                    and rewards_text:find("Ring of visibility", 1, true) ~= nil
            end
            t.check("scroll.rewardLines", rewards_ok, "scroll.rewards() -> " .. tostring(rewards_result) .. " lines=" .. rewards_text)
            t.quest.expect_complete()
            -- Documented 20,006.9 Magic XP (^dt_magic_reward_xp = 200069 tenths); the client's experience
            -- field carries whole xp, so the 0.9 shows as a floor of 20,006.
            local _, magic_after = t.skill.read("magic")
            local magic_delta = magic_after.experience - snapshot.magic.experience
            t.check("reward.magic_xp", magic_delta == 20006 or magic_delta == 200069,
                "magic xp delta " .. tostring(magic_delta) .. " (documented 20006.9 = 200069 tenths; the client floors to whole xp)")
            -- Quest Helper's item reward is the Ring of Visibility (DesertTreasure.getItemRewards); it
            -- was received from Rasolo mid-quest (rs2:1493) and is worn. Taking it off and counting it in
            -- the backpack reads the held reward back. Ancient Magicks / Smoke Dungeon are unlock lines
            -- (scroll.rewardLines above); the wiki grants no signet, so the stale "signet" text is not a row.
            t.exec("reward.unequipRing", t.player.unequip, "fd_ring_visibility")
            t.ticks(2)
            local _, ring_count = t.inv.count("fd_ring_visibility")
            t.check("reward.ringOfVisibility", (ring_count or 0) >= 1, "Ring of Visibility held after completion, backpack count " .. tostring(ring_count))
            -- LEG 6 END
        end },
    },
}
