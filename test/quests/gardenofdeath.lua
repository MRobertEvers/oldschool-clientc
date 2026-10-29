-- The Garden of Death, driven the way RSProx index 4427 plays it ([1218..2558];
-- docs/rsprox-analysis/quests/garden_of_death.md). Written as a relay of legs
-- (docs/quest_authoring/relay.md).
--
-- The tablets are read on the stone plaque and put away with its own close
-- button (the server closes it, and the note pages follow on that tick); the
-- words are typed in the recorder's order, the misses too ("is", " water", a
-- second "water"); no carving has to be read for a word to be taken ("north"
-- [1618] with the lake map unread, "no" [2128] in the third ruin).
local function answer(t, word)
    -- Jagex's prompt is clientscript 110 (meslayer mode 9), answered with
    -- RESUME_P_STRINGDIALOG: scriptrun sends it, the client types it.
    local result = t.chat.resume_string(word)
    if result == "unsupported" then
        t.text(word)
        t.key("enter")
    end
    t.ticks(2)
end

local function translate_button(t, label)
    local _, button = t.ui.widget("tgod_translations:translate")
    local result = t.ui.invoke(button, 1)
    t.ticks(2)
    t.check(label, result == "ok" and t.chat.kind() == "none", "pressed " .. tostring(button) .. ": " .. tostring(result) .. "; the list is down and the prompt up (chat kind " .. tostring(t.chat.kind()) .. ")")
end

-- New translations, each answered Yes to "Attempt another translation?" but the
-- last, which is the stage and "You feel that you've translated enough words".
local function learn(t, prefix, words)
    for index, word in ipairs(words) do
        answer(t, word)
        t.exec(prefix .. "-" .. word, t.chat.play, {"mesbox:You've discovered a new translation:"})
        t.ticks(1)
        if index < #words then
            -- "Attempt another translation?" Yes., answered raw: the verb reads the
            -- options -> prompt reopen as stale; the next answer's box proves it
            t.chat.choose("Yes.")
            t.ticks(1)
        end
    end
    t.exec(prefix .. "-enough", t.chat.play, {"mesbox:You feel that you've translated enough words to read some of the"})
    t.ticks(2)
end

