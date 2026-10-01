-- The Giant Dwarf: driven from the Quest Helper ladder (31 steps, 3 legs).
-- Consortium tasks are mined and smelted in Keldagrim (no ::give of ores/bars).
return {
    id = "giantdwarf",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel crafting 12",
        "::setlevel firemaking 16",
        "::setlevel magic 40",
        "::setlevel thieving 20",
        "::setlevel mining 60",
        "::setlevel smithing 20",
        "::give rune_pickaxe 1",
        "::setlevel hitpoints 99",
        "::giantdwarf",
        "::give logs 1",
        "::give tinderbox 1",
        "::give coal 1",
        "::give coins 200",
        "::give iron_bar 1",
        "::give lawrune 3",
        "::give airrune 6",
        "::give sapphire 3",
        "::give redberry_pie 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb571_giantdwarf_quest",
            constants = {
                not_started = 0, arrived = 1, veldaban_done = 2, blasidar_done = 3,
                vermundi_asked = 4, librarian_asked = 5, got_book = 6, showed_book = 7,
                machine_loaded = 8, machine_started = 9, clothes_done = 10,
                saro_asked = 11, dromund_asked = 12, left_boot = 13, boots_done = 14,
                santiri_asked = 15, sapphires_used = 16, imcando_asked = 17,
                reldo_told = 18, axe_done = 19, items_given = 20, blasidar_after = 21,
                entered_consortium = 22, secretary_done = 23, director_done = 24,
                joined_company = 25, director_after = 26, ready_to_finish = 28,
                complete = 50,
            },
            row = "quest_thegiantdwarf",
            display = "The Giant Dwarf",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Leg 1
        t.exec("goto-enterDwarfCave", t.player.goto_tile, 2732, 3713, 0)
        t.exec("enterDwarfCave", t.player.click_loc, "trollromance_stronghold_exit_tunnel", 1)
        t.ticks(4)
        t.exec("enterDwarfCave2", t.player.click_loc, "dwarf_cavewall_tunnel", 1)
        t.ticks(4)
        -- the prequest symbol has no spawn row; the placed boatman (m44_158.spawn:12) routes to it
        t.exec("goto-talkToBoatman", t.player.goto_tile, 2840, 10129, 0)
        t.exec("talkToBoatman", t.player.talk_to, "dwarf_city_boatman_mines", 1)
        t.exec("talkToBoatman.chat", t.chat.play, {
            "npc:For a human like you",
            "choose:That's a deal!",
            "player:That's a deal!",
            "npc:Yes, I'm ready",
            "player:Yes.",
        })
        t.ticks(4)
        t.expect("quest.stage.arrived", t.quest.expect_stage("arrived"))

        t.exec("goto-talkToVeldaban", t.player.goto_tile, 2827, 10213, 0)
        t.exec("talkToVeldaban", t.player.talk_to, "dwarf_city_black_guard_leader", 1)
        t.exec("talkToVeldaban.chat", t.chat.play, {
            "npc:Welcome to Keldagrim",
            "npc:I want you to go",
            "npc:I need you to go to Blasidar",
            "choose:Yes, I will do this.",
            "player:Yes, I will do this.",
        })
        t.ticks(3)
        t.expect("quest.stage.veldaban_done", t.quest.expect_stage("veldaban_done"))

        t.exec("goto-talkToBlasidar", t.player.goto_tile, 2906, 10203, 0)
        t.exec("talkToBlasidar", t.player.talk_to, "dwarf_city_shop_sculpture", 1)
        t.exec("talkToBlasidar.chat", t.chat.play, {
            "npc:Ah, a human!",
            "npc:I'm building a grand statue",
            "choose:Yes, I will do this.",
            "player:Yes, I will do this.",
            "npc:Wonderful! Vermundi",
        })
        t.ticks(3)
        t.expect("quest.stage.blasidar_done", t.quest.expect_stage("blasidar_done"))

        t.exec("goto-talkToVermundi", t.player.goto_tile, 2887, 10186, 0)
        t.exec("talkToVermundi", t.player.talk_to, "dwarf_city_shop_cloth_poor", 1)
        t.exec("talkToVermundi.chat", t.chat.play, {
            "player:Yes, I'm looking for some special clothes.",
            "npc:Clothes fit for King Alvis",
        })
        t.ticks(3)
        t.expect("quest.stage.vermundi_asked", t.quest.expect_stage("vermundi_asked"))

        t.exec("goto-talkToLibrarian", t.player.goto_tile, 2861, 10224, 0)
        t.exec("talkToLibrarian", t.player.talk_to, "dwarf_city_librarian", 1)
        t.exec("talkToLibrarian.chat", t.chat.play, {
            "player:Do you know anything about King Alvis' clothes?",
            "npc:There is a book on dwarven costumes",
        })
        t.ticks(3)
        t.expect("quest.stage.librarian_asked", t.quest.expect_stage("librarian_asked"))

        t.exec("goto-climbBookcase", t.player.goto_tile, 2859, 10226, 0)
        t.exec("climbBookcase", t.player.click_loc, "dwarf_keldagrim_bookcase_ladder", 1)
        t.ticks(3)
        t.expect("quest.stage.got_book", t.quest.expect_stage("got_book"))
        local _, book = t.inv.count("dwarf_library_book")
        t.check("climbBookcase.book", book == 1, "Scholars Guide in backpack count=" .. tostring(book))

        t.exec("goto-talkToVermundiAfterBook", t.player.goto_tile, 2887, 10186, 0)
        t.exec("talkToVermundiWithBook", t.player.talk_to, "dwarf_city_shop_cloth_poor", 1)
        t.exec("talkToVermundiWithBook.chat", t.chat.play, {
            "player:Yes, about those special clothes again...",
            "npc:Ah, the Scholars Guide",
        })
        t.ticks(3)
        t.expect("quest.stage.showed_book", t.quest.expect_stage("showed_book"))

        -- Leg 2
        t.exec("goto-useCoalOnMachine", t.player.goto_tile, 2885, 10187, 0)
        local machine = t.player.by_symbol("loc", "dwarf_keldagrim_spinning_machine")
        t.exec("useCoalOnMachine", t.player.use_on, "coal", machine)
        t.ticks(3)
        t.expect("quest.stage.machine_loaded", t.quest.expect_stage("machine_loaded"))
        machine = t.player.by_symbol("loc", "dwarf_keldagrim_spinning_machine")
        t.exec("startMachine", t.player.use_on, "tinderbox", machine)
        t.ticks(3)
        t.expect("quest.stage.machine_started", t.quest.expect_stage("machine_started"))

        t.exec("talkToVermundiWithMachine", t.player.talk_to, "dwarf_city_shop_cloth_poor", 1)
        t.exec("talkToVermundiWithMachine.chat", t.chat.play, {
            "player:Yes, about those special clothes again...",
            "npc:The machine's running now",
            "choose:I'll pay.",
            "player:I'll pay.",
            "mesbox:Vermundi spins you",
        })
        t.ticks(3)
        t.expect("quest.stage.clothes_done", t.quest.expect_stage("clothes_done"))
        local _, clothes = t.inv.count("dwarf_clothes")
        t.check("clothes.received", clothes == 1, "exquisite clothes count=" .. tostring(clothes))

        t.exec("goto-talkToSaro", t.player.goto_tile, 2827, 10196, 0)
        t.exec("talkToSaro", t.player.talk_to, "dwarf_city_shop_armour", 1)
        t.exec("talkToSaro.chat", t.chat.play, {
            "player:Yes, I'm looking for a pair of special boots.",
            "npc:Special boots?",
        })
        t.ticks(3)
        t.expect("quest.stage.saro_asked", t.quest.expect_stage("saro_asked"))

        t.exec("goto-talkToDromund", t.player.goto_tile, 2837, 10218, 0)
        t.exec("openDromundDoor", t.player.click_loc, "dwarf_keldagrim_door_ornate")
        t.exec("goto-talkToDromund-in", t.player.goto_tile, 2836, 10222, 0)
        t.exec("talkToDromund", t.player.talk_to, "dwarf_city_excentric_dwarf")
        t.exec("talkToDromund.chat", t.chat.play, { "player:Saro said", "npc:Get out you pesky human" })
        t.ticks(2)
        t.expect("quest.stage.dromund_asked", t.quest.expect_stage("dromund_asked"))

        local got = false
        for i = 1, 12 do
            local r, d = t.player.click_obj("dwarf_perfect_left_boot", 3)
            t.step("leftBootAttempt-" .. i, (r == "ok" or r == "timeout" or r == "refused") and "PASS" or "FAIL", tostring(r) .. " " .. tostring(d))
            t.ticks(2)
            local _, n = t.inv.count("dwarf_perfect_left_boot")
            if n and n > 0 then got = true break end
            t.chat.close()
            t.ticks(6)
        end
        t.check("takeLeftBoot", got, "left boot in backpack after waiting for Dromund to look away")
        t.ticks(2)
        t.expect("quest.stage.left_boot", t.quest.expect_stage("left_boot"))

        t.exec("goto-takeRightBoot", t.player.goto_tile, 2836, 10229, 0)
        local pair = false
        for i = 1, 12 do
            local r, d = t.player.cast("telegrab", { kind = "obj", id = "dwarf_perfect_right_boot" })
            t.step("rightBootAttempt-" .. i, (r == "ok" or r == "refused" or r == "timeout") and "PASS" or "FAIL", tostring(r) .. " " .. tostring(d))
            t.ticks(4)
            local _, nn = t.inv.count("dwarf_perfect_pair_of_boots")
            if nn and nn > 0 then pair = true break end
            t.chat.close()
            t.ticks(4)
        end
        t.check("takeRightBoot", pair, "exquisite pair in backpack after telekinetic grab")
        t.expect("quest.stage.boots_done", t.quest.expect_stage("boots_done"))

        t.exec("goto-talkToSantiri", t.player.goto_tile, 2828, 10229, 0)
        t.exec("talkToSantiri", t.player.talk_to, "dwarf_city_shop_weapons", 1)
        t.exec("talkToSantiri.chat", t.chat.play, {
            "player:Yes, I'm looking for a particular battleaxe.",
            "npc:A battleaxe, you say?",
            "player:Blasidar the sculptor needs it",
            "npc:Ah, I see.",
            "player:Perhaps I can repair the axe?",
        })
        t.ticks(3)
        t.expect("quest.stage.santiri_asked", t.quest.expect_stage("santiri_asked"))

        t.exec("useSapphires", t.player.use_item_on_item, "sapphire", "dwarf_battleaxe_old")
        t.ticks(3)
        t.expect("quest.stage.sapphires_used", t.quest.expect_stage("sapphires_used"))

        t.exec("goto-talkToLibrarianAboutImcando", t.player.goto_tile, 2861, 10224, 0)
        t.exec("talkToLibrarianAboutImcando", t.player.talk_to, "dwarf_city_librarian", 1)
        t.exec("talkToLibrarianAboutImcando.chat", t.chat.play, {
            "player:Can you help me find an Imcando dwarf?",
            "npc:Imcando dwarves?",
        })
        t.ticks(3)
        t.expect("quest.stage.imcando_asked", t.quest.expect_stage("imcando_asked"))

        t.exec("goto-talkToReldo", t.player.goto_tile, 3211, 3492, 0)
        t.exec("talkToReldo", t.player.talk_to, "reldo_normal", 1)
        t.exec("talkToReldo.chat", t.chat.play, {
            "player:Ask about Imcando dwarves.",
            "npc:Imcando dwarves?",
        })
        t.ticks(3)
        t.expect("quest.stage.reldo_told", t.quest.expect_stage("reldo_told"))

        t.exec("goto-talkToThurgo", t.player.goto_tile, 3001, 3145, 0)
        t.exec("talkToThurgo", t.player.talk_to, "thurgo", 1)
        t.exec("talkToThurgo.chat", t.chat.play, {
            "player:Would you like a redberry pie?",
            "npc:You make excellent redberry pies",
            "player:Can you repair that axe now?",
            "mesbox:Thurgo hammers",
            "player:Return to Keldagrim immediately.",
        })
        t.ticks(3)
        t.expect("quest.stage.axe_done", t.quest.expect_stage("axe_done"))

        t.exec("goto-giveItemsToRiki", t.player.goto_tile, 2904, 10205, 0)
        t.exec("giveItemsToRiki", t.player.talk_to, "dwarf_city_shop_sculpture_model_multi", 1)
        t.exec("giveItemsToRiki.chat", t.chat.play, {
            "npc:Thank...you.",
            "npc:Thank...you.",
            "npc:Thank...you.",
            "mesbox:Riki the model stands dressed",
        })
        t.ticks(3)
        t.expect("quest.stage.items_given", t.quest.expect_stage("items_given"))

        t.exec("goto-talkToBlasidarAfterItems", t.player.goto_tile, 2906, 10203, 0)
        t.exec("talkToBlasidarAfterItems", t.player.talk_to, "dwarf_city_shop_sculpture", 1)
        t.exec("talkToBlasidarAfterItems.chat", t.chat.play, {
            "player:I've given Riki the clothes",
            "npc:Wonderful! Let me take a look",
            "npc:Now, to win the Black Guard's trust",
        })
        t.ticks(3)
        t.expect("quest.stage.blasidar_after", t.quest.expect_stage("blasidar_after"))

        -- Leg 3: the consortium
        t.exec("goto-enterConsortium", t.player.goto_tile, 2895, 10209, 0)
        t.exec("enterConsortium", t.player.click_loc, "dwarf_keldagrim_wide_stairs_lower")
        t.ticks(3)
        t.expect("quest.stage.entered_consortium", t.quest.expect_stage("entered_consortium"))

        -- The tasks are random; the ores/bars are MINED and SMELTED for real
        -- (Keldagrim's own rocks and furnace), and a task we cannot mine
        -- (clay, silver, gold, mithril, steel...) is turned down with
        -- "No thanks." (the quest's own refusal, -2 points).
        local ROCKS = {
            [2] = { ore = "copper_ore", a = "copperrock1", b = "copperrock2", at_x = 2871, at_z = 10117 },
            [3] = { ore = "tin_ore", a = "tinrock1", b = "tinrock2", at_x = 2856, at_z = 10160 },
            [4] = { ore = "iron_ore", a = "ironrock1", b = "ironrock2", at_x = 2830, at_z = 10152 },
            [8] = { ore = "coal", a = "coalrock1", b = "coalrock2", at_x = 2864, at_z = 10120 },
        }

        for i = 1, 40 do
            t.exec("goto-talkToSecretary-" .. i, t.player.goto_tile, 2869, 10204, 1)
            t.exec("talkToSecretary-" .. i, t.player.talk_to, "dwarf_city_secretary_blue_opal")
            for k = 1, 12 do
                local kind = t.chat.kind()
                if kind == "options" then
                    local _, tk = t.var.server("varp7199_gdwarf_task")
                    if tk and ROCKS[tk] then
                        if t.chat.choose("I'll take it.") ~= "ok" then t.chat.choose("I'll keep looking.") end
                    else
                        if t.chat.choose("No thanks.") ~= "ok" then t.chat.choose("I'll keep looking.") end
                    end
                elseif kind == "none" then break
                else t.chat.continue_() end
                t.ticks(2)
            end
            t.ticks(2)
            local _, task = t.var.server("varp7199_gdwarf_task")
            local _, count = t.var.server("varp7200_gdwarf_task_count")
            local _, points = t.var.server("varp7198_gdwarf_points")
            local _, st = t.quest.stage()
            if st and st >= 23 then
                t.check("talkToSecretary-done-" .. i, true, "secretary finished, points=" .. tostring(points))
                break
            end
            local rock = task and ROCKS[task]
            if rock and count and count > 0 then
                t.exec("goto-mine-" .. rock.ore .. "-" .. i, t.player.goto_tile, rock.at_x, rock.at_z, 0)
                local got = 0
                for att = 1, 80 do
                    local _, n = t.inv.count(rock.ore)
                    got = n or 0
                    if got >= count then break end
                    t.player.click_loc((att % 2 == 1) and rock.a or rock.b, 1)
                    t.inv.await(rock.ore, got + 1, 12)
                end
                t.check("mine-" .. rock.ore .. "-" .. i, got >= count,
                    "mined " .. tostring(got) .. "/" .. tostring(count) .. " " .. rock.ore .. " points=" .. tostring(points))
            else
                t.check("talkToSecretary-refused-" .. i, true, "task " .. tostring(task) .. " turned down, points=" .. tostring(points))
            end
        end
        t.expect("quest.stage.secretary_done", t.quest.expect_stage("secretary_done"))

        for i = 1, 40 do
            t.exec("goto-talkToDirector-" .. i, t.player.goto_tile, 2879, 10198, 1)
            t.exec("talkToDirector-" .. i, t.player.talk_to, "dwarf_city_director_blue_opal")
            for k = 1, 12 do
                local kind = t.chat.kind()
                if kind == "options" then
                    local _, tk = t.var.server("varp7199_gdwarf_task")
                    if tk == 1 or tk == 2 then
                        if t.chat.choose("I'll take it.") ~= "ok" then t.chat.choose("I'll keep looking.") end
                    else
                        if t.chat.choose("No thanks.") ~= "ok" then t.chat.choose("I'll keep looking.") end
                    end
                elseif kind == "none" then break
                else t.chat.continue_() end
                t.ticks(2)
            end
            t.ticks(2)
            local _, task = t.var.server("varp7199_gdwarf_task")
            local _, count = t.var.server("varp7200_gdwarf_task_count")
            local _, points = t.var.server("varp7198_gdwarf_points")
            local _, st = t.quest.stage()
            if st and st >= 24 then
                t.check("talkToDirector-done-" .. i, true, "director finished, points=" .. tostring(points))
                break
            end
            if (task == 1 or task == 2) and count and count > 0 then
                local bar = (task == 1) and "bronze_bar" or "iron_bar"
                local mines = (task == 1) and { 2, 3 } or { 4 }
                for _, which in ipairs(mines) do
                    local rock = ROCKS[which]
                    t.exec("goto-mine-" .. rock.ore .. "-" .. i, t.player.goto_tile, rock.at_x, rock.at_z, 0)
                    local want = (task == 1) and count or (count * 2 + 2)
                    local got = 0
                    for att = 1, 120 do
                        local _, n = t.inv.count(rock.ore)
                        got = n or 0
                        if got >= want then break end
                        t.player.click_loc((att % 2 == 1) and rock.a or rock.b, 1)
                        t.inv.await(rock.ore, got + 1, 12)
                    end
                    t.check("mine-" .. rock.ore .. "-" .. i, got >= want,
                        "mined " .. tostring(got) .. "/" .. tostring(want) .. " " .. rock.ore)
                end
                t.exec("goto-furnace-" .. i, t.player.goto_tile, 2869, 10202, 0)
                local furnace = t.player.by_symbol("loc", "dwarf_keldagrim_furnace")
                local made = 0
                for att = 1, 60 do
                    local _, nb = t.inv.count(bar)
                    made = nb or 0
                    if made >= count then break end
                    t.player.use_on((task == 1) and "copper_ore" or "iron_ore", furnace)
                    t.chat.close()
                    t.inv.await(bar, made + 1, 8)
                end
                t.check("smelt-" .. bar .. "-" .. i, made >= count,
                    "smelted " .. tostring(made) .. "/" .. tostring(count) .. " " .. bar .. " at the furnace")
            else
                t.check("talkToDirector-refused-" .. i, true, "task " .. tostring(task) .. " turned down, points=" .. tostring(points))
            end
        end
        t.expect("quest.stage.director_done", t.quest.expect_stage("director_done"))

        t.exec("joinCompany", t.player.talk_to, "dwarf_city_director_blue_opal")
        t.exec("joinCompany.chat", t.chat.play, {
            "npc:Have you ever considered joining",
            "choose:I'd like to officially join your company.",
            "player:I'd like to officially join your company.",
            "npc:It is agreed then!",
        })
        t.ticks(2)
        t.expect("quest.stage.joined_company", t.quest.expect_stage("joined_company"))

        t.exec("talkToDirectorAfterJoining", t.player.talk_to, "dwarf_city_director_blue_opal")
        t.exec("talkToDirectorAfterJoining.chat", t.chat.play, {
            "npc:Blasidar the sculptor has sent you?",
            "player:Blasidar the sculptor has sent me.",
            "npc:Then I will remember it",
            "player:Yes! Long live",
        })
        t.ticks(2)
        t.expect("quest.stage.director_after", t.quest.expect_stage("director_after"))

        t.exec("goto-leaveConsortium", t.player.goto_tile, 2863, 10209, 1)
        t.exec("leaveConsortium", t.player.click_loc, "dwarf_keldagrim_wide_stairs_upper")
        t.ticks(3)
        t.expect("quest.stage.ready_to_finish", t.quest.expect_stage("ready_to_finish"))

        local _, before = t.skill.snapshot()
        t.exec("goto-talkToVeldabanAfterJoining", t.player.goto_tile, 2828, 10215, 0)
        t.exec("talkToVeldabanAfterJoining", t.player.talk_to, "dwarf_city_black_guard_leader")
        t.exec("talkToVeldabanAfterJoining.chat", t.chat.play, {
            "player:I've been accepted into the consortium",
            "npc:Excellent work, human.",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.check("reward.mining", t.skill.expect_gain("mining", 2500, before))
        t.check("reward.smithing", t.skill.expect_gain("smithing", 2500, before))
        t.check("reward.crafting", t.skill.expect_gain("crafting", 2500, before))
        t.check("reward.magic", t.skill.expect_gain("magic", 1500, before))
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1500, before))
        t.check("reward.firemaking", t.skill.expect_gain("firemaking", 1500, before))
        t.finish(0)
    end,
}
