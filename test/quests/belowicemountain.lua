-- Below Ice Mountain -- driven step by step against OSRS-Content quests/quest_belowicemountain,
-- rebuilt from RSProx index 4374 (the whole quest recorded 2026-10-25;
-- docs/rsprox-analysis/quests/below_ice_mountain.md). The route is the recording's:
--
-- Willow on the path (3003,3435) -> Checkal in Barbarian Village (3087,3415) -> Atlas in the
-- long hall: the workout (a copy of Varrock and the village) -> Checkal: the flex -> Marley in
-- Edgeville -> the Blue Moon's cook -> knife on bread -> Marley: the sandwich -> Burntof in the
-- Rising Sun -> a Mind Bomb from the bar -> the drink used on Burntof: rock paper scissors upstairs (a
-- copy) -> Willow at the entrance: the excavation, the tripwire and the ruins (three copies) ->
-- the four pillars brought down on the Ancient Guardian in that copy -> the bag, the doors,
-- Ramarno -> the scroll -> Ramarno at the doors.
--
-- Scenes play in private copies, so their cameras are checked by op and height, not by tile.
-- Pages that go up under a scene have nothing to click (as recorded), so a scene is waited
-- out, not clicked through.
--
-- Setup: 16 Quest Points; a bronze pickaxe and Mining 10 for the pillars (the recording's way),
-- lobsters for the guardian's swings; bread, cooked meat and a knife for the sandwich; coins
-- for the ale.
return {
    id = "belowicemountain",
    fixture = "fresh_lumbridge.ini",
    max_frames = 220000,
    setup = {
        "::clearinv",
        "::belowicemountain",
        "::setvar varp101_qp 16",
        "::setlevel hitpoints 40",
        "::setlevel defence 30",
        "::setlevel mining 10",
        "::give bronze_pickaxe 1",
        "::give lobster 10",
        "::give bread 1",
        "::give cooked_meat 1",
        "::give knife 1",
        "::give coins 20",
    },

    run = function(t)
        -- the wire audit against RSProx 4374 reads this (build/quest_gate/<run>/ticklog.tsv)
        t.ticklog.start()
        t.quest.bind({
            varp = "varb12063_bim",
            constants = { not_started = 0, willow = 5, pitched = 7, started = 10, crew = 15, dungeon = 20,
                gathered = 25, reenter = 30, guardian = 35, cutscene = 40, complete = 120 },
            display = "Below Ice Mountain",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.var.expect("varb12063_bim", 0))

        local function varbit(name)
            local _, v = t.var.varbit(name)
            return v
        end
        -- A scene's pages: the ones with a continue are clicked; the ones it puts up under a
        -- moving cast have nothing to click (as recorded) and are waited out. Done when the
        -- chatbox has been empty for six ticks after the last click.
        local function play_scene(name, max_ticks, track)
            local clicks, idle, waited = 0, 0, 0
            local path, last = {}, nil
            while waited < max_ticks do
                if track then
                    local _, tile = t.world.tile()
                    if tile ~= nil then
                        local here = tile.x .. "," .. tile.z
                        if here ~= last then
                            path[#path + 1] = waited .. ":" .. here
                            last = here
                        end
                    end
                end
                if t.chat.kind() ~= "none" then
                    idle = 0
                    if t.chat.kind() == "options" then
                        break
                    end
                    if t.chat.continue_() == "ok" then
                        clicks = clicks + 1
                    end
                else
                    idle = idle + 1
                    if clicks > 0 and idle >= 6 then
                        break
                    end
                end
                t.ticks(1)
                waited = waited + 1
            end
            t.step(name, clicks > 0 and "PASS" or "FAIL", clicks .. " page(s) clicked over " .. waited .. " tick(s)")
            if track then
                t.step(name .. ".path", "PASS", table.concat(path, " "))
            end
        end
        -- A multinpc is drawn in its SHELL's stance (deob Statics.method4320); these shells
        -- state human_ready over rungs that state none (multinpc_stance_audit.py on 4374).
        local function stance(name, sym)
            local r, d, pose = t.npc.pose(sym)
            t.check(name, r == "ok" and pose ~= nil and pose.readyanim == 808, tostring(d))
        end
        -- Every page's echo (and every mesbox and game line) since the last dump, for the
        -- page-by-page diff against RSProx 4374 (ledger.tsv, rows "pages.<leg>").
        local page_serial = t.player._message_serial() or 0
        local function dump_pages(leg)
            local lr, lines = t.chat.lines(page_serial)
            local out = {}
            if lr == "ok" then
                for i = 1, #lines do
                    out[#out + 1] = tostring(lines[i].type) .. ":" .. tostring(lines[i].text)
                    page_serial = math.max(page_serial, lines[i].serial)
                end
            end
            t.step("pages." .. leg, "PASS", table.concat(out, " || "))
        end
        local function at_tile(name, x, z, level, slack)
            local _, tile = t.world.tile()
            slack = slack or 0
            t.check(name, tile ~= nil and math.abs(tile.x - x) <= slack and math.abs(tile.z - z) <= slack
                and tile.level == level,
                "at " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z) .. "," .. tostring(tile and tile.level)
                    .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- ---- Willow on the path ([385]..[416]) ----
        t.ticklog.mark("leg.willow")
        t.exec("goto-willow", t.player.goto_tile, 3004, 3435, 0)
        stance("willow.stance", "bim_willow_outside")
        t.exec("talkToWillow", t.player.talk_to, "bim_willow_outside")
        t.exec("talkToWillow-pitch", t.chat.drain, { stop_at = "options" })
        t.exec("talkToWillow-yes", t.chat.choose, "Yes.")
        t.check("quest.stage.willow", varbit("varb12063_bim") == 5, "bim=" .. tostring(varbit("varb12063_bim")))
        t.exec("talkToWillow-tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.started", t.var.expect("varb12063_bim", 10))

        -- ---- Checkal; Atlas and the workout ([458]..[775]) ----
        dump_pages("before-checkal")
        t.ticklog.mark("leg.checkal")
        t.exec("goto-checkal", t.player.goto_tile, 3086, 3415, 0)
        stance("checkal.stance", "bim_checkal_barb")
        t.exec("talkToCheckal", t.player.talk_to, "bim_checkal_barb")
        t.exec("talkToCheckal-chat", t.chat.drain, {})
        t.check("talkToCheckal-sent", varbit("varb12065_bim_checkal") == 5, "checkal=" .. tostring(varbit("varb12065_bim_checkal")))

        dump_pages("before-atlas")
        t.ticklog.mark("leg.atlas")
        t.exec("goto-atlas", t.player.goto_tile, 3078, 3433, 0)
        t.exec("atlas.doorIn", t.player.pass_door, { closed = "fai_barbarian_large_doubledoor_l", open = "fai_barbarian_open_large_doubledoor_l",
            at = { 3078, 3435, 0 }, near = { 3078, 3434 }, far = { 3078, 3436 } })
        t.exec("talkToAtlas", t.player.talk_to, "bim_atlas")
        t.exec("talkToAtlas-menu", t.chat.drain, { stop_at = "options" })
        local gym_mark = t.cutscene.mark()
        t.exec("talkToAtlas-yes", t.chat.choose, "Yes.")
        t.ticks(8)
        play_scene("atlas-workout", 320, true)
        t.exec("atlas-workout.cutscene", t.cutscene.await, "atlas-workout", { since = gym_mark, quiet = 300, expect = {
            { op = "moveto", height = 500 },
            { op = "lookat", height = 350 },
            { op = "reset" },
        } })
        t.check("atlas-trained", varbit("varb12065_bim_checkal") == 15, "checkal=" .. tostring(varbit("varb12065_bim_checkal")))
        t.check("atlas-flex-unlocked", varbit("varb12062_emote_flex") == 1, "emote_flex=" .. tostring(varbit("varb12062_emote_flex")))

        dump_pages("before-flex")
        t.ticklog.mark("leg.flex")
        t.exec("atlas.doorOut", t.player.pass_door, { closed = "fai_barbarian_large_doubledoor_l", open = "fai_barbarian_open_large_doubledoor_l",
            at = { 3078, 3435, 0 }, near = { 3078, 3436 }, far = { 3078, 3434 } })
        t.exec("goto-flexCheckal", t.player.goto_tile, 3086, 3415, 0)
        t.exec("flexCheckal-ask", t.player.talk_to, "bim_checkal_barb")
        t.exec("flexCheckal-ask-chat", t.chat.drain, {})
        t.check("flexCheckal-prompted", varbit("varb12065_bim_checkal") == 20, "checkal=" .. tostring(varbit("varb12065_bim_checkal")))
        local er, ed = t.player.emote("flex")
        t.ticks(5)
        play_scene("flexCheckal-chat", 40)
        t.check("flexCheckal", varbit("varb12065_bim_checkal") == 40, "emote=" .. tostring(er) .. " " .. tostring(ed) .. "; checkal=" .. tostring(varbit("varb12065_bim_checkal")))
        t.expect("quest.stage.crew", t.var.expect("varb12063_bim", 15))

        -- ---- Marley, the cook, the sandwich ([873]..[1186]) ----
        dump_pages("before-marley")
        t.ticklog.mark("leg.marley")
        t.exec("goto-marley", t.player.goto_tile, 3088, 3469, 0)
        stance("marley.stance", "bim_marley_edge")
        t.exec("talkToMarley", t.player.talk_to, "bim_marley_edge")
        t.exec("talkToMarley-chat", t.chat.drain, {})
        t.check("talkToMarley-wants-sandwich", varbit("varb12064_bim_marley") == 5, "marley=" .. tostring(varbit("varb12064_bim_marley")))

        dump_pages("before-cook")
        t.ticklog.mark("leg.cook")
        t.exec("goto-cook", t.player.goto_tile, 3230, 3400, 0)
        -- the cook's other three answers first, as 4374 asked them ([962]..[1038])
        t.exec("cookSell", t.player.talk_to, "fai_varrock_bluemoon_chef")
        t.exec("cookSell-menu", t.chat.drain, { stop_at = "options" })
        t.exec("cookSell-ask", t.chat.choose, "Can you sell me any food?")
        t.exec("cookSell-offer", t.chat.drain, { stop_at = "options" })
        t.exec("cookSell-buy", t.chat.choose, "Alright, I'll buy a cabbage.")
        t.exec("cookSell-tail", t.chat.drain, {})
        t.exec("cookSell-cabbage", t.inv.await, "cabbage", 1, 8)
        t.exec("cookFree", t.player.talk_to, "fai_varrock_bluemoon_chef")
        t.exec("cookFree-menu", t.chat.drain, { stop_at = "options" })
        t.exec("cookFree-ask", t.chat.choose, "Can you give me any free food?")
        t.exec("cookFree-tail", t.chat.drain, {})
        t.exec("cookKitchen", t.player.talk_to, "fai_varrock_bluemoon_chef")
        t.exec("cookKitchen-menu", t.chat.drain, { stop_at = "options" })
        t.exec("cookKitchen-ask", t.chat.choose, "I don't want anything from this horrible kitchen.")
        t.exec("cookKitchen-tail", t.chat.drain, {})
        t.exec("talkToCook", t.player.talk_to, "fai_varrock_bluemoon_chef")
        t.exec("talkToCook-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToCook-ask", t.chat.choose, "I was wondering if you'd be able to make me a Steak sandwich?")
        t.exec("talkToCook-tail", t.chat.drain, {})
        t.check("talkToCook-recipe", varbit("varb12064_bim_marley") == 10, "marley=" .. tostring(varbit("varb12064_bim_marley")))
        t.exec("makeSandwich", t.player.use_item_on_item, "knife", "bread")
        t.exec("makeSandwich-await", t.inv.await, "bim_steak_sandwich", 1, 12)

        t.exec("goto-feedMarley", t.player.goto_tile, 3088, 3469, 0)
        t.exec("feedMarley", t.player.talk_to, "bim_marley_edge")
        play_scene("feedMarley-chat", 60)
        t.check("feedMarley-recruited", varbit("varb12064_bim_marley") == 40, "marley=" .. tostring(varbit("varb12064_bim_marley")))
        local _, sandwiches = t.inv.count("bim_steak_sandwich")
        t.check("feedMarley-sandwich-consumed", sandwiches == 0, "sandwich=" .. tostring(sandwiches))

        -- ---- Burntof, an ale, the game upstairs ([1200]..[1349]) ----
        dump_pages("before-burntof")
        t.ticklog.mark("leg.burntof")
        t.exec("goto-burntof", t.player.goto_tile, 2956, 3368, 0)
        t.exec("talkToBurntof", t.player.talk_to, "bim_burntof_pub")
        t.exec("talkToBurntof-chat", t.chat.drain, {})
        t.check("talkToBurntof-wants-drink", varbit("varb12066_bim_burntof") == 5, "burntof=" .. tostring(varbit("varb12066_bim_burntof")))
        -- Kaylee on the customer floor, and the Mind Bomb, as 4374 [1224]..[1234]
        t.exec("buyAle", t.player.talk_to, "risingsun_barmaid2")
        t.exec("buyAle-menu", t.chat.drain, { stop_at = "options" })
        t.exec("buyAle-ale", t.chat.choose, "I'll try the Mind Bomb.")
        t.exec("buyAle-tail", t.chat.drain, {})
        t.exec("buyAle-await", t.inv.await, "wizards_mind_bomb", 1, 12)
        t.exec("goto-giveAle", t.player.goto_tile, 2956, 3368, 0)
        local burntof = t.player.by_symbol("npc", "bim_burntof_pub")
        local rps_mark = t.cutscene.mark()
        t.exec("giveAle", t.player.use_on, "wizards_mind_bomb", burntof)
        t.exec("giveAle-chat", t.chat.drain, { stop_at = "options", max_pages = 60 })
        t.check("giveAle-game", varbit("varb12066_bim_burntof") == 15, "burntof=" .. tostring(varbit("varb12066_bim_burntof")))
        t.exec("rps-round1", t.chat.choose, "Paper.")
        t.exec("rps-round1-tail", t.chat.drain, { stop_at = "options" })
        t.exec("rps-round2", t.chat.choose, "Scissors.")
        t.exec("rps-round2-tail", t.chat.drain, { stop_at = "options" })
        t.exec("rps-round3", t.chat.choose, "Rock.")
        play_scene("rps-tail", 80)
        t.exec("rps.cutscene", t.cutscene.await, "rps", { since = rps_mark, quiet = 120, expect = {
            { op = "moveto", height = 400 },
            { op = "lookat", height = 180 },
            { op = "reset" },
        } })
        t.check("rps-recruited", varbit("varb12066_bim_burntof") == 40, "burntof=" .. tostring(varbit("varb12066_bim_burntof")))
        t.expect("quest.stage.dungeon", t.var.expect("varb12063_bim", 20))
        at_tile("rps.back", 2956, 3368, 0)

        -- ---- Willow at the entrance: the excavation and the ruins ([1498]..[1747]) ----
        dump_pages("before-entrance")
        t.ticklog.mark("leg.entrance")
        t.exec("goto-entrance", t.player.goto_tile, 2997, 3496, 0)
        stance("willowEntrance.stance", "bim_willow_outside_entrace")
        stance("checkalEntrance.stance", "bim_checkal_temple_outside")
        stance("marleyEntrance.stance", "bim_marley_temple_outside")
        -- the crew first ([1474]..[1496]), then Willow: "No." and back ([1499]..[1512])
        t.exec("talkToBurntofEntrance", t.player.talk_to, "bim_burntof_temple_outside")
        t.exec("talkToBurntofEntrance-chat", t.chat.drain, {})
        t.exec("talkToCheckalEntrance", t.player.talk_to, "bim_checkal_temple_outside")
        t.exec("talkToCheckalEntrance-chat", t.chat.drain, {})
        t.exec("talkToMarleyEntrance", t.player.talk_to, "bim_marley_temple_outside")
        t.exec("talkToMarleyEntrance-chat", t.chat.drain, {})
        t.exec("talkToWillowEntrance", t.player.talk_to, "bim_willow_outside_entrace")
        t.exec("talkToWillowEntrance-pre", t.chat.drain, { stop_at = "options" })
        t.expect("quest.stage.gathered", t.var.expect("varb12063_bim", 25))
        t.exec("talkToWillowEntrance-no", t.chat.choose, "No.")
        t.exec("talkToWillowEntrance-no-tail", t.chat.drain, {})
        t.exec("talkToWillowAgain", t.player.talk_to, "bim_willow_outside_entrace")
        t.exec("talkToWillowAgain-pre", t.chat.drain, { stop_at = "options" })
        local dig_mark = t.cutscene.mark()
        t.exec("talkToWillowEntrance-yes", t.chat.choose, "Yes.")
        play_scene("excavation", 400)
        t.exec("excavation.cutscene", t.cutscene.await, "excavation", { since = dig_mark, quiet = 300, expect = {
            { op = "moveto", height = 600 },
            { op = "lookat", height = 400 },
            { op = "reset" },
        } })
        t.expect("quest.stage.guardian", t.var.expect("varb12063_bim", 35))
        t.check("crew-awake", varbit("varb12065_bim_checkal") == 50 and varbit("varb12064_bim_marley") == 50
            and varbit("varb12066_bim_burntof") == 50, "sides " .. tostring(varbit("varb12065_bim_checkal")) .. "/"
            .. tostring(varbit("varb12064_bim_marley")) .. "/" .. tostring(varbit("varb12066_bim_burntof")))

        -- ---- the four pillars on the Ancient Guardian ([1750]..[1822]) ----
        dump_pages("before-guardian")
        t.ticklog.mark("leg.guardian")
        t.exec("guardian-present", t.npc.await_present, "bim_golem_boss", 16, 10)
        for pillar = 1, 4 do
            t.exec("pillar" .. pillar, t.player.click_loc, "bim_boss_rock", 1)
            t.ticks(12)
            local _, food = t.inv.count("lobster")
            if (food or 0) > 0 then
                t.player.inv_op("lobster", 1)
                t.ticks(2)
            end
        end
        t.exec("guardian-dead", t.npc.await_gone, "bim_golem_boss", 20, 40)
        t.ticks(4)
        t.expect("quest.stage.cutscene", t.var.expect("varb12063_bim", 40))

        -- ---- the bag, the doors, Ramarno; the scroll ([1822]..[1925]) ----
        dump_pages("before-bag")
        t.ticklog.mark("leg.bag")
        local bag_mark = t.cutscene.mark()
        local _, coins_before = t.inv.count("coins")
        play_scene("bag", 200)
        t.exec("bag.cutscene", t.cutscene.await, "bag", { since = bag_mark, quiet = 150, expect = {
            { op = "moveto", height = 450 },
            { op = "lookat", height = 300 },
            { op = "reset" },
        } })
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", (coins_after or 0) - (coins_before or 0) == 2000,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after))
        t.quest.expect_complete()
        dump_pages("before-ramarno")
        t.ticklog.mark("leg.ramarno")
        t.ticks(6)
        play_scene("ramarno", 200)
        t.check("ramarno-intro", varbit("varb12068_camdozaal_ramarno_intro") == 1,
            "intro=" .. tostring(varbit("varb12068_camdozaal_ramarno_intro")))
        dump_pages("end")
        t.finish(0)
    end,
}