-- The plaque's rows against the recording's, as the client reads them (the
-- <col> tags are the wire audit's: ui.text answers the text drawn).
local function plaque(t, label, rows)
    for row, want in pairs(rows) do
        local _, got = t.ui.text("stoneplaque:ms" .. row)
        local plain = string.gsub(want, "<[^>]*>", "")
        -- a long tagged row comes back cut short, mid-tag ("...Home fire</"):
        -- the shown part must be the recorded row's start
        local shown = string.gsub(string.gsub(tostring(got), "<[^>]*>", ""), "<[^>]*$", "")
        local ok = got == plain or (#shown >= 40 and string.sub(plain, 1, #shown) == shown)
        t.check(label .. ".ms" .. row, ok, "ms" .. row .. " = " .. tostring(got) .. " (4427: " .. want .. ")")
    end
end

local function close_plaque(t)
    local _, button = t.ui.widget("stoneplaque:close")
    t.ui.invoke(button, 1)
    t.ticks(1)
end

local function read_tablet(t, label, tablet, rows)
    t.exec(label, t.player.inv_op, tablet, 1)
    t.exec(label .. "-open", t.ui.await_open, "stoneplaque", 6)
    plaque(t, label, rows)
    close_plaque(t)
end

local function open_list(t, label)
    t.exec(label, t.player.inv_op, "tgod_translations", 1)
    t.exec(label .. "-open", t.ui.await_open, "tgod_translations", 6)
end

return {
    id = "gardenofdeath",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_xmarksthespot", -- Veos sails to Great Kourend only once X Marks the Spot is done (xmarksthespot.rs2:27)
        "::setlevel farming 20", -- the guide's requirement: Farming 20 to start (gardenofdeath.rs2)
    },
    bind = {
        varp = "varb14609_tgod",
        constants = {
            not_started = 0, journal = 2, t1 = 4, enter1 = 6, take1 = 8, tablet1_read = 10,
            note_taken = 12, read1 = 14, t1_done = 16, enter2 = 20, vines = 22, vines_cut = 24,
            take2 = 26, read2 = 28, t2_done = 30, enter3 = 34, take3 = 36, read3 = 38,
            t3_done = 40, enter4 = 44, take4 = 46, read4 = 48, final = 50, final_read = 52,
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
                -- the wire audit against RSProx 4427 reads this (build/quest_gate/<run>/ticklog.tsv)
                t.ticklog.start()
                t.ticklog.mark("leg.dungeon1")
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

                -- Travel on foot and by ship (no ::gardenofdeath placement: it would put the player in Kourend and skip the ferry):
                -- Port Sarim -> Veos "Can you take me to Great Kourend?" -> Port Piscarilius dock
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

                -- [1359..1368] the tent: Start the quest? Yes.
                t.exec("getJournal", t.player.click_loc, "tgod_tent", 1)
                t.exec("getJournal-dialog", t.chat.play, {"choose:Yes.", "objbox:You search the tent and find a journal. You take it."})
                t.ticks(1)
                t.exec("getJournal-has", t.inv.expect_has, "tgod_journal", 1)
                t.expect("quest.stage.journal", t.quest.expect_stage("journal"))

                -- [1369..1377] the journal: two spreads, forward and back, then closed
                t.exec("readJournal", t.player.inv_op, "tgod_journal", 1)
                t.exec("readJournal-open", t.ui.await_open, "book", 6)
                local _, title = t.ui.text("book:title")
                t.check("readJournal-title", title == "Kasonde's Journal", "book title " .. tostring(title))
                local _, left1 = t.ui.text("book:page_left_text_1")
                t.check("readJournal-row1", left1 == "For many years now, much", "left row 1 " .. tostring(left1))
                -- [1371] the arrow is a pause button: the second spread
                t.exec("readJournal-turn", t.ui.press_pause, "book:page_right_button", -1, "book:page_right_button")
                t.ticks(1)
                local _, right15 = t.ui.text("book:page_right_text_15")
                t.check("readJournal-spread2", right15 == "time to uncover the truth.", "right row 15 " .. tostring(right15))
                t.ui.close_modal()
                t.ticks(1)
                t.expect("quest.stage.t1", t.quest.expect_stage("t1"))

                -- [1385], [1390] the camping equipment, twice
                t.exec("getSecateurs", t.player.click_loc, "tgod_camping_equipment", 1)
                t.exec("getSecateurs-box", t.chat.play, {"objbox:You search through the camping equipment and find"})
                t.exec("getSecateurs-has", t.inv.expect_has, "secateurs", 1)
                t.exec("getSecateurs-again", t.player.click_loc, "tgod_camping_equipment", 1)
                t.exec("getSecateurs-again-box", t.chat.play, {"mesbox:You search through the camping equipment, but find nothing of"})

                -- [1403..1407] the hole: stage 6 with the climb, the landing 1309,9869
                t.exec("enterHole", t.player.click_loc, "tgod_garden_1_entry", 1)
                t.ticks(4)
                t.expect("quest.stage.enter1", t.quest.expect_stage("enter1"))
                local _, hole_tile = t.world.tile()
                t.check("enterHole-landing", hole_tile ~= nil and hole_tile.x == 1309 and hole_tile.z == 9869, "landed at " .. tostring(hole_tile and (hole_tile.x .. "," .. hole_tile.z)) .. " (4427: 1309,9869)")

                -- [1428] tablet 1
                t.exec("searchForTablet", t.player.click_loc, "tgod_garden_1_tablet_table", 1)
                t.exec("searchForTablet-box", t.chat.play, {"objbox:You search the table and find a stone tablet. You take"})
                t.exec("searchForTablet-has", t.inv.expect_has, "tgod_tablet_1", 1)
                t.expect("quest.stage.take1", t.quest.expect_stage("take1"))

                -- [1464..1477] read: the Old Ones' text alone on ms6; the close button, the note on its back
                read_tablet(t, "readTablet1", "tgod_tablet_1", {
                    [5] = "",
                    [6] = "Amok miki. Tal toka kemo. Lini toka makt. Tal tlane osto ates ikam.",
                    [7] = "",
                })
                t.expect("quest.stage.tablet1_read", t.quest.expect_stage("tablet1_read"))
                t.exec("readTablet1-note", t.chat.play, {"objbox:As you finish reading, you spot a note stuck to the back", "choose:Yes.", "objbox:You take the note. Upon closer inspection"})
                t.exec("readTablet1-has", t.inv.expect_has, "tgod_translations", 1)
                t.expect("quest.stage.note_taken", t.quest.expect_stage("note_taken"))

                -- [1479..1518] the list, the button, "is" refused, then island and water
                open_list(t, "readTranslations")
                t.expect("quest.stage.read1", t.quest.expect_stage("read1"))
                translate_button(t, "attemptTranslation")
                answer(t, "is")
                t.exec("attempt-is", t.chat.play, {"mesbox:You can't think of any suitable translations for that word."})
                t.ticks(1)
                answer(t, "island")
                t.exec("attempt-island", t.chat.play, {"mesbox:You've discovered a new translation:"})
                t.chat.choose("Yes.")
                t.ticks(1)
                answer(t, "water")
                t.exec("attempt-water", t.chat.play, {"mesbox:You've discovered a new translation:", "choose:No."})

                -- [1544..1558] the plant diagram: the carving, read and put away with its close button
                t.exec("searchPlantTable", t.player.click_loc, "tgod_garden_1_plant_diagram_table", 1)
                t.exec("searchPlantTable-box", t.chat.play, {"objbox:You search the table and find a carving. You take it."})
                t.exec("inspectPlantDiagram", t.player.inv_op, "tgod_garden_1_plant_diagram", 1)
                t.exec("inspectPlantDiagram-open", t.ui.await_open, "tgod_garden_1_plant_diagram", 6)
                local _, diagram_close = t.ui.widget("tgod_garden_1_plant_diagram:close")
                t.ui.invoke(diagram_close, 1)
                t.ticks(1)
                t.check("inspectPlantDiagram-closed", t.ui.await_close("tgod_garden_1_plant_diagram", 4) == "ok", "the close button takes the carving down")

                -- [1558..1563] the hourglass chest's label
                t.exec("searchHourglassChest", t.player.click_loc, "tgod_garden_1_hourglass_chest", 1)
                t.exec("searchHourglassChest-box", t.chat.play, {"objbox:The chest contains some hourglasses."})

                -- [1576..1621] " water" refused, water known, then time vessel north: the stage on north
                open_list(t, "readTranslations-2")
                translate_button(t, "attemptTranslation-2")
                answer(t, " water")
                t.exec("attempt-space-water", t.chat.play, {"mesbox:You can't think of any suitable translations for that word."})
                t.ticks(1)
                answer(t, "water")
                t.exec("attempt-water-again", t.chat.play, {"mesbox:You've already discovered this translation:"})
                t.ticks(1)
                learn(t, "inputWords1", {"time", "vessel", "north"})
                t.expect("quest.stage.t1_done", t.quest.expect_stage("t1_done"))

                -- [1636..1638] the diagram table once the carving is carried
                t.exec("searchPlantTable-again", t.player.click_loc, "tgod_garden_1_plant_diagram_table", 1)
                t.exec("searchPlantTable-again-box", t.chat.play, {"mesbox:You search the table, but find nothing of interest."})
                t.check("leg.1.end", t.chat.kind() == "none", "quiet point in dungeon 1 beside the diagram table; stage t1_done (16); chat kind " .. tostring(t.chat.kind()))
                -- LEG 1 END
            end,
        },
        {
            name = "dungeon2",
            run = function(t)
                -- LEG 2 BEGIN: leaveHole
                t.ticklog.mark("leg.dungeon2")
                -- [1651..1654] the rope: face, reach, the landing 1309,3469
                t.exec("leaveHole", t.player.click_loc, "tgod_garden_1_exit", 1)
                t.ticks(4)
                local _, exit_tile = t.world.tile()
                t.check("leaveHole-landing", exit_tile ~= nil and exit_tile.x == 1309 and exit_tile.z == 3469, "on the surface at " .. tostring(exit_tile and (exit_tile.x .. "," .. exit_tile.z)) .. " (4427: 1309,3469)")

                -- the boat at the Molch dock ([2237]: Molch Island|Battlefront|Shayzien|Cancel)
                t.exec("goto-goToMolch", t.player.goto_tile, 1342, 3646, 0)
                t.exec("goToMolch", t.player.click_loc, "aerial_fishing_boat", 1)
                t.exec("goToMolch-menu", t.chat.play, {"choose:Molch Island"})
                t.ticks(6)
                local _, boat_tile = t.world.tile()
                t.check("goToMolch-landed", boat_tile ~= nil and boat_tile.x == 1369 and boat_tile.z == 3639, "landed on Molch Island at " .. tostring(boat_tile and (boat_tile.x .. "," .. boat_tile.z)) .. " (4427 [1811]: 1369,3639)")

                -- [1831..1834] the hole: 16 -> 18 -> 20, the landing 1375,10032
                t.exec("enterMolchHole", t.player.click_loc, "tgod_garden_2_entry_vis", 1)
                t.ticks(4)
                t.expect("quest.stage.enter2", t.quest.expect_stage("enter2"))
                local _, hole2 = t.world.tile()
                t.check("enterMolchHole-landing", hole2 ~= nil and hole2.x == 1375 and hole2.z == 10032, "landed at " .. tostring(hole2 and (hole2.x .. "," .. hole2.z)) .. " (4427: 1375,10032)")

                -- [1840..1862] the vines: inspect finds the note (22), read it, cut (24)
                t.exec("searchVines", t.player.click_loc, "tgod_vines_inspect", 1)
                t.exec("searchVines-box", t.chat.play, {"objbox:You spot a note on the floor near the vines. You take"})
                t.expect("quest.stage.vines", t.quest.expect_stage("vines"))
                t.exec("readVineNote", t.player.inv_op, "tgod_vine_note", 1)
                t.exec("readVineNote-open", t.ui.await_open, "note", 6)
                local _, line3 = t.ui.text("note:line3")
                t.check("readVineNote-line3", line3 == "Vines! Vines everywhere! I'm on the verge of greatness", "line3 " .. tostring(line3))
                local _, line10 = t.ui.text("note:line10")
                t.check("readVineNote-line10", line10 == "cautious.", "line10 " .. tostring(line10))
                local _, note_close = t.ui.widget("note:close")
                t.ui.invoke(note_close, 1)
                t.ticks(1)
                t.check("readVineNote-closed", t.ui.await_close("note", 4) == "ok", "the close button takes the note down")
                t.exec("cutVines", t.player.click_loc, "tgod_vines_cut", 1)
                t.exec("cutVines-box", t.chat.play, {"mesbox:You cut through the vines."})
                t.expect("quest.stage.vines_cut", t.quest.expect_stage("vines_cut"))

                -- [1867] the squeeze south
                t.exec("squeezeVines", t.player.click_loc, "tgod_vines_squeeze", 1)
                t.ticks(4)
                local _, squeeze_tile = t.world.tile()
                t.check("squeezeVines-south", squeeze_tile ~= nil and squeeze_tile.z == 10024, "squeezed through to " .. tostring(squeeze_tile and (squeeze_tile.x .. "," .. squeeze_tile.z)))

                -- [1884..1899] tablet 2: taken (26), read (28)
                t.exec("searchForTablet2", t.player.click_loc, "tgod_garden_2_tablet_table", 1)
                t.exec("searchForTablet2-box", t.chat.play, {"objbox:You search the table and find a stone tablet. You take"})
                t.expect("quest.stage.take2", t.quest.expect_stage("take2"))
                read_tablet(t, "readTablet2", "tgod_tablet_2", {
                    [3] = "",
                    [4] = "Siua olkat moki. Olkat moki tal. Kualt olkat tal toka. Moki yoka tlane lini",
                    [5] = "kamat. Silam osto achi ates xali.",
                    [6] = "",
                    [7] = "Siua olkat <col=33cc59>needed</col>. Olkat <col=33cc59>need</col> tal. Kualt olkat tal <col=33cc59>vessel</col>. <col=33cc59>Need</col> yoka",
                    [8] = "<col=33cc59>garden</col> lini <col=33cc59>method</col>. Silam <col=33cc59>below</col> achi <col=33cc59>water</col> xali.",
                    [9] = "",
                })
                t.expect("quest.stage.read2", t.quest.expect_stage("read2"))

                -- [1900..1950] west poison body food earth: the stage on earth
                open_list(t, "readTranslations2")
                translate_button(t, "attemptTranslation2")
                learn(t, "inputWords2", {"west", "poison", "body", "food", "earth"})
                t.expect("quest.stage.t2_done", t.quest.expect_stage("t2_done"))
                t.check("leg.2.end", t.chat.kind() == "none", "quiet point in dungeon 2 beside the tablet table; stage t2_done (30); chat kind " .. tostring(t.chat.kind()))
                -- LEG 2 END
            end,
        },
        {
            name = "dungeon3",
            run = function(t)
                -- LEG 3 BEGIN: squeezeVines2
                t.ticklog.mark("leg.dungeon3")
                -- [1960..1965] the squeeze north, [1967..1975] the rope (landing 1364,3636)
                t.exec("squeezeVines2", t.player.click_loc, "tgod_vines_squeeze", 1)
                t.ticks(4)
                local _, back_tile = t.world.tile()
                t.check("squeezeVines2-north", back_tile ~= nil and back_tile.z == 10025, "squeezed back north to " .. tostring(back_tile and (back_tile.x .. "," .. back_tile.z)))
                t.exec("leaveHole2", t.player.click_loc, "tgod_garden_2_exit", 1)
                t.ticks(4)
                local _, exit2_tile = t.world.tile()
                t.check("leaveHole2-landing", exit2_tile ~= nil and exit2_tile.x == 1364 and exit2_tile.z == 3636, "on Molch Island at " .. tostring(exit2_tile and (exit2_tile.x .. "," .. exit2_tile.z)) .. " (4427: 1364,3636)")

                -- [1978..1990] the boat to Molch (landing 1341,3645)
                t.exec("leaveMolchIsland", t.player.click_loc, "aerial_fishing_boat", 1)
                t.exec("leaveMolchIsland-menu", t.chat.play, {"choose:Molch"})
                t.ticks(6)
                local _, dock_tile = t.world.tile()
                t.check("leaveMolchIsland-landed", dock_tile ~= nil and dock_tile.x == 1341 and dock_tile.z == 3645, "landed at the Molch dock at " .. tostring(dock_tile and (dock_tile.x .. "," .. dock_tile.z)) .. " (4427 [1988]: 1341,3645)")

                -- [2043..2050] the hole by Xeric's shrine: 30 -> 32 -> 34, the landing 1296,10033
                t.exec("goto-enterXericShrineHole", t.player.goto_tile, 1315, 3617, 0)
                t.exec("enterXericShrineHole", t.player.click_loc, "tgod_garden_3_entry_vis", 1)
                t.ticks(4)
                t.expect("quest.stage.enter3", t.quest.expect_stage("enter3"))
                local _, hole3 = t.world.tile()
                t.check("enterXericShrineHole-landing", hole3 ~= nil and hole3.x == 1296 and hole3.z == 10033, "landed at " .. tostring(hole3 and (hole3.x .. "," .. hole3.z)) .. " (4427: 1296,10033)")

                -- [2096..2111] tablet 3 (36, read 38), then tablet 2 again with its new words
                t.exec("searchForTablet3", t.player.click_loc, "tgod_garden_3_tablet_table", 1)
                t.exec("searchForTablet3-box", t.chat.play, {"objbox:You search the table and find a stone tablet. You take"})
                t.expect("quest.stage.take3", t.quest.expect_stage("take3"))
                read_tablet(t, "readTablet3", "tgod_tablet_3", {
                    [2] = "",
                    [3] = "Lini kamat kemo. Olkat kemo. Tal toka kemo. Miki xita. Miki achi moki. Miki",
                    [4] = "chua tal. Miki tlane lini. Uitt takam osto tlane tasol.",
                    [5] = "",
                    [6] = "Lini <col=33cc59>method</col> kemo. <col=33cc59>Body</col> kemo. Tal <col=33cc59>vessel</col> kemo. <col=33cc59>Time</col> xita. <col=32b5ff>Time</col> <col=33cc59>poison</col>",
                    [7] = "<col=33cc59>needed</col>. <col=32b5ff>Time</col> chua tal. <col=32b5ff>Time</col> <col=33cc59>garden</col> lini. Uitt takam <col=33cc59>below</col> <col=32b5ff>garden</col>",
                    [8] = "<col=33cc59>building</col>.",
                    [9] = "",
                })
                t.expect("quest.stage.read3", t.quest.expect_stage("read3"))
                read_tablet(t, "rereadTablet2", "tgod_tablet_2", {
                    [7] = "Siua <col=33cc59>body</col> <col=33cc59>needed</col>. <col=33cc59>Body</col> <col=33cc59>need</col> tal. <col=32b5ff>Food</col> <col=33cc59>body</col> tal <col=33cc59>vessel</col>. <col=33cc59>Need</col> yoka",
                    [8] = "<col=33cc59>garden</col> lini <col=33cc59>method</col>. <col=33cc59>West</col> <col=33cc59>below</col> <col=33cc59>poison</col> <col=33cc59>water</col> <col=32b5ff>earth</col>.",
                })

                -- [2112..2149] make yes no move arrive east south ("no" with tablet 4 unread): the stage on south
                open_list(t, "readTranslations3")
                translate_button(t, "attemptTranslation3")
                learn(t, "inputWords3", {"make", "yes", "no", "move", "arrive", "east", "south"})
                t.expect("quest.stage.t3_done", t.quest.expect_stage("t3_done"))
                t.check("leg.3.end", t.chat.kind() == "none", "quiet point in dungeon 3 beside the tablet table; stage t3_done (40); chat kind " .. tostring(t.chat.kind()))
                -- LEG 3 END
            end,
        },
        {
            name = "dungeon4",
            run = function(t)
                -- LEG 4 BEGIN: leaveHole3
                t.ticklog.mark("leg.dungeon4")
                -- [2166..2170] the rope (landing 1315,3617)
                t.exec("leaveHole3", t.player.click_loc, "tgod_garden_3_exit", 1)
                t.ticks(4)
                local _, exit3_tile = t.world.tile()
                t.check("leaveHole3-landing", exit3_tile ~= nil and exit3_tile.x == 1315 and exit3_tile.z == 3617, "on the surface at " .. tostring(exit3_tile and (exit3_tile.x .. "," .. exit3_tile.z)) .. " (4427: 1315,3617)")

                -- [2326..2330] the hole in the Ruins of Morra: 40 -> 42 -> 44, the landing 1456,9824
                t.exec("goto-enterMorraHole", t.player.goto_tile, 1449, 3511, 0)
                t.exec("enterMorraHole", t.player.click_loc, "tgod_garden_4_entry_vis", 1)
                t.ticks(4)
                t.expect("quest.stage.enter4", t.quest.expect_stage("enter4"))
                local _, hole4 = t.world.tile()
                t.check("enterMorraHole-landing", hole4 ~= nil and hole4.x == 1456 and hole4.z == 9824, "landed at " .. tostring(hole4 and (hole4.x .. "," .. hole4.z)) .. " (4427: 1456,9824)")

                -- [2341..2353] tablet 4 (46, read 48)
                t.exec("searchForTablet4", t.player.click_loc, "tgod_garden_4_tablet_table", 1)
                t.exec("searchForTablet4-box", t.chat.play, {"objbox:You search the table and find a stone tablet. You take"})
                t.expect("quest.stage.take4", t.quest.expect_stage("take4"))
                read_tablet(t, "readTablet4", "tgod_tablet_4", {
                    [4] = "Miki achi ami. Tlane chua rani miki. Lini antil. Antil tepat. Ami tal. Tal toka",
                    [5] = "ami. Zema-Tal tepat.",
                    [7] = "<col=32b5ff>Time</col> <col=33cc59>poison</col> <col=32b5ff>no</col>. <col=32b5ff>Garden</col> <col=32b5ff>make</col> rani <col=32b5ff>time</col>. <col=32b5ff>Move</col> antil. Antil tepat. <col=33cc59>No</col> tal.",
                    [8] = "Tal <col=33cc59>vessel</col> <col=32b5ff>no</col>. Zema-Tal tepat.",
                })
                t.expect("quest.stage.read4", t.quest.expect_stage("read4"))

                -- [2356..2413] few big sun moon life death mind home air fire: the stage on fire
                open_list(t, "readTranslations4")
                translate_button(t, "attemptTranslation4")
                learn(t, "inputWords4", {"few", "big", "sun", "moon", "life", "death", "mind", "home", "air", "fire"})
                t.expect("quest.stage.final", t.quest.expect_stage("final"))
                -- [2434] the full list ("Miki - Time/Death", Ehke and Ralo appended): the wire audit's
                open_list(t, "readTranslationsFinal")
                t.ui.close_modal()
                t.ticks(1)

                -- [2416..2446] tablet 4 with every word (52); the close, the note on the floor, taken (54)
                read_tablet(t, "readTabletFinal", "tgod_tablet_4", {
                    [7] = "<col=33cc59>Death</col> <col=33cc59>poison</col> <col=32b5ff>no</col>. <col=32b5ff>Garden</col> <col=32b5ff>make</col> <col=32b5ff>moon</col> <col=33cc59>death</col>. <col=32b5ff>Move</col> <col=33cc59>home</col>. <col=33cc59>Home</col> <col=32b5ff>fire</col>. <col=33cc59>No</col>",
                    [8] = "<col=33cc59>life</col>. <col=32b5ff>Life</col> <col=33cc59>vessel</col> <col=32b5ff>no</col>. Zema-Tal <col=32b5ff>fire</col>.",
                })
                t.expect("quest.stage.final_read", t.quest.expect_stage("final_read"))
                t.exec("readTabletFinal-note", t.chat.play, {"objbox:As you finish reading, you spot a note on the floor.", "choose:Yes.", "objbox:You take the note."})
                t.exec("readTabletFinal-has", t.inv.expect_has, "tgod_final_note", 1)
                t.expect("quest.stage.warning", t.quest.expect_stage("warning"))

                -- [2449..2485] the warning note in the long scroll; its close button ends the quest,
                -- the scroll's close brings the last box
                local _, reward_before = t.skill.snapshot()
                t.exec("readWarningNote", t.player.inv_op, "tgod_final_note", 1)
                t.exec("readWarningNote-open", t.ui.await_open, "longscroll", 6)
                local _, scroll_close = t.ui.widget("longscroll:close")
                t.ui.invoke(scroll_close, 1)
                t.ticks(2)
                t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
                t.exec("questScroll-open", t.ui.await_open, "questscroll", 6)
                local _, quest_close = t.ui.widget("questscroll:close_button")
                t.ui.invoke(quest_close, 1)
                t.ticks(1)
                -- [2485] the box on the scroll's close; the level-up ([2497]) comes on its continue
                t.exec("completeMessage", t.chat.play, {"mesbox:Although you've now discovered the fate of the Old Ones"})
                t.ticks(2)
                t.check("reward.farming", t.skill.expect_gain("farming", 10000, reward_before), "10,000 Farming XP (4427 [2481])")
                t.quest.expect_complete()
                -- LEG 4 END
                t.finish(0)
            end,
        },
    },
}
