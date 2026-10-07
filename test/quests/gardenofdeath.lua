-- The Garden of Death. Written as a relay of legs (docs/quest_authoring/relay.md).
-- Guide: Quest Helper thegardenofdeath; notes: docs/quests/ladders/gardenofdeath.notes.md.
return {
    id = "gardenofdeath",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_xmarksthespot", -- Veos sails to Great Kourend only once X Marks the Spot is done (xmarksthespot.rs2:27)
        "::setlevel farming 20", -- the guide's requirement: Farming 20 to start (gardenofdeath.rs2:37)
    },
    bind = {
        varp = "varb14609_tgod",
        constants = {
            not_started = 0, journal = 2, t1 = 4, enter1 = 6, take1 = 13, read1 = 14,
            t1_done = 16, enter2 = 18, vines = 22, vines_cut = 24, take2 = 27, read2 = 28,
            t2_done = 30, enter3 = 32, take3 = 37, read3 = 38, t3_done = 40,
            enter4 = 43, take4 = 47, read4 = 48, final = 50, final_read = 52,
            warning = 54, complete = 56,
        },
        row = "quest_gardenofdeath",
        display = "The Garden of Death",
        points = 1,
    },

    legs = {
        {
            name = "dungeon1",
            run = function(t)
                -- LEG 1 BEGIN: getJournal
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

                -- Travel on foot and by ship (no ::gardenofdeath placement: it would put the player in Kourend and skip the ferry):
                -- Port Sarim -> Veos "Can you take me to Great Kourend?" (clientofkourend.rs2:30) -> Port Piscarilius dock
                -- 1824,3690 -> overland to the Mount Quidamortem camp.
                t.exec("goto-veosSarim", t.player.goto_tile, 3054, 3246, 0)
                t.exec("talkToVeos", t.player.talk_to, "veos_sarim", 1)
                t.exec("talkToVeos-menu", t.chat.drain, { stop_at = "options" })
                t.exec("talkToVeos-sail", t.chat.choose, "Can you take me to Great Kourend?")
                t.exec("talkToVeos-done", t.chat.drain, {})
                t.ticks(4)
                local _, dock_arrival = t.world.tile()
                t.check("talkToVeos-landed", dock_arrival ~= nil and dock_arrival.x >= 1800 and dock_arrival.x < 1850, "landed at the Piscarilius dock at " .. tostring(dock_arrival and (dock_arrival.x .. "," .. dock_arrival.z)))
                t.exec("goto-tent", t.player.goto_tile, 1313, 3472, 0)

                -- Search the tent (gardenofdeath.rs2:341): Start the quest? Yes.
                t.exec("getJournal", t.player.click_loc, "tgod_tent", 1)
                t.exec("getJournal-dialog-1", t.chat.play, {"choose:Yes."})
                t.ticks(2)
                t.exec("getJournal-has", t.inv.expect_has, "tgod_journal", 1)
                t.expect("quest.stage.journal", t.quest.expect_stage("journal"))

                -- readJournal: op1 on the journal opens the book (gardenofdeath.rs2:379)
                t.exec("readJournal", t.player.inv_op, "tgod_journal", 1)
                t.ticks(3)
                t.key("escape")
                t.ticks(2)
                t.expect("quest.stage.t1", t.quest.expect_stage("t1"))

                -- Search the camping equipment (gardenofdeath.rs2:422)
                t.exec("goto-getSecateurs", t.player.goto_tile, 1312, 3472, 0)
                t.exec("getSecateurs", t.player.click_loc, "tgod_camping_equipment", 1)
                t.exec("getSecateurs-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.exec("getSecateurs-has", t.inv.expect_has, "secateurs", 1)

                -- Enter the hole (gardenofdeath.rs2:438): needs the journal read; sets stage 6
                t.exec("goto-enterHole", t.player.goto_tile, 1308, 3469, 0)
                t.exec("enterHole", t.player.click_loc, "tgod_garden_1_entry", 1)
                t.ticks(3)
                t.expect("quest.stage.enter1", t.quest.expect_stage("enter1"))
                local _, hole_tile = t.world.tile()
                t.check("enterHole-below", hole_tile ~= nil and hole_tile.z > 9000, "in the dungeon at " .. tostring(hole_tile and (hole_tile.x .. "," .. hole_tile.z)))

                -- Search the north-west table (gardenofdeath.rs2:484): tablet 1
                t.exec("searchForTablet", t.player.click_loc, "tgod_garden_1_tablet_table", 1)
                t.exec("searchForTablet-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.exec("searchForTablet-has", t.inv.expect_has, "tgod_tablet_1", 1)
                t.expect("quest.stage.take1", t.quest.expect_stage(13))

                -- readTablet1: op1 on the tablet (gardenofdeath.rs2:617); the note on its back follows
                t.exec("readTablet1", t.player.inv_op, "tgod_tablet_1", 1)
                t.ticks(3)
                t.key("escape")
                t.ticks(3)
                t.expect("quest.stage.read1", t.quest.expect_stage("read1"))
                t.exec("readTablet1-note-seen", t.chat.continue_, true)
                t.exec("readTablet1-note", t.chat.play, {"choose:Yes."})
                t.ticks(2)
                t.exec("readTablet1-note-taken", t.chat.continue_, true)
                t.ticks(2)
                t.exec("readTranslations-has", t.inv.expect_has, "tgod_translations", 1)

                -- readTranslations: op1 on the Word translations opens the list (gardenofdeath.rs2:309)
                t.exec("readTranslations", t.player.inv_op, "tgod_translations", 1)
                t.exec("readTranslations-open", t.ui.await_open, "tgod_translations", 10)
                local _, list_text = t.ui.text("tgod_translations:scroll_title")
                t.check("readTranslations-list", list_text ~= nil, "word list title: " .. tostring(list_text))

                -- attemptTranslation: the list's Attempt Translation resume button
                local _, translate_button = t.ui.widget("tgod_translations:translate")
                local press_result = t.ui.invoke(translate_button, 1)
                t.ticks(3)
                t.check("attemptTranslation", press_result == "ok" and t.chat.kind() == "name", "pressed component " .. tostring(translate_button) .. ": " .. tostring(press_result) .. "; chat kind " .. tostring(t.chat.kind()))

                -- inputWords1: island water time vessel north (Quest Helper set 1); each answer
                -- is a mesbox, then "Attempt another translation?" (gardenofdeath.rs2:274)
                local words = {"island", "water", "time", "vessel", "north"}
                for index, word in ipairs(words) do
                    t.exec("inputWords1-" .. word, t.chat.name_entry, word)
                    t.ticks(2)
                    t.exec("inputWords1-" .. word .. "-result", t.chat.play, {"mesbox:You've discovered a new translation"})
                    t.ticks(2)
                    if index < #words then
                        -- "Attempt another translation?" Yes. (answered raw: the verb reads the options->name-prompt reopen as stale;
                        -- the next inputWords1 name_entry row proves the prompt is back up)
                        t.chat.choose("Yes.")
                        t.ticks(3)
                    end
                end

                -- the fifth word completes tablet 1: the "translated enough" mesbox, then the loop returns
                t.exec("inputWords1-enough", t.chat.play, {"mesbox:You feel that you've translated enough words to read some of the text"})
                t.ticks(3)
                t.expect("quest.stage.t1_done", t.quest.expect_stage("t1_done"))
                t.check("leg.1.end", t.chat.kind() == "none", "quiet point in dungeon 1 beside the tablet table (about 1307,9884 level 0); stage t1_done (16) read from the server; chat kind " .. tostring(t.chat.kind()) .. "; backpack: tgod_journal, secateurs, tgod_tablet_1, tgod_translations")

                -- LEG 1 END
            end,
        },
        {
            name = "dungeon2",
            run = function(t)
                -- LEG 2 BEGIN: leaveHole
                t.exec("leaveHole", t.player.click_loc, "tgod_garden_1_exit", 1)
                t.ticks(3)
                local _, exit_tile = t.world.tile()
                t.check("leaveHole-surface", exit_tile ~= nil and exit_tile.z < 5000, "back on the surface at " .. tostring(exit_tile and (exit_tile.x .. "," .. exit_tile.z)))

                -- goToMolch: Boaty (gardenofdeath.rs2:960); the surface exit lands at 1308,3469, a 281-tile walk south of the Molch dock
                t.exec("goto-goToMolch", t.player.goto_tile, 1342, 3646, 0)
                t.exec("goToMolch", t.player.click_loc, "aerial_fishing_boat", 1)
                t.exec("goToMolch-dialog", t.chat.play, {"choose:Molch Island"})
                t.ticks(4)
                local _, boat_tile = t.world.tile()
                t.check("goToMolch-landed", boat_tile ~= nil and boat_tile.x >= 1360 and boat_tile.x < 1380, "landed on Molch Island at " .. tostring(boat_tile and (boat_tile.x .. "," .. boat_tile.z)))

                -- enterMolchHole (gardenofdeath.rs2:451)
                t.exec("enterMolchHole", t.player.click_loc, "tgod_garden_2_entry_vis", 1)
                t.ticks(3)
                t.expect("quest.stage.enter2", t.quest.expect_stage("enter2"))

                -- searchVines / cutVines (gardenofdeath.rs2:883, 889)
                t.exec("goto-searchVines", t.player.goto_tile, 1375, 10026, 0)
                t.exec("searchVines", t.player.click_loc, "tgod_vines_inspect", 1)
                t.exec("searchVines-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.expect("quest.stage.vines", t.quest.expect_stage("vines"))
                t.exec("cutVines", t.player.click_loc, "tgod_vines_cut", 1)
                t.exec("cutVines-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.expect("quest.stage.vines_cut", t.quest.expect_stage("vines_cut"))

                -- the cut vines become a squeeze-through (gardenofdeath.rs2:910); the table is on the south side
                t.exec("squeezeVines", t.player.click_loc, "tgod_vines_squeeze", 1)
                t.ticks(3)
                local _, squeeze_tile = t.world.tile()
                t.check("squeezeVines-south", squeeze_tile ~= nil and squeeze_tile.z <= 10024, "squeezed through to " .. tostring(squeeze_tile and (squeeze_tile.x .. "," .. squeeze_tile.z)))

                -- searchForTablet2 (gardenofdeath.rs2:539): the south-west table is past the vines
                t.exec("searchForTablet2", t.player.click_loc, "tgod_garden_2_tablet_table", 1)
                t.exec("searchForTablet2-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.exec("searchForTablet2-has", t.inv.expect_has, "tgod_tablet_2", 1)
                t.expect("quest.stage.take2", t.quest.expect_stage("take2"))

                -- readTablet2 (gardenofdeath.rs2:617)
                t.exec("readTablet2", t.player.inv_op, "tgod_tablet_2", 1)
                t.ticks(3)
                t.key("escape")
                t.ticks(3)
                t.expect("quest.stage.read2", t.quest.expect_stage("read2"))

                -- readTranslations2
                t.exec("readTranslations2", t.player.inv_op, "tgod_translations", 1)
                t.exec("readTranslations2-open", t.ui.await_open, "tgod_translations", 10)
                local _, list_text = t.ui.text("tgod_translations:scroll_title")
                t.check("readTranslations2-list", list_text ~= nil, "word list title: " .. tostring(list_text))

                -- attemptTranslation2
                local _, translate_button = t.ui.widget("tgod_translations:translate")
                local press_result = t.ui.invoke(translate_button, 1)
                t.ticks(3)
                t.check("attemptTranslation2", press_result == "ok" and t.chat.kind() == "name", "pressed component " .. tostring(translate_button) .. ": " .. tostring(press_result) .. "; chat kind " .. tostring(t.chat.kind()))

                -- inputWords2: west poison body food earth (Quest Helper set 2)
                local words = {"west", "poison", "body", "food", "earth"}
                for index, word in ipairs(words) do
                    t.exec("inputWords2-" .. word, t.chat.name_entry, word)
                    t.ticks(2)
                    t.exec("inputWords2-" .. word .. "-result", t.chat.play, {"mesbox:You've discovered a new translation"})
                    t.ticks(2)
                    if index < #words then
                        t.chat.choose("Yes.")
                        t.ticks(3)
                    end
                end
                t.exec("inputWords2-enough", t.chat.play, {"mesbox:You feel that you've translated enough words to read some of the text"})
                t.ticks(3)
                t.expect("quest.stage.t2_done", t.quest.expect_stage("t2_done"))
                t.check("leg.2.state", t.chat.kind() == "none", "quiet point in dungeon 2 beside the south-west tablet table; stage t2_done (30) read from the server; chat kind " .. tostring(t.chat.kind()))
                -- LEG 2 END
            end,
        },
        {
            name = "dungeon3",
            run = function(t)
                -- LEG 3 BEGIN: leaveHole2
                -- "air" (word 29, typed in set 4) is seen only on dungeon 2's rune carving (gardenofdeath.rs2:828, spot 7 at 1375,10012): still south of the vines, search and inspect it now
                t.exec("search-tgod_garden_2_rune_diagram", t.player.click_loc, "tgod_garden_2_rune_diagram_rubble", 1)
                t.exec("search-tgod_garden_2_rune_diagram-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.exec("search-tgod_garden_2_rune_diagram-has", t.inv.expect_has, "tgod_garden_2_rune_diagram", 1)
                t.exec("inspect-tgod_garden_2_rune_diagram", t.player.inv_op, "tgod_garden_2_rune_diagram", 1)
                t.ticks(3)
                t.chat.continue_(true)
                t.key("escape")
                t.ticks(2)
                -- the vines split dungeon 2: squeeze back north to the rope (gardenofdeath.rs2:910)
                t.exec("squeezeVines2", t.player.click_loc, "tgod_vines_squeeze", 1)
                t.ticks(3)
                local _, back_tile = t.world.tile()
                t.check("squeezeVines2-north", back_tile ~= nil and back_tile.z > 10024, "squeezed back north to " .. tostring(back_tile and (back_tile.x .. "," .. back_tile.z)))
                t.exec("leaveHole2", t.player.click_loc, "tgod_garden_2_exit", 1)
                t.ticks(3)
                local _, exit2_tile = t.world.tile()
                t.check("leaveHole2-surface", exit2_tile ~= nil and exit2_tile.z < 5000, "back on Molch Island's surface at " .. tostring(exit2_tile and (exit2_tile.x .. "," .. exit2_tile.z)))

                -- leaveMolchIsland: Boaty (gardenofdeath.rs2:960), dock choice Molch
                t.exec("goto-leaveMolchIsland", t.player.goto_tile, 1367, 3640, 0)
                t.exec("leaveMolchIsland", t.player.click_loc, "aerial_fishing_boat", 1)
                t.exec("leaveMolchIsland-dialog", t.chat.play, {"choose:Molch"})
                t.ticks(4)
                local _, dock_tile = t.world.tile()
                t.check("leaveMolchIsland-landed", dock_tile ~= nil and dock_tile.x >= 1330 and dock_tile.x < 1355 and dock_tile.z < 5000, "landed at the Molch dock at " .. tostring(dock_tile and (dock_tile.x .. "," .. dock_tile.z)))

                -- enterXericShrineHole (gardenofdeath.rs2:460)
                t.exec("goto-enterXericShrineHole", t.player.goto_tile, 1314, 3615, 0)
                t.exec("enterXericShrineHole", t.player.click_loc, "tgod_garden_3_entry_vis", 1)
                t.ticks(3)
                t.expect("quest.stage.enter3", t.quest.expect_stage("enter3"))

                -- searchForTablet3 (gardenofdeath.rs2:546)
                t.exec("searchForTablet3", t.player.click_loc, "tgod_garden_3_tablet_table", 1)
                t.exec("searchForTablet3-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.exec("searchForTablet3-has", t.inv.expect_has, "tgod_tablet_3", 1)
                t.expect("quest.stage.take3", t.quest.expect_stage("take3"))

                -- readTablet3
                t.exec("readTablet3", t.player.inv_op, "tgod_tablet_3", 1)
                t.ticks(3)
                t.key("escape")
                t.ticks(3)
                t.expect("quest.stage.read3", t.quest.expect_stage("read3"))

                -- a word can be typed only after it was seen (gardenofdeath.rs2:795): search the five spots, inspect each find
                local finds3 = {
                    {"tgod_garden_3_bucket_diagram_table", "tgod_garden_3_bucket_diagram"},
                    {"tgod_garden_3_carving_diagram_table", "tgod_garden_3_carving_diagram"},
                    {"tgod_garden_3_package_diagram_vase", "tgod_garden_3_package_diagram"},
                    {"tgod_garden_3_transfer_diagram_mushroom", "tgod_garden_3_transfer_diagram"},
                    {"tgod_garden_3_compass_chest", "tgod_garden_3_compass"},
                }
                for _, find in ipairs(finds3) do
                    t.exec("search-" .. find[2], t.player.click_loc, find[1], 1)
                    t.exec("search-" .. find[2] .. "-dismiss", t.chat.continue_, true)
                    t.ticks(2)
                    t.exec("search-" .. find[2] .. "-has", t.inv.expect_has, find[2], 1)
                    t.exec("inspect-" .. find[2], t.player.inv_op, find[2], 1)
                    t.ticks(3)
                    t.chat.continue_(true)
                    t.key("escape")
                    t.ticks(2)
                end

                -- readTranslations3
                t.exec("readTranslations3", t.player.inv_op, "tgod_translations", 1)
                t.exec("readTranslations3-open", t.ui.await_open, "tgod_translations", 10)
                local _, list3_text = t.ui.text("tgod_translations:scroll_title")
                t.expect("readTranslations3-list", list3_text ~= nil and "ok" or "refused", "word list title: " .. tostring(list3_text))

                -- attemptTranslation3
                local _, translate3_button = t.ui.widget("tgod_translations:translate")
                local press3_result = t.ui.invoke(translate3_button, 1)
                t.ticks(3)
                t.check("attemptTranslation3", press3_result == "ok" and t.chat.kind() == "name", "pressed component " .. tostring(translate3_button) .. ": " .. tostring(press3_result) .. "; chat kind " .. tostring(t.chat.kind()))

                -- inputWords3: make yes no move arrive east south (Quest Helper set 3)
                local words3 = {"make", "yes", "no", "move", "arrive", "east", "south"}
                for index, word in ipairs(words3) do
                    t.exec("inputWords3-" .. word, t.chat.name_entry, word)
                    t.ticks(2)
                    t.exec("inputWords3-" .. word .. "-result", t.chat.play, {"mesbox:You've discovered a new translation"})
                    t.ticks(2)
                    if index < #words3 then
                        t.chat.choose("Yes.")
                        t.ticks(3)
                    end
                end
                t.exec("inputWords3-enough", t.chat.play, {"mesbox:You feel that you've translated enough words to read some of the text"})
                t.ticks(3)
                t.expect("quest.stage.t3_done", t.quest.expect_stage("t3_done"))
                t.check("leg.3.end", t.chat.kind() == "none", "quiet point in dungeon 3 beside the tablet table; stage t3_done (40) read from the server; backpack tgod_journal, secateurs, tablets 1-3, tgod_translations; chat kind " .. tostring(t.chat.kind()))
                -- LEG 3 END
            end,
        },
        {
            name = "dungeon4",
            run = function(t)
                -- LEG 4 BEGIN: leaveHole3
                t.exec("leaveHole3", t.player.click_loc, "tgod_garden_3_exit", 1)
                t.ticks(3)
                local _, exit3_tile = t.world.tile()
                t.check("leaveHole3-surface", exit3_tile ~= nil and exit3_tile.z < 5000, "back on the surface at " .. tostring(exit3_tile and (exit3_tile.x .. "," .. exit3_tile.z)))

                -- enterMorraHole (gardenofdeath.rs2:469): plain travel to the Ruins of Morra, then the hole
                t.exec("goto-enterMorraHole", t.player.goto_tile, 1450, 3510, 0)
                t.exec("enterMorraHole", t.player.click_loc, "tgod_garden_4_entry_vis", 1)
                t.ticks(3)
                t.expect("quest.stage.enter4", t.quest.expect_stage("enter4"))

                -- searchForTablet4 (gardenofdeath.rs2:553)
                t.exec("searchForTablet4", t.player.click_loc, "tgod_garden_4_tablet_table", 1)
                t.exec("searchForTablet4-dismiss", t.chat.continue_, true)
                t.ticks(2)
                t.exec("searchForTablet4-has", t.inv.expect_has, "tgod_tablet_4", 1)
                t.expect("quest.stage.take4", t.quest.expect_stage("take4"))

                -- readTablet4
                t.exec("readTablet4", t.player.inv_op, "tgod_tablet_4", 1)
                t.ticks(3)
                t.key("escape")
                t.ticks(3)
                t.expect("quest.stage.read4", t.quest.expect_stage("read4"))

                -- a word can be typed only after it was seen (gardenofdeath.rs2:795): search the spots, inspect each find
                local finds4 = {
                    {"tgod_garden_4_creature_diagram_table", "tgod_garden_4_creature_diagram"},
                    {"tgod_garden_4_recycling_diagram_table", "tgod_garden_4_recycling_diagram"},
                    {"tgod_garden_4_light_diagram_chest", "tgod_garden_4_light_diagram"},
                    {"tgod_garden_4_rune_diagram_vase", "tgod_garden_4_rune_diagram"},
                    {"tgod_garden_4_delivery_diagram_rubble", "tgod_garden_4_delivery_diagram"},
                }
                for _, find in ipairs(finds4) do
                    t.exec("search-" .. find[2], t.player.click_loc, find[1], 1)
                    t.exec("search-" .. find[2] .. "-dismiss", t.chat.continue_, true)
                    t.ticks(2)
                    t.exec("search-" .. find[2] .. "-has", t.inv.expect_has, find[2], 1)
                    t.exec("inspect-" .. find[2], t.player.inv_op, find[2], 1)
                    t.ticks(3)
                    t.chat.continue_(true)
                    t.key("escape")
                    t.ticks(2)
                end
                -- the labelled chest marks "home" seen (gardenofdeath.rs2:778)
                t.exec("search-teleport-chest", t.player.click_loc, "tgod_garden_4_teleport_chest", 1)
                t.exec("search-teleport-chest-dismiss", t.chat.continue_, true)
                t.ticks(2)

                -- readTranslations4
                t.exec("readTranslations4", t.player.inv_op, "tgod_translations", 1)
                t.exec("readTranslations4-open", t.ui.await_open, "tgod_translations", 10)
                local _, list4_text = t.ui.text("tgod_translations:scroll_title")
                t.check("readTranslations4-list", list4_text ~= nil, "word list title: " .. tostring(list4_text))

                -- attemptTranslation4
                local _, translate4_button = t.ui.widget("tgod_translations:translate")
                local press4_result = t.ui.invoke(translate4_button, 1)
                t.ticks(3)
                t.check("attemptTranslation4", press4_result == "ok" and t.chat.kind() == "name", "pressed component " .. tostring(translate4_button) .. ": " .. tostring(press4_result) .. "; chat kind " .. tostring(t.chat.kind()))

                -- inputWords4: few big sun moon life death mind home air fire (Quest Helper set 4)
                local words4 = {"few", "big", "sun", "moon", "life", "death", "mind", "home", "air", "fire"}
                for index, word in ipairs(words4) do
                    t.exec("inputWords4-" .. word, t.chat.name_entry, word)
                    t.ticks(2)
                    t.exec("inputWords4-" .. word .. "-result", t.chat.play, {"mesbox:You've discovered a new translation"})
                    t.ticks(2)
                    if index < #words4 then
                        t.chat.choose("Yes.")
                        t.ticks(3)
                    end
                end
                t.exec("inputWords4-enough", t.chat.play, {"mesbox:You feel that you've translated enough words to read some of the text"})
                t.ticks(3)
                t.expect("quest.stage.final", t.quest.expect_stage("final"))

                -- readTabletFinal (gardenofdeath.rs2:597, queued on closing the tablet): the note on its back
                t.exec("readTabletFinal", t.player.inv_op, "tgod_tablet_4", 1)
                t.ticks(3)
                t.key("escape")
                t.ticks(3)
                t.exec("readTabletFinal-dialog", t.chat.play, {"*", "choose:Yes."})
                t.exec("readTabletFinal-take", t.chat.continue_, true)
                t.ticks(2)
                t.exec("readTabletFinal-has", t.inv.expect_has, "tgod_final_note", 1)
                t.expect("quest.stage.warning", t.quest.expect_stage("warning"))

                -- readWarningNote (gardenofdeath.rs2:406): closing the book ends the quest
                local _, reward_before = t.skill.snapshot()
                t.exec("readWarningNote", t.player.inv_op, "tgod_final_note", 1)
                t.ticks(3)
                t.key("escape")
                t.ticks(4)
                t.chat.continue_(true)
                t.ticks(2)
                t.quest.expect_complete()
                t.check("reward.farming", t.skill.expect_gain("farming", 10000, reward_before), "10000 Farming XP (Quest Helper reward)")
                -- LEG 4 END
                t.finish(0)
            end,
        },
    },
}
