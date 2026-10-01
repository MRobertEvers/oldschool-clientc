-- A Forgettable Tale of a Drunken Dwarf (quest_forgettabletale), a 12-leg relay
-- (docs/quest_authoring/relay.md). Ladder: python3 tools/quest_gate/ladder.py forgettabletale --leg K.
-- Each leg is self-contained: it shares no Lua local with another leg.

return {
    id = "forgettabletale",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- Quest Helper requirements: The Giant Dwarf + Fishing Contest (forget_meets_quest_reqs),
        -- Cooking 22 and Farming 17 (Veldaban's mesbox)
        "::complete quest_giantdwarf",
        "::complete quest_fishingcompo",
        "::setlevel cooking 22",
        "::setlevel farming 17",
        -- Quest Helper items brought along: 2 barley malt, 2 buckets of water, dibber, rake,
        -- about 400 coins (Veldaban), a dwarven stout (Khorvak's drink), a beer (the Drunken
        -- Dwarf) and an empty beer glass (Gauss's toast; leg 3+), a kebab for the last step.
        "::give barley_malt 2",
        "::give bucket_water 2",
        "::give dibber 1",
        "::give rake 1",
        "::give coins 500",
        "::give dwarven_stout 1",
        "::give beer 1",
        "::give beer_glass 1",
        "::give kebab 1",
        -- leg 2: a spade to harvest the kelda patch (forget_farming.rs2 [oploc1,kelda_hops_fullygrown])
        "::give spade 1",
    },
    bind = {
        varp = "varb822_forget_quest",
        constants = {
            not_started = 0, talk_drunkdwarf = 10, bring_beer = 20, collect_seeds = 30,
            grow_kelda = 40, brew_stout = 50, give_stout = 60, hear_more = 65,
            ask_conductor = 70, ask_director = 80, tunnels_first = 100, listening_room = 105,
            tunnels_second = 110, library = 115, tunnels_third = 118, last_cutscene = 119,
            report_veldaban = 120, eat_kebab = 130, complete = 140,
        },
        row = "quest_forgettabletale",
        display = "Forgettable Tale...",
        points = 2,
    },
    legs = {
        { name = "keldagrim_seeds", run = function(t)
            -- LEG 1: travelToKeldagrim .. talkToGauss (the first three kelda seeds' givers)
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            t.exec("goto-travelToKeldagrim", t.player.goto_tile, 3140, 3504, 0)
            t.exec("travelToKeldagrim", t.player.click_loc, "ge_keldagrim_trapdoor", 1)
            t.exec("travelToKeldagrim-dialog", t.chat.play, {
                "mesbox:The trapdoor leads down",
                "choose:Yes please.",
                "player:Yes please.",
            })
            t.ticks(6)
            local _, tile = t.world.tile()
            t.check("travelToKeldagrim-arrived", tile ~= nil, "after the trapdoor the player stands at " .. tostring(tile))

            t.exec("goto-talkToVeldaban", t.player.goto_tile, 2826, 10215, 0)
            t.exec("talkToVeldaban", t.player.talk_to, "dwarf_city_black_guard_leader", 1)
            t.exec("talkToVeldaban-dialog", t.chat.play, {
                "npc:Ah, human. The Consortium",
                "npc:Half the world",
                "npc:Have you heard of the Red Axe",
                "npc:Are you interested",
                "choose:Very interested!",
                "player:Very interested!",
                "npc:Good. There is a dwarf",
                "choose:Yes.",
                "player:Yes.",
                "npc:Then you know",
                "npc:Our questions",
                "npc:Will you speak",
                "choose:Sounds like just the job for me!",
                "player:Sounds like just the job",
                "mesbox:You will need level",
                "npc:Excellent",
            })
            t.ticks(4)
            t.expect("quest.stage.talk_drunkdwarf", t.quest.expect_stage("talk_drunkdwarf"))

            t.exec("goto-talkToDrunkDwarf", t.player.goto_tile, 2913, 10221, 0)
            t.exec("talkToDrunkDwarf", t.player.talk_to, "dwarf_city_drunken_dwarf", 1)
            t.exec("talkToDrunkDwarf-dialog", t.chat.play, {
                "npc:*hic* Who're you?",
                "choose:I need to know about the Red Axe...",
                "player:I need to know about the Red Axe",
                "npc:*hic* The Red Axe?",
            })
            t.ticks(3)
            t.expect("quest.stage.bring_beer", t.quest.expect_stage("bring_beer"))

            -- goTalkDrunkDwarfAgain: give the Drunken Dwarf the beer
            t.exec("goTalkDrunkDwarfAgain", t.player.talk_to, "dwarf_city_drunken_dwarf", 1)
            t.exec("goTalkDrunkDwarfAgain-dialog", t.chat.play, {
                "player:Here, have a beer.",
                "npc:*hic* Ahh, cheers!",
                "npc:Truth is, one beer",
                "npc:I've still got one kelda seed",
                "mesbox:The Drunken Dwarf hands you a kelda seed",
            })
            t.ticks(3)
            t.expect("quest.stage.collect_seeds", t.quest.expect_stage("collect_seeds"))
            t.check("goTalkDrunkDwarfAgain-seed", select(2, t.inv.count("kelda_hop_seed")) == 1,
                "kelda seeds held: " .. tostring(select(2, t.inv.count("kelda_hop_seed"))))

            -- talkToRowdyDwarf: he names an item (a random one of 24); fetching arbitrary junk
            -- such as acne potions is not a guide step, so the named item is staged.
            t.exec("goto-talkToRowdyDwarf", t.player.goto_tile, 2914, 10198, 0)
            t.exec("talkToRowdyDwarf", t.player.talk_to, "dwarf_city_rowdy_dwarf", 1)
            t.exec("talkToRowdyDwarf-dialog", t.chat.play, {
                "player:The Drunken Dwarf said you might",
                "npc:Ha! Maybe I do.",
            })
            t.ticks(2)
            local rowdy_items = { "acne_potion", "amulet_mould", "anchovie_pizza", "ashes", "banana", "bat_bones",
                "broken_glass", "feud_camel_pooh_bucket", "burnt_chicken", "cabbage", "eye_patch",
                "feud_karidian_fakebeard", "flier", "king_worm", "pink_skirt", "potato", "red_cape",
                "spinning_plate", "studded_chaps", "steel_dart", "swamp_tar", "horsey_brown", "vampire_dust",
                "white_apron" }
            local want_result, want = t.var.server("varp7194_forget_rowdy_item")
            if want_result ~= "ok" then want_result, want = t.var.varbit("varp7194_forget_rowdy_item") end
            local item = rowdy_items[want]
            t.check("talkToRowdyDwarf-request", item ~= nil, "Rowdy asked for item index " .. tostring(want) .. " (" .. tostring(want_result) .. ")" .. " = " .. tostring(item))
            if item == nil then t.blocked("seam: cannot read forget_rowdy_item (" .. tostring(want_result) .. ")") return end
            t.cheat("::give " .. item .. " 1")
            t.ticks(2)
            t.exec("talkToRowdyDwarf-trade", t.player.talk_to, "dwarf_city_rowdy_dwarf", 1)
            t.exec("talkToRowdyDwarf-trade-dialog", t.chat.play, {
                "player:The Drunken Dwarf said you might",
                "player:I've brought you a",
                "npc:Ha, that's just what I wanted!",
                "mesbox:The Rowdy Dwarf hands you a kelda seed",
            })
            t.ticks(2)
            t.check("talkToRowdyDwarf-seed", select(2, t.inv.count("kelda_hop_seed")) == 2,
                "kelda seeds held: " .. tostring(select(2, t.inv.count("kelda_hop_seed"))))

            -- travelToWWM: the ticket first (getWWMTicket), then the cart
            t.exec("goto-getWWMTicket", t.player.goto_tile, 2906, 10171, 0)
            t.exec("getWWMTicket", t.player.talk_to, "dwarf_city_train_conductor4", 1)
            t.exec("getWWMTicket-dialog", t.chat.play, {
                "npc:Welcome to the cart station",
                "choose:I'd like to buy a ticket.",
                "player:I'd like to buy a ticket.",
                "npc:Where to?",
                "choose:To White Wolf Mountain.",
                "player:To White Wolf Mountain.",
                "npc:That will be",
                "choose:Buy.",
                "player:Buy.",
                "mesbox:You buy a ticket",
            })
            t.ticks(2)
            t.check("getWWMTicket-ticket", select(2, t.inv.count("dwarf_minecart_ticket_kelda_whitewolf")) == 1,
                "tickets held: " .. tostring(select(2, t.inv.count("dwarf_minecart_ticket_kelda_whitewolf"))))
            t.exec("goto-travelToWWM", t.player.goto_tile, 2919, 10169, 0)
            t.exec("travelToWWM", t.player.click_loc, "keldagrim_train_cart", 1)
            t.ticks(8)
            local _, at = t.world.tile()
            t.check("travelToWWM-arrived", at ~= nil, "after the cart the player stands at " .. tostring(at))

            -- talkToKhorvak, then goGiveKhorvakBeer: one conversation in the content
            -- (forget_khorvak_talk): the drink offer, then the stout is handed over at once.
            t.exec("goto-talkToKhorvak", t.player.goto_tile, 2864, 9878, 0)
            t.exec("talkToKhorvak", t.player.talk_to, "dwarfrock_engineer2", 1)
            t.exec("talkToKhorvak-dialog", t.chat.play, {
                "player:The Drunken Dwarf said you might",
                "npc:Maybe I do.",
                "choose:What if I offer you a drink?",
                "player:What if I offer you a drink?",
                "npc:Now you're talking.",
                "player:I have one right here.",
                "npc:Ha, cheers!",
                "mesbox:Khorvak hands you a kelda seed",
            })
            t.ticks(2)
            t.check("goGiveKhorvakBeer", select(2, t.inv.count("kelda_hop_seed")) == 3
                and select(2, t.inv.count("dwarven_stout")) == 0,
                "stout given, kelda seeds held: " .. tostring(select(2, t.inv.count("kelda_hop_seed")))
                .. ", stouts: " .. tostring(select(2, t.inv.count("dwarven_stout"))))

            -- back to Keldagrim by the return cart, then Gauss
            t.exec("goto-takeCartFromWWMToKelda", t.player.goto_tile, 2875, 9868, 0)
            t.exec("getKeldaTicket", t.player.talk_to, "dwarf_city_train_conductor6", 1)
            t.exec("getKeldaTicket-dialog", t.chat.play, {
                "npc:Welcome to the cart station",
                "choose:I'd like to buy a ticket.",
                "player:I'd like to buy a ticket.",
                "npc:That will be",
                "choose:Buy.",
                "player:Buy.",
                "mesbox:You buy a ticket",
            })
            t.ticks(2)
            t.exec("takeCartFromWWMToKelda", t.player.click_loc, "whitewolfmountain_train_cart", 1)
            t.ticks(8)

            t.exec("goto-talkToGauss", t.player.goto_tile, 2838, 10195, 0)
            t.exec("talkToGauss", t.player.talk_to, "dwarf_city_dwarf_man6", 1)
            t.exec("talkToGauss-dialog", t.chat.play, {
                "player:The Drunken Dwarf said you had",
                "npc:Ha! So he did tell you.",
                "npc:Fetch a drink",
            })
            t.ticks(2)

            local _, sx = t.world.tile()
            local _, lvl = t.world.level()
            local _, stage = t.var.server("varb822_forget_quest")
            t.check("leg.1.end", true, "tile " .. tostring(type(sx) == "table" and (tostring(sx.x) .. "," .. tostring(sx.z)) or sx) .. " level " .. tostring(lvl) .. ", forget_quest=" .. tostring(stage)
                .. ", seeds " .. tostring(select(2, t.inv.count("kelda_hop_seed"))))
        end },
        { name = "kelda_patch", run = function(t)
            -- LEG 2: the toast with Gauss (4th seed), Rind, rake + plant, the real wait, harvest
            t.exec("goto-talkToGauss-toast", t.player.goto_tile, 2838, 10195, 0)
            t.exec("talkToGauss-toast", t.player.talk_to, "dwarf_city_dwarf_man6", 1)
            t.exec("talkToGauss-toast-dialog", t.chat.play, {
                "player:The Drunken Dwarf said you had",
                "player:To the Drunken Dwarf",
                "npc:To the kelda!",
                "mesbox:Gauss hands you a kelda seed",
            })
            t.ticks(2)
            t.check("talkToGauss-seed", select(2, t.inv.count("kelda_hop_seed")) == 4,
                "kelda seeds held: " .. tostring(select(2, t.inv.count("kelda_hop_seed"))))

            t.exec("goto-talkToRind", t.player.goto_tile, 2854, 10197, 0)
            t.exec("talkToRind", t.player.talk_to, "dwarf_city_gardener_dwarf", 1)
            t.exec("talkToRind-dialog", t.chat.play, {
                "player:I've got four kelda seeds",
                "npc:Kelda hops!",
            })
            t.ticks(2)
            t.expect("talkToRind-stage", t.quest.expect_stage("grow_kelda"))

            -- plantKelda: rake the weeds, then use the seeds on the weeded patch
            t.exec("goto-plantKelda", t.player.goto_tile, 2854, 10201, 0)
            t.exec("plantKelda-rake", t.player.click_loc, "farming_hops_patch_keldagrim", 1)
            t.exec("plantKelda-weeded", t.var.await, "varb823_forget_farming", 3, 40)
            local patch = t.player.by_symbol("loc", "farming_hops_patch_keldagrim")
            t.exec("plantKelda", t.player.use_on, "kelda_hop_seed", patch)
            t.exec("plantKelda-growing", t.var.await, "varb823_forget_farming", 4, 12)
            t.check("plantKelda-planted", select(2, t.inv.count("kelda_hop_seed")) == 0,
                "kelda seeds left: " .. tostring(select(2, t.inv.count("kelda_hop_seed"))))

            -- waitForKelda: forget_farming.rs2 [proc,forget_kelda_catchup] advances one growth
            -- stage per ^forget_kelda_stage_minutes (4) of wall-clock date_minutes: 16 minutes.
            -- The documented fast-forward for a real-time wait is t.clock.skip; the quest's own
            -- forget_tick softtimer then runs the catch-up.
            t.exec("waitForKelda-skip", t.clock.skip, 16)
            t.exec("waitForKelda", t.var.await, "varb823_forget_farming", 8, 110)

            -- harvestHops: [oploc1,kelda_hops_fullygrown] (spade in setup)
            t.exec("harvestHops", t.player.click_loc, "farming_hops_patch_keldagrim", 1)
            t.exec("harvestHops-hops", t.inv.await, "kelda_hops", 1, 20)
            t.expect("quest.stage.brew_stout", t.quest.expect_stage("brew_stout"))

            -- goUpstairsPub: forget_brewing.rs2:10 [oploc1,dwarf_keldagrim_stairs_lower] (2916,10196,0)
            t.exec("goto-goUpstairsPub", t.player.goto_tile, 2914, 10196, 0)
            t.exec("goUpstairsPub", t.player.click_loc, "dwarf_keldagrim_stairs_lower", 1)
            t.ticks(2)
            local _, lvl2 = t.world.level()
            local _, tile2 = t.world.tile()
            t.check("goUpstairsPub-level", lvl2 == 1, "level " .. tostring(lvl2) .. " at "
                .. tostring(type(tile2) == "table" and (tile2.x .. "," .. tile2.z) or tile2))
            local _, stage = t.var.server("varb822_forget_quest")
            t.check("leg.2.end", lvl2 == 1, "tile " .. tostring(type(tile2) == "table" and (tile2.x .. "," .. tile2.z) or tile2)
                .. " level " .. tostring(lvl2) .. ", forget_quest=" .. tostring(stage)
                .. ", kelda_hops " .. tostring(select(2, t.inv.count("kelda_hops"))))
        end },
        { name = "brew_and_secret_cart", run = function(t)
            -- LEG 3: pot, yeast, vat, valve, barrel, stairs, conductor, director, secret cart
            local function n(sym) return select(2, t.inv.count(sym)) end
            t.exec("goto-pickupPot", t.player.goto_tile, 2916, 10192, 1)
            t.exec("pickupPot", t.player.click_obj, "pot_empty", 3)
            t.ticks(3)
            t.check("pickupPot-held", n("pot_empty") == 1, "pots " .. tostring(n("pot_empty")))
            t.exec("buyYeast", t.player.talk_to, "blandebir", 1)
            t.exec("buyYeast-dialog", t.chat.play, {
                "player:Do you have any spare ale yeast?",
                "npc:Ale yeast? Yes",
                "choose:That's a good deal - please fill my pot with ale yeast for 25GP.",
                "player:That's a good deal",
                "mesbox:Blandebir fills your pot",
            })
            t.ticks(2)
            t.check("buyYeast-held", n("ale_yeast") == 1, "yeast " .. tostring(n("ale_yeast")))
            local vat = t.player.by_symbol("loc", "brewing_vat_1")
            t.exec("addWater", t.player.use_on, "bucket_water", vat)
            t.exec("addWater-v", t.var.await_server, "varb736_brewing_vat_varbit_1", 1, 30)
            t.check("vat-client-varp", true, "client farming_varp_9 = " .. tostring(select(2, t.var.varp("varp510_farming_varp_9"))) .. " / vat varbit = " .. tostring(select(2, t.var.varbit("varb736_brewing_vat_varbit_1"))))
            t.exec("addMalts", t.player.use_on, "barley_malt", vat)
            t.exec("addMalts-v", t.var.await_server, "varb736_brewing_vat_varbit_1", 2, 30)
            t.exec("addKelda", t.player.use_on, "kelda_hops", vat)
            t.exec("addKelda-v", t.var.await_server, "varb736_brewing_vat_varbit_1", 68, 30)
            t.exec("addYeast", t.player.use_on, "ale_yeast", vat)
            t.exec("addYeast-v", t.var.await_server, "varb736_brewing_vat_varbit_1", 69, 30)
            t.exec("waitBrewing-skip", t.clock.skip, 40)
            t.exec("waitBrewing", t.var.await_server, "varb736_brewing_vat_varbit_1", 71, 130)
            t.exec("turnValve", t.player.click_loc, "vat_valve_1", 1)
            t.exec("turnValve-v", t.var.await_server, "varb738_brewing_barrel_varbit_1", 3, 30)
            local barrel = t.player.by_symbol("loc", "brewing_barrel_1")
            t.exec("useGlassOnBarrel", t.player.use_on, "beer_glass", barrel)
            t.exec("useGlassOnBarrel-v", t.inv.await, "kelda_stout", 1, 20)
            t.expect("quest.stage.give_stout", t.quest.expect_stage("give_stout"))
            t.exec("goDownFromPub", t.player.click_loc, "dwarf_keldagrim_stairs_upper", 1)
            t.ticks(2)
            t.check("goDownFromPub-level", select(2, t.world.level()) == 0, "level " .. tostring(select(2, t.world.level())))
            -- giveStout (goGiveDrunkenDwarfKelda): hand the drunken dwarf the stout, hear more
            t.exec("goto-giveStout", t.player.goto_tile, 2913, 10221, 0)
            t.exec("giveStout", t.player.talk_to, "dwarf_city_drunken_dwarf", 1)
            t.exec("giveStout-dialog", t.chat.play, {
                "player:I've brought you a kelda stout.",
                "npc:Is that... it can't be",
                "npc:*glug glug glug*",
            })
            t.ticks(2)
            t.expect("quest.stage.hear_more", t.quest.expect_stage("hear_more"))
            t.exec("hearMore", t.player.talk_to, "dwarf_city_drunken_dwarf", 1)
            t.exec("hearMore-dialog", t.chat.play, {
                "npc:Right, where was I",
                "npc:The tunnel was boarded up",
                "player:Thank you, that's exactly",
            })
            t.ticks(2)
            t.expect("quest.stage.ask_conductor", t.quest.expect_stage("ask_conductor"))
            -- talkToCartConductor
            t.exec("goto-talkToCartConductor", t.player.goto_tile, 2922, 10168, 0)
            t.exec("talkToCartConductor", t.player.talk_to, "dwarf_city_train_conductor8", 1)
            t.exec("talkToCartConductor-dialog", t.chat.play, {
                "npc:Welcome to the cart station",
                "choose:Ask about closed off tunnel.",
                "player:Ask about closed off tunnel.",
                "npc:Ah, the old spur",
                "npc:It is their tunnel now",
            })
            t.ticks(2)
            t.expect("quest.stage.ask_director", t.quest.expect_stage("ask_director"))
            t.exec("goto-goUpToDirector", t.player.goto_tile, 2895, 10208, 0)
            t.exec("goUpToDirector", t.player.click_loc, "dwarf_keldagrim_wide_stairs_lower", 1)
            t.ticks(3)
            t.check("goUpToDirector-level", select(2, t.world.level()) == 1, "level " .. tostring(select(2, t.world.level())))
            t.exec("goto-talkToDirector", t.player.goto_tile, 2869, 10203, 1)
            t.exec("talkToDirector", t.player.talk_to, "dwarf_city_director_blue_opal", 1)
            t.exec("talkToDirector-dialog", t.chat.play, {
                "npc:Yes? I am busy",
                "choose:Can you help me with a boarded up tunnel?",
                "player:Can you help me with a boarded up tunnel?",
                "npc:The old spur",
                "npc:Very well",
            })
            t.ticks(2)
            t.expect("quest.stage.tunnels_first", t.quest.expect_stage("tunnels_first"))
            t.exec("goto-goDownFromDirector", t.player.goto_tile, 2895, 10212, 1)
            t.exec("goDownFromDirector", t.player.click_loc, "dwarf_keldagrim_wide_stairs_upper", 1)
            t.ticks(3)
            -- takeSecretCart: empty hands, two free slots
            t.exec("goto-takeSecretCart", t.player.goto_tile, 2919, 10166, 0)
            t.exec("takeSecretCart", t.player.click_loc, "keldagrim_train_cart", 1)
            t.ticks(6)
            local _, at = t.world.tile()
            t.check("takeSecretCart-hub", type(at) == "table" and at.x >= 1850 and at.x <= 1870 and at.z >= 4950 and at.z <= 4960,
                "at " .. tostring(type(at) == "table" and (at.x .. "," .. at.z) or at) .. " level " .. tostring(select(2, t.world.level())))
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.3.end", type(endtile) == "table", "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. ", kelda_stout " .. tostring(select(2, t.inv.count("kelda_stout"))))
        end },
        { name = "puzzle_group_one", run = function(t)
            -- LEG 4 BEGIN: searchBox1
            local ROUTE1 = { [0] = 2, [2] = 1 } -- junction index (0-based) -> 1 green / 2 yellow (guide puzzle1P1/P2)
            local function stones(sym) return select(2, t.var.server(sym)) end
            t.exec("goto-hub", t.player.goto_tile, 1861, 4954, 1)
            t.exec("searchBox1", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.exec("startPuzzle1", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle1-open", t.ui.await_open, "forget_puzzle1", 30)
            local function com(i) return "forget_puzzle1:switch_" .. ("abcdefgh"):sub(i + 1, i + 1) end
            for i = 0, 7 do -- clear every junction first so its stone comes back
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == 0 then break end
                    local _, w = t.ui.widget(com(i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
            end
            local names = { [0] = "puzzle1P1", [2] = "puzzle1P2" }
            for _, i in ipairs({ 0, 2 }) do
                local want = ROUTE1[i]
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(names[i], v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            local _, okw = t.ui.widget("forget_puzzle1:ok_button")
            t.ui.invoke(okw, 0)
            local closed = t.ui.await_close("forget_puzzle1", 30)
            t.check("puzzle1Ok", closed == "ok", "interface closed: " .. tostring(closed)
                .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 1")
            t.exec("takePuzzle1Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.check("platform1-level", select(2, t.world.level()) == 3, "level " .. tostring(select(2, t.world.level())))
            t.exec("searchPuzzle1Box", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-box1", stones("varb862_forget_num_right") == 1, "green " .. tostring(stones("varb862_forget_num_right")))
            t.exec("returnFromPuzzle1", t.player.click_loc, "forget_train_return_cart", 1)
            t.ticks(6)
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.4.end", type(endtile) == "table" and select(2, t.world.level()) == 1,
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))))
            -- LEG 4 END
        end },
        { name = "puzzle_group_two", run = function(t)
            -- LEG 5 BEGIN: startPuzzle2
            -- junction index (0-based) -> 1 green / 2 yellow: group 1 route 2 (guide puzzle2P1..P3, forget_puzzle.rs2:93 route_expected 200/201/204)
            local ROUTE2 = { [0] = 1, [1] = 2, [4] = 1 }
            local ROUTE3 = { [0] = 2 } -- puzzle3P1 only; leg 6 sets the rest (route 3: 300/302/305/306)
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijkl"):sub(i + 1, i + 1) end
            local function clear(iface, count)
                for i = 0, count - 1 do -- clear every junction first so its stone comes back
                    for _ = 1, 3 do
                        if select(2, t.var.server("forget_if" .. (i + 1))) == 0 then break end
                        local _, w = t.ui.widget(com(iface, i))
                        t.ui.invoke(w, 0)
                        t.ticks(1)
                    end
                end
            end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            t.exec("goto-hub-routes", t.player.goto_tile, 1861, 4954, 1)
            t.exec("startPuzzle2", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle2-open", t.ui.await_open, "forget_puzzle1", 30)
            clear("forget_puzzle1", 8)
            setj("forget_puzzle1", 0, ROUTE2[0], "puzzle2P1")
            setj("forget_puzzle1", 1, ROUTE2[1], "puzzle2P2")
            setj("forget_puzzle1", 4, ROUTE2[4], "puzzle2P3")
            local _, okw = t.ui.widget("forget_puzzle1:ok_button")
            t.ui.invoke(okw, 0)
            local closed = t.ui.await_close("forget_puzzle1", 30)
            t.check("puzzle2Ok", closed == "ok" and select(2, t.var.server("varp7193_forget_route")) == 2,
                "interface closed: " .. tostring(closed) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 2")
            t.exec("takePuzzle2Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.check("platform2-level", select(2, t.world.level()) == 3, "level " .. tostring(select(2, t.world.level())))
            t.exec("searchPuzzle2Box", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-box2", true, "yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")))
            t.exec("returnFromPuzzle2", t.player.click_loc, "forget_train_return_cart", 1)
            t.ticks(6)
            t.exec("startPuzzle3", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle3-open", t.ui.await_open, "forget_puzzle1", 30)
            clear("forget_puzzle1", 8)
            setj("forget_puzzle1", 0, ROUTE3[0], "puzzle3P1")
            t.key("escape") -- leave the machinery open-free so the boundary is quiet; the junction stays set
            t.ticks(3)
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.5.end", type(endtile) == "table" and select(2, t.world.level()) == 1,
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. ", junction1=" .. tostring(select(2, t.var.server("varb842_forget_if1")))
                .. ", stones yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")))
            -- LEG 5 END
        end },
        { name = "listening_room", run = function(t)
            -- LEG 6 BEGIN: puzzle3P2
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijkl"):sub(i + 1, i + 1) end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            -- group 1 route 3 (forget_puzzle.rs2:93 route_expected 300/302/305/306); junction 0 (yellow) was set by leg 5
            t.exec("goto-hub-library", t.player.goto_tile, 1861, 4955, 1)
            t.exec("startPuzzle3-reopen", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle3-reopen-open", t.ui.await_open, "forget_puzzle1", 30)
            setj("forget_puzzle1", 2, 2, "puzzle3P2")
            setj("forget_puzzle1", 5, 1, "puzzle3P3")
            setj("forget_puzzle1", 6, 1, "puzzle3P4")
            local _, okw = t.ui.widget("forget_puzzle1:ok_button")
            t.ui.invoke(okw, 0)
            local closed = t.ui.await_close("forget_puzzle1", 30)
            t.check("puzzle3Ok", closed == "ok" and select(2, t.var.server("varp7193_forget_route")) == 3,
                "interface closed: " .. tostring(closed) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 3")
            t.exec("takePuzzle3Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.expect("quest.stage.listening_room", t.quest.expect_stage("listening_room"))
            t.check("listening-room-level", select(2, t.world.level()) == 2, "level " .. tostring(select(2, t.world.level())))
            -- the guide's leaveListeningRoom1 says "once you finish listening": the director's talk is forget_story.rs2:44 forget_listen
            t.exec("listenToConversation", t.player.talk_to, "forget_redaxe_director_cutscene_alt", 1)
            t.exec("listenToConversation-dialog", t.chat.play, {
                "npc:The Consortium is blind to it. Every cart in Keldagrim rolls right over our heads and not one of them has ever looked down.",
                "npc:And the tunnels reach all the way under the trading floor, boss?",
                "npc:All the way. When the time comes we come up in their own strongroom. The dwarves will not know what hit them.",
                "npc:Ogres want their share. Ogres want gold, and dwarf ale!",
                "npc:You will have both, once our friends from the west have delivered what they promised. Now, back to work, all of you.",
            })
            t.ticks(3)
            t.check("listened", select(2, t.var.server("varb870_forget_room1_listening")) == 1, "forget_room1_listening=" .. tostring(select(2, t.var.server("varb870_forget_room1_listening"))))
            t.exec("leaveListeningRoom1", t.player.click_loc, "forget_story_exit_next", 1)
            t.ticks(6)
            t.expect("quest.stage.tunnels_second", t.quest.expect_stage("tunnels_second"))
            t.exec("searchBox2", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-hub-box", stones("varb861_forget_num_left") == 2 and stones("varb862_forget_num_right") == 1,
                "yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. " (group 2 hub box: 2 yellow 1 green, forget_puzzle.rs2:330)")
            t.exec("startPuzzle4", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle4-open", t.ui.await_open, "forget_puzzle2", 30)
            -- group 2 route 4: 400=green, 401=yellow, 404=yellow (leg 7 sets 404)
            setj("forget_puzzle2", 0, 1, "puzzle4P1")
            setj("forget_puzzle2", 1, 2, "puzzle4P2")
            t.key("escape")
            t.ticks(3)
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.6.state", type(endtile) == "table" and select(2, t.world.level()) == 1,
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. ", junctions 1,2=" .. tostring(select(2, t.var.server("varb842_forget_if1"))) .. "," .. tostring(select(2, t.var.server("varb843_forget_if2")))
                .. ", stones yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right"))
                .. "; backpack unchanged (empty pot, buckets, dibber, rake, spade, coins, kebab)")
            -- LEG 6 END
        end },
        { name = "puzzle_group_two_routes", run = function(t)
            -- LEG 7 BEGIN: puzzle4P3
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijkl"):sub(i + 1, i + 1) end
            local function clear(iface, count)
                for i = 0, count - 1 do
                    for _ = 1, 3 do
                        if select(2, t.var.server("forget_if" .. (i + 1))) == 0 then break end
                        local _, w = t.ui.widget(com(iface, i))
                        t.ui.invoke(w, 0)
                        t.ticks(1)
                    end
                end
            end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            -- group 2 route 4: 400 green, 401 yellow, 404 yellow (forget_puzzle.rs2:93); junctions 0 and 1 were set by leg 6
            t.exec("goto-hub-nine", t.player.goto_tile, 1861, 4955, 1)
            t.exec("startPuzzle4-reopen", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle4-reopen-open", t.ui.await_open, "forget_puzzle2", 30)
            setj("forget_puzzle2", 4, 2, "puzzle4P3")
            local _, okw = t.ui.widget("forget_puzzle2:ok_button")
            t.ui.invoke(okw, 0)
            local closed = t.ui.await_close("forget_puzzle2", 30)
            t.check("puzzle4Ok", closed == "ok" and select(2, t.var.server("varp7193_forget_route")) == 4,
                "interface closed: " .. tostring(closed) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 4")
            t.exec("takePuzzle4Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.check("platform4-level", select(2, t.world.level()) == 3, "level " .. tostring(select(2, t.world.level())))
            t.exec("searchPuzzle4Box", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-box4", true, "yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")))
            t.exec("returnFromPuzzle4", t.player.click_loc, "forget_train_return_cart", 1)
            t.ticks(6)
            -- group 2 route 5: 500 yellow, 502 yellow, 505 green, 510 green
            t.exec("startPuzzle5", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle5-open", t.ui.await_open, "forget_puzzle2", 30)
            clear("forget_puzzle2", 12)
            setj("forget_puzzle2", 0, 2, "puzzle5P1")
            setj("forget_puzzle2", 2, 2, "puzzle5P2")
            setj("forget_puzzle2", 5, 1, "puzzle5P3")
            setj("forget_puzzle2", 10, 1, "puzzle5P4")
            local _, ok5 = t.ui.widget("forget_puzzle2:ok_button")
            t.ui.invoke(ok5, 0)
            local closed5 = t.ui.await_close("forget_puzzle2", 30)
            t.check("puzzle5Ok", closed5 == "ok" and select(2, t.var.server("varp7193_forget_route")) == 5,
                "interface closed: " .. tostring(closed5) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 5")
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.7.end", type(endtile) == "table" and select(2, t.world.level()) == 1,
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. ", route=5 set (cart not yet taken), stones yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right"))
                .. "; backpack unchanged (empty pot, buckets, dibber, rake, spade, coins, kebab)")
            -- LEG 7 END
        end },
        { name = "puzzle_group_two_library", run = function(t)
            -- LEG 8 BEGIN: takePuzzle5Cart
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijkl"):sub(i + 1, i + 1) end
            local function clear(iface, count)
                for i = 0, count - 1 do
                    for _ = 1, 3 do
                        if select(2, t.var.server("forget_if" .. (i + 1))) == 0 then break end
                        local _, w = t.ui.widget(com(iface, i))
                        t.ui.invoke(w, 0)
                        t.ticks(1)
                    end
                end
            end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            -- route 5 was set by leg 7; the cart goes to the medium platform (forget_puzzle.rs2:440)
            t.exec("takePuzzle5Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.check("platform5-level", select(2, t.world.level()) == 3, "level " .. tostring(select(2, t.world.level())))
            t.exec("searchPuzzle5Box", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-box5", true, "yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")))
            t.exec("returnFromPuzzle5", t.player.click_loc, "forget_train_return_cart", 1)
            t.ticks(6)
            -- group 2 route 6: 600 green, 601 green, 603 yellow, 609 green, 611 yellow (forget_puzzle.rs2:93)
            t.exec("startPuzzle6", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle6-open", t.ui.await_open, "forget_puzzle2", 30)
            clear("forget_puzzle2", 12)
            setj("forget_puzzle2", 0, 1, "puzzle6P1")
            setj("forget_puzzle2", 1, 1, "puzzle6P2")
            setj("forget_puzzle2", 3, 2, "puzzle6P3")
            setj("forget_puzzle2", 9, 1, "puzzle6P4")
            setj("forget_puzzle2", 11, 2, "puzzle6P5")
            local _, ok6 = t.ui.widget("forget_puzzle2:ok_button")
            t.ui.invoke(ok6, 0)
            local closed6 = t.ui.await_close("forget_puzzle2", 30)
            t.check("puzzle6Ok", closed6 == "ok" and select(2, t.var.server("varp7193_forget_route")) == 6,
                "interface closed: " .. tostring(closed6) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 6")
            t.exec("takePuzzle6Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.expect("quest.stage.library", t.quest.expect_stage("library"))
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.8.end", type(endtile) == "table",
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. " (library), stones yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right"))
                .. "; backpack unchanged (empty pot, buckets, dibber, rake, spade, coins, kebab)")
            -- LEG 8 END
        end },
        { name = "library_and_puzzle_seven", run = function(t)
            -- LEG 9 BEGIN: searchBookcase
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijklmnopqrst"):sub(i + 1, i + 1) end
            local function clear(iface, count)
                for i = 0, count - 1 do
                    for _ = 1, 3 do
                        if select(2, t.var.server("forget_if" .. (i + 1))) == 0 then break end
                        local _, w = t.ui.widget(com(iface, i))
                        t.ui.invoke(w, 0)
                        t.ticks(1)
                    end
                end
            end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            -- forget_story.rs2:61 the bookcase must be the one at 2_29_77_48_39 = 1904,4967
            t.exec("searchBookcase", t.player.click_loc, "dwarf_keldagrim_metal_bookcase", 1, { at = { 1904, 4967, 2 } })
            t.exec("searchBookcase-dialog", t.chat.play, { "mesbox:You search the bookcase and find a Red Axe employee handbook" })
            t.ticks(2)
            t.check("bookcase-read", select(2, t.var.server("varb833_forget_room2_bookcase")) == 1, "forget_room2_bookcase=" .. tostring(select(2, t.var.server("varb833_forget_room2_bookcase"))))
            t.exec("searchCrate2", t.player.click_loc, "forget_metal_crate_withpapers2", 1)
            t.exec("searchCrate2-dialog", t.chat.play, { "mesbox:You find a second sheet of paper" })
            t.ticks(2)
            t.exec("searchCrate1", t.player.click_loc, "forget_metal_crate_withpapers1", 1)
            t.exec("searchCrate1-dialog", t.chat.play, { "mesbox:You find a sheet of paper" })
            t.ticks(2)
            t.check("crates-read", select(2, t.var.server("varb834_forget_room2_paper1")) == 1 and select(2, t.var.server("varb835_forget_room2_paper2")) == 1,
                "paper1=" .. tostring(select(2, t.var.server("varb834_forget_room2_paper1"))) .. " paper2=" .. tostring(select(2, t.var.server("varb835_forget_room2_paper2"))))
            t.exec("leaveLibrary", t.player.click_loc, "forget_story_exit_next", 1)
            t.exec("leaveLibrary-dialog", t.chat.play, { "choose:Yes." })
            t.ticks(6)
            t.expect("quest.stage.tunnels_third", t.quest.expect_stage("tunnels_third"))
            t.exec("searchBox3", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-box3", true, "yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")))
            t.exec("startPuzzle7", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle7-open", t.ui.await_open, "forget_puzzle3", 30)
            clear("forget_puzzle3", 19)
            -- route 7 (forget_puzzle.rs2:93): 700 green, 701 green, 703 yellow, 705 yellow
            setj("forget_puzzle3", 0, 1, "puzzle7P1")
            setj("forget_puzzle3", 1, 1, "puzzle7P2")
            setj("forget_puzzle3", 3, 2, "puzzle7P3")
            setj("forget_puzzle3", 5, 2, "puzzle7P4")
            local _, ok7 = t.ui.widget("forget_puzzle3:ok_button")
            t.ui.invoke(ok7, 0)
            local closed7 = t.ui.await_close("forget_puzzle3", 30)
            t.check("puzzle7Ok", closed7 == "ok" and select(2, t.var.server("varp7193_forget_route")) == 7,
                "interface closed: " .. tostring(closed7) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 7")
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.9.end", type(endtile) == "table",
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. " (tunnels_third), route=7 set (cart not yet taken), stones yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right"))
                .. "; backpack unchanged (empty pot, buckets, dibber, rake, spade, coins, kebab)")
            -- LEG 9 END
        end },
        { name = "puzzle_seven_cart_and_eight", run = function(t)
            -- LEG 10 BEGIN: takePuzzle7Cart
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijklmnopqrst"):sub(i + 1, i + 1) end
            local function clear(iface, count)
                for i = 0, count - 1 do
                    for _ = 1, 3 do
                        if select(2, t.var.server("forget_if" .. (i + 1))) == 0 then break end
                        local _, w = t.ui.widget(com(iface, i))
                        t.ui.invoke(w, 0)
                        t.ticks(1)
                    end
                end
            end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                ;(name == "puzzle8P4-811" and t.expect or t.check)(name, v == want and "ok" or "mismatch", "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            -- route 7 was set by leg 9; the cart goes to the small platform (forget_puzzle.rs2:440)
            t.exec("takePuzzle7Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.check("platform7-level", select(2, t.world.level()) == 3, "level " .. tostring(select(2, t.world.level())))
            t.exec("searchPuzzle7Box", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-box7", true, "yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")))
            t.exec("returnFromPuzzle7", t.player.click_loc, "forget_train_return_cart", 1)
            t.ticks(6)
            -- route 8 (forget_puzzle.rs2:93): 800 yellow, 802 yellow, 804 yellow, 806 green, 808 green, 811 green
            t.exec("startPuzzle8", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle8-open", t.ui.await_open, "forget_puzzle3", 30)
            clear("forget_puzzle3", 19)
            setj("forget_puzzle3", 0, 2, "puzzle8P1")
            setj("forget_puzzle3", 2, 2, "puzzle8P2")
            setj("forget_puzzle3", 4, 2, "puzzle8P3")
            setj("forget_puzzle3", 6, 1, "puzzle8P4")
            setj("forget_puzzle3", 8, 1, "puzzle8P4-808")
            setj("forget_puzzle3", 11, 1, "puzzle8P4-811")
            t.key("escape") -- leave the machinery open-free so the boundary is quiet; the junctions stay set
            t.ticks(2)
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.10.end", type(endtile) == "table" and select(2, t.world.level()) == 1,
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. " (tunnels_third), route 8 junctions set, Ok not pressed, stones yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right"))
                .. "; backpack unchanged (empty pot, buckets, dibber, rake, spade, coins, kebab)")
            -- LEG 10 END
        end },
        { name = "puzzle_eight_cart_and_nine", run = function(t)
            -- LEG 11 BEGIN: puzzle8P5
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijklmnopqrst"):sub(i + 1, i + 1) end
            local function clear(iface, count)
                for i = 0, count - 1 do
                    for _ = 1, 3 do
                        if select(2, t.var.server("forget_if" .. (i + 1))) == 0 then break end
                        local _, w = t.ui.widget(com(iface, i))
                        t.ui.invoke(w, 0)
                        t.ticks(1)
                    end
                end
            end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            -- route 8 (forget_puzzle.rs2:93) was set by leg 10 and its interface closed: reopen, verify, press Ok
            t.exec("startPuzzle8-reopen", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle8-reopen-open", t.ui.await_open, "forget_puzzle3", 30)
            setj("forget_puzzle3", 6, 1, "puzzle8P5")
            setj("forget_puzzle3", 8, 1, "puzzle8P6")
            local _, ok8 = t.ui.widget("forget_puzzle3:ok_button")
            t.ui.invoke(ok8, 0)
            local closed8 = t.ui.await_close("forget_puzzle3", 30)
            t.check("puzzle8Ok", closed8 == "ok" and select(2, t.var.server("varp7193_forget_route")) == 8,
                "interface closed: " .. tostring(closed8) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 8")
            t.exec("takePuzzle8Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(6)
            t.check("platform8-level", select(2, t.world.level()) == 3, "level " .. tostring(select(2, t.world.level())))
            t.exec("searchPuzzle8Box", t.player.click_loc, "keldagrim_track_junction_card_box", 1)
            t.ticks(2)
            t.check("stones-after-box8", true, "yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")))
            t.exec("returnFromPuzzle8", t.player.click_loc, "forget_train_return_cart", 1)
            t.ticks(6)
            -- route 9 (forget_puzzle.rs2:114): 900 green, 901 green, 903 yellow, 905 green, 907 yellow; the guide steps in this leg are the first three
            t.exec("startPuzzle9", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle9-open", t.ui.await_open, "forget_puzzle3", 30)
            clear("forget_puzzle3", 19)
            setj("forget_puzzle3", 0, 1, "puzzle9P1")
            setj("forget_puzzle3", 1, 1, "puzzle9P2")
            setj("forget_puzzle3", 3, 2, "puzzle9P3")
            t.key("escape")
            t.ticks(2)
            local _, endtile = t.world.tile()
            local _, endstage = t.var.server("varb822_forget_quest")
            t.check("leg.11.end", type(endtile) == "table" and select(2, t.world.level()) == 1,
                "tile " .. tostring(type(endtile) == "table" and (endtile.x .. "," .. endtile.z) or endtile)
                .. " level " .. tostring(select(2, t.world.level())) .. ", forget_quest=" .. tostring(endstage)
                .. " (tunnels_third), route 9 junctions 0,1,3 set (5 green, 7 yellow left), stones yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right"))
                .. "; backpack unchanged (empty pot, buckets, dibber, rake, spade, coins, kebab)")
            -- LEG 11 END
        end },
        { name = "puzzle_nine_and_finale", run = function(t)
            -- LEG 12 BEGIN: puzzle9P4
            local function stones(sym) return select(2, t.var.server(sym)) end
            local function com(iface, i) return iface .. ":switch_" .. ("abcdefghijklmnopqrst"):sub(i + 1, i + 1) end
            local function setj(iface, i, want, name)
                for _ = 1, 3 do
                    if select(2, t.var.server("forget_if" .. (i + 1))) == want then break end
                    local _, w = t.ui.widget(com(iface, i))
                    t.ui.invoke(w, 0)
                    t.ticks(1)
                end
                local v = select(2, t.var.server("forget_if" .. (i + 1)))
                t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want
                    .. " (stones left yellow " .. tostring(stones("varb861_forget_num_left")) .. " green " .. tostring(stones("varb862_forget_num_right")) .. ")")
            end
            -- route 9 (forget_puzzle.rs2:114-125): 900 g, 901 g, 903 y, 905 g, 907 y, 910 g, 913 y, 916 y.
            -- Leg 11 set junctions 0,1,3 and closed the interface; the junctions persist.
            t.exec("startPuzzle9-reopen", t.player.click_loc, "keldagrim_track_junction_control_box", 1)
            t.exec("startPuzzle9-reopen-open", t.ui.await_open, "forget_puzzle3", 30)
            setj("forget_puzzle3", 5, 1, "puzzle9P4")
            setj("forget_puzzle3", 7, 2, "puzzle9P5")
            setj("forget_puzzle3", 10, 1, "puzzle9P6")
            setj("forget_puzzle3", 13, 2, "puzzle9P7")
            setj("forget_puzzle3", 16, 2, "puzzle9P8")
            local _, ok9 = t.ui.widget("forget_puzzle3:ok_button")
            t.ui.invoke(ok9, 0)
            local closed9 = t.ui.await_close("forget_puzzle3", 30)
            t.check("puzzle9Ok", closed9 == "ok" and select(2, t.var.server("varp7193_forget_route")) == 9,
                "interface closed: " .. tostring(closed9) .. ", forget_route=" .. tostring(select(2, t.var.server("varp7193_forget_route"))) .. " want 9")
            t.exec("takePuzzle9Cart", t.player.click_loc, "forget_train_cart", 1)
            t.ticks(8)
            t.exec("watchCutscene", t.chat.play, {
                "npc:The gnomes of the west",
                "npc:Splendid.",
                "npc:The chaos dwarves will hold",
                "npc:For the price",
                "npc:Wait. Who is that?",
                "npc:Human spy!",
                "mesbox:The ogre shaman raises",
                "npc:Forget the army!",
                "mesbox:You wake up beside the cart station",
            })
            t.ticks(2)
            t.expect("quest.stage.report_veldaban", t.quest.expect_stage("report_veldaban"))
            t.exec("goto-goReturnToVeldaban", t.player.goto_tile, 2826, 10215, 0)
            t.exec("goReturnToVeldaban", t.player.talk_to, "dwarf_city_black_guard_leader", 1)
            t.exec("goReturnToVeldaban-dialog", t.chat.play, {
                "npc:Ah, you are back!",
                "player:Yes, I found the Red Axe",
                "npc:Going to what?",
                "player:I... I do not remember",
                "npc:Human, that is no way",
                "npc:Whatever the Red Axe did",
            })
            t.check("goReturnToVeldaban-close", t.chat.continue_() == "ok", "continued the last page")
            t.ticks(2)
            t.expect("quest.stage.eat_kebab", t.quest.expect_stage("eat_kebab"))
            -- the kebab needs a beer (forget_pub.rs2:forget_kebab_finale); a beer is 2 coins from the barmaid
            t.exec("goto-bar", t.player.goto_tile, 2912, 10192, 0)
            t.exec("buyBeer", t.player.talk_to, "dwarf_city_barmaid_poor", 1)
            t.exec("buyBeer-dialog", t.chat.play, {
                "npc:Welcome! What can I get you?",
                "choose:A beer, please.",
                "player:A beer, please.",
                "npc:That'll be 2 coins.",
                "mesbox:You buy a beer",
            })
            t.ticks(2)
            local snap_result, snap = t.skill.snapshot()
            t.check("rewards.snapshot", snap_result == "ok", "skill.snapshot before the kebab -> " .. tostring(snap_result))
            t.exec("eatKebab", t.player.inv_op, "kebab", 1)
            t.ticks(6)
            t.exec("eatKebab-dialog", t.chat.play, {
                "player:Commander Veldaban sent me",
                "npc:Ha ha! Beer and a kebab",
                "player:I had something terribly important",
                "npc:Forget it!",
                "player:You know, I think",
                "mesbox:You have forgotten everything",
            })
            t.ticks(4)
            t.check("reward.cooking", t.skill.expect_gain("cooking", 5000, snap) == "ok", "literal 5000 Cooking XP (forget_shared.rs2:58, 50000 in tenths)")
            t.check("reward.farming", t.skill.expect_gain("farming", 5000, snap) == "ok", "literal 5000 Farming XP (forget_shared.rs2:59, 50000 in tenths)")
            local _, stout = t.inv.count("mature_dwarven_stout")
            t.check("reward.stout", stout == 2, "mature dwarven stout in the backpack: " .. tostring(stout) .. " want 2")
            t.quest.expect_complete()
            -- LEG 12 END
        end },
    },
}
