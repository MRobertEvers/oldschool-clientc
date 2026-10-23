-- The Ides of Milk, end to end, as RSProx index 4325 (rev 241) and index
-- 4129 (rsprox.net 4153, rev 238) played it: Cassius -> Gillie -> Seth ->
-- the shelf and the book -> Cassius and the first sample -> the Duke ->
-- Gillie and the second sample -> the pen gate, the arena and Brutus ->
-- Gillie -> Cassius and the scroll -> Gillie's cowbell amulet and lamp.
--
-- docs/rsprox-analysis/quests/ides_of_milk.md is the timeline the content was
-- written from and the audit against this test's tick log: the run keeps
-- build/quest_gate/<run>/ticklog.tsv for tools/rsprox/ticklog_tells.py.
--
-- Every stage below is the varbit value Jagex wrote (idesofmilk.constant).
-- The talks are drained page by page (t.chat.drain); a choice is the row the
-- recording chose.
--
-- Brutus's ordinary swing cannot kill during the quest and his specials take
-- 14..19; 99 hitpoints, rune armour and sharks carry the fight without
-- dodging.
return {
    id = "idesofmilk",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::idesofmilk", -- resets the quest and stands the player by Cassius
        "::setlevel attack 99",
        -- Strength 30, not 99: Brutus has to live long enough to use his
        -- specials, which the audit compares with the recordings.
        "::setlevel strength 30",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_scimitar 1",
        "::give mithril_platebody 1", -- rune needs Dragon Slayer
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 10",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb20106_cowquest",
            constants = {
                not_started = 0, started = 1, empire = 2, investigate = 3, book = 4,
                book_given = 5, drink1 = 6, drank1 = 8, duke = 10, gillie2 = 12,
                drink2 = 14, drank2 = 16, fight = 18, bull_dead = 20, finish = 21,
                complete = 22,
            },
            row = "quest_idesofmilk",
            display = "The Ides of Milk",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.note(select(2, t.ticklog.start()))

        t.ticks(3) -- the setup cheats' effects are not client-side yet
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")
        for _, item in ipairs({ "rune_scimitar", "mithril_platebody", "rune_platelegs", "rune_full_helm", "rune_kiteshield" }) do
            t.exec("equip." .. item, t.player.equip, item)
        end

        local function drain(name, opts)
            local r, d = t.chat.drain(opts or { max_pages = 80 })
            t.expect(name, r, d)
        end
        local function talk(name, npc, stop_at)
            t.exec(name, t.player.talk_to, npc, 1)
            drain(name .. ".pages", { stop_at = stop_at, max_pages = 80 })
        end

        -- Crossings: every gate and door between the player and a target is
        -- clicked (docs/QUEST_ORCHESTRATOR.md door rule).
        local function field_in(tag)
            t.exec("goto-" .. tag .. ".fieldGate", t.player.goto_tile, 3251, 3266, 0)
            t.exec(tag .. ".fieldGateIn", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
                at = { 3253, 3266, 0 }, near = { 3252, 3266 }, far = { 3254, 3267 },
                far_ok = function(tt) return tt.x >= 3253 end, far_desc = "in the cow field, x >= 3253" })
        end
        local function field_out(tag)
            t.exec(tag .. ".fieldGateOut", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
                at = { 3253, 3266, 0 }, near = { 3254, 3266 }, far = { 3250, 3266 },
                far_ok = function(tt) return tt.x <= 3252 end, far_desc = "outside the cow field, x <= 3252" })
        end
        local function climb(name, sym, op, from_level, to_level)
            t.exec(name, t.player.click_loc, sym, op, { at = { 3204, 3229, from_level } })
            t.expect(name .. ".level", t.await({
                level = function()
                    local lr, lt = t.world.tile()
                    return lr == "ok" and type(lt) == "table" and lt.level == to_level
                end,
                note = sym .. ": waiting for level " .. to_level,
            }, 10))
        end

        -- -------------------------------------------------------------
        -- Cassius (4325 [193..245])
        -- -------------------------------------------------------------
        t.exec("goto-talkToCassius", t.player.goto_tile, 3171, 3277, 0)
        talk("talkToCassius", "cowboss_farmer", "options")
        t.exec("talkToCassius.yes", t.chat.choose, "Yes.")
        drain("talkToCassius.brief")
        t.exec("quest.stage.investigate", t.quest.expect_stage, "investigate")

        -- -------------------------------------------------------------
        -- Gillie (4325 [296..318]) and Seth (4325 [373..411])
        -- -------------------------------------------------------------
        field_in("talkToGillie")
        talk("talkToGillie", "gillie_the_milkmaid", "options")
        t.exec("talkToGillie.productive", t.chat.choose, "Can you share what makes your cows so productive?")
        drain("talkToGillie.happiness")
        t.exec("var.gillieInformation", t.var.await, "varb20107_cowquest_gillie_information", 1, 5)
        field_out("talkToSeth")

        t.exec("goto-talkToSeth.yardGate", t.player.goto_tile, 3239, 3296, 0)
        t.exec("talkToSeth.yardGateIn", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
            at = { 3236, 3296, 0 }, near = { 3237, 3296 }, far = { 3234, 3294 },
            far_ok = function(tt) return tt.x <= 3236 end, far_desc = "in Seth's farmyard, x <= 3236" })
        t.exec("talkToSeth.houseDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3230, 3291, 0 }, near = { 3231, 3291 }, far = { 3229, 3291 },
            far_ok = function(tt) return tt.x <= 3230 end, far_desc = "in the farmhouse, x <= 3230" })
        -- Seth's own room is behind its door (the recorder opened it too, 4325
        -- [366]); he wanders, so he may be either side of it.
        t.exec("talkToSeth.roomDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3225, 3293, 0 }, near = { 3225, 3293 }, far = { 3224, 3292 },
            far_ok = function(tt) return tt.x <= 3224 end, far_desc = "in Seth's room, x <= 3224" })
        talk("talkToSeth", "favour_seth_groats")
        t.exec("quest.stage.book", t.quest.expect_stage, "book")
        t.exec("var.sethInformation", t.var.await, "varb20108_cowquest_seth_information", 1, 5)

        -- -------------------------------------------------------------
        -- The shelf and the book (4325 [427..440])
        -- -------------------------------------------------------------
        t.exec("searchShelves.roomDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3225, 3293, 0 }, near = { 3224, 3293 }, far = { 3227, 3288 },
            far_ok = function(tt) return tt.x >= 3225 end, far_desc = "in the farmhouse's main room, x >= 3225" })
        t.exec("searchShelves", t.player.click_loc, "cowquest_seth_shelf", 1)
        drain("searchShelves.box")
        t.exec("inv.husbandryBook", t.inv.await, "cowquest_husbandry_book", 1, 5)
        t.exec("readBook", t.player.inv_op, "cowquest_husbandry_book", 1)
        t.expect("readBook.open", t.ui.await_open("book", 5))
        t.expect("readBook.page1", t.ui.expect_text("book:page_left_text_5", "Happy animals make happy", 3))
        t.exec("readBook.turn", t.ui.press_pause, "book:page_right_button", -1, "book:page_right_button")
        t.expect("readBook.page3", t.ui.expect_text("book:page_left_text_3", "Principle Three.", 3))
        t.expect("readBook.close", t.chat.close())
        t.ticks(2)

        -- -------------------------------------------------------------
        -- Cassius and the first sample (4325 [505..552])
        -- -------------------------------------------------------------
        t.exec("returnToCassius.houseDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3230, 3291, 0 }, near = { 3229, 3291 }, far = { 3232, 3291 },
            far_ok = function(tt) return tt.x >= 3231 end, far_desc = "out in the farmyard, x >= 3231" })
        t.exec("returnToCassius.yardGateOut", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
            at = { 3236, 3296, 0 }, near = { 3236, 3296 }, far = { 3239, 3296 },
            far_ok = function(tt) return tt.x >= 3237 end, far_desc = "outside Seth's farmyard, x >= 3237" })
        t.exec("goto-returnToCassius", t.player.goto_tile, 3171, 3277, 0)
        talk("returnToCassius", "cowboss_farmer")
        t.exec("quest.stage.drink1", t.quest.expect_stage, "drink1")
        t.exec("inv.milkSample1", t.inv.expect_has, "cowquest_milk_sample_1", 1)

        local _, hp_before = t.skill.read("hitpoints")
        t.exec("drinkMilkSample1", t.player.inv_op, "cowquest_milk_sample_1", 1)
        t.ticks(7) -- the page with no continue, the cough at O+3, the next page at O+5
        drain("drinkMilkSample1.cassius")
        t.exec("quest.stage.duke", t.quest.expect_stage, "duke")
        t.exec("inv.milkSample2", t.inv.expect_has, "cowquest_milk_sample_2", 1)
        local _, hp_after = t.skill.read("hitpoints")
        t.check("drinkMilkSample1.damage",
            type(hp_before) == "table" and type(hp_after) == "table" and hp_after.level == hp_before.level - 1,
            "hitpoints " .. tostring(hp_before and hp_before.level) .. " -> " .. tostring(hp_after and hp_after.level)
                .. " (the milk's 1 on the poison splat, 4325 [531])")

        -- -------------------------------------------------------------
        -- The Duke (4325 [582..604])
        -- -------------------------------------------------------------
        t.exec("goto-talkToDuke.castleCourtyard", t.player.goto_tile, 3222, 3218, 0)
        t.exec("talkToDuke.toStairs", t.player.walk_route,
            { { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } }, { level = 0 })
        climb("talkToDuke.stairsUp", "spiralstairsbottom_3", 1, 0, 1)
        t.exec("talkToDuke.dukeDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3222, 1 }, near = { 3207, 3222 }, far = { 3209, 3221 },
            far_ok = function(tt) return tt.x >= 3208 end, far_desc = "in the Duke's room, x >= 3208" })
        talk("talkToDuke", "duke_of_lumbridge")
        t.exec("quest.stage.gillie2", t.quest.expect_stage, "gillie2")

        t.exec("talkToGillieAgain.dukeDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3222, 1 }, near = { 3208, 3222 }, far = { 3206, 3224 },
            far_ok = function(tt) return tt.x <= 3207 end, far_desc = "out of the Duke's room, x <= 3207" })
        t.exec("talkToGillieAgain.toStairs", t.player.walk_route, { { 3205, 3228 } }, { level = 1 })
        climb("talkToGillieAgain.stairsDown", "spiralstairsmiddle", 3, 1, 0)
        t.exec("talkToGillieAgain.outOfCastle", t.player.walk_route,
            { { 3207, 3227 }, { 3214, 3226 }, { 3215, 3219 }, { 3222, 3218 } }, { level = 0 })

        -- -------------------------------------------------------------
        -- Gillie and the second sample (4325 [705..745])
        -- -------------------------------------------------------------
        field_in("talkToGillieAgain")
        talk("talkToGillieAgain", "gillie_the_milkmaid")
        t.exec("quest.stage.drink2", t.quest.expect_stage, "drink2")
        t.exec("drinkMilkSample2", t.player.inv_op, "cowquest_milk_sample_2", 1)
        drain("drinkMilkSample2.gillie")
        t.exec("quest.stage.fight", t.quest.expect_stage, "fight")

        -- -------------------------------------------------------------
        -- The pen gate, the arena and Brutus (4325 [762..813])
        -- -------------------------------------------------------------
        t.exec("releaseBull.walk", t.player.walk_route, { { 3256, 3278 }, { 3259, 3286 }, { 3261, 3293 } }, { level = 0 })
        t.exec("releaseBull", t.player.click_loc, "fencegate_l_cowboss_start", 1)
        t.exec("releaseBull.menu", t.chat.play, { "options", "choose:Yes." })
        t.ticks(6) -- R+4: the arena, the fade in and the box with its continue
        drain("releaseBull.box")
        local present_result, present_detail = t.npc.await_present("cowboss", 16, 10)
        t.expect("arena.brutus", present_result, present_detail)
        t.ticks(4) -- he comes at spawn+6

        t.exec("killBrutus.engage", t.player.attack, "cowboss", 2, 15)
        local dead = false
        for round = 1, 12 do
            local r, d = t.npc.await_dead_engaged(60, 16, { eat = { item = "shark", below = 45 } })
            t.note("round " .. round .. ": " .. tostring(r) .. " " .. tostring(d))
            if r == "ok" then
                dead = true
                break
            end
            if r == "no_row" then
                t.player.attack("cowboss", 2, 16)
            end
        end
        t.check("killBrutus", dead, "Brutus (58 hp) dead")
        t.expect("player.aliveAfterBrutus", t.player.alive())
        t.exec("quest.stage.bull_dead", t.quest.expect_stage, "bull_dead")
        t.ticks(2)
        drain("brutusTag.box")
        t.expect("arena.left", t.await({
            level = function()
                local r, here = t.world.tile()
                return r == "ok" and here.x >= 3240 and here.x <= 3270 and here.z >= 3255 and here.z <= 3300
            end,
            note = "back on the real field (x 3240..3270, z 3255..3300)",
        }, 12))
        t.ticks(4)

        -- -------------------------------------------------------------
        -- Gillie (4325 [818..836]) and Cassius (4325 [895..922])
        -- -------------------------------------------------------------
        t.exec("talkToGillieAfterBull.walk", t.player.walk_route, { { 3258, 3284 }, { 3255, 3275 } }, { level = 0 })
        talk("talkToGillieAfterBull", "gillie_the_milkmaid")
        t.exec("quest.stage.finish", t.quest.expect_stage, "finish")

        field_out("finishQuest")
        t.exec("goto-finishQuest", t.player.goto_tile, 3171, 3277, 0)
        talk("finishQuest", "cowboss_farmer")
        t.ticks(3)
        t.quest.expect_complete()
        t.expect("finishQuest.closeScroll", t.chat.close())

        -- -------------------------------------------------------------
        -- Gillie's rewards and the lamp (4129 [780..807])
        -- -------------------------------------------------------------
        field_in("collectRewards")
        talk("collectRewards", "gillie_the_milkmaid")
        t.exec("reward.cowbellAmulet", t.inv.expect_has, "cowbell_amulet_empty", 1)
        t.exec("reward.lamp", t.inv.expect_has, "cowboss_reward_lamp", 1)
        t.exec("var.reward", t.var.await, "varb20109_cowquest_reward", 1, 5)

        t.exec("rubLamp", t.player.inv_op, "cowboss_reward_lamp", 1)
        t.exec("rubLamp.skill", t.chat.play, { "options", "choose:More options...", "options", "choose:Prayer", "options", "choose:Yes.", "mesbox:Your wish has been granted!" })
        t.exec("var.lampUsed", t.var.await, "varb20110_cowboss_reward_lamp", 1, 5)

        t.finish(0)
    end,
}
