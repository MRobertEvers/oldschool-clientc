-- The skilling make-menus LostCity's port skipped, each opening interface 270
-- `skillmulti` the way Jagex does (docs/rsprox-analysis/lostcity_menus.md):
-- unfinished and finished potions, gem cutting, feathering and arrowtips,
-- amulet stringing, wine, knife on bread, cooking on a fire, raw beef on a
-- range (Sinew | Cooked meat), and Aggie's Make-dyes. One makeable item makes
-- it without the panel. A scriptrun press answers with quantity 1.
--
--     python3 tools/quest_gate/run.py --script test/quests/_skill_menus.lua --name skill_menus
return {
    id = "skill_menus",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::clearinv",
        "::complete quest_druidicritual",
        "::setlevel herblore 99",
        "::setlevel crafting 99",
        "::setlevel fletching 99",
        "::setlevel cooking 99",
        "::setlevel firemaking 99",
    },

    run = function(t)
        t.ticks(3)
        local function count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end
        local function give(...)
            t.cheat("::clearinv")
            for _, line in ipairs({ ... }) do
                t.cheat("::give " .. line)
            end
            t.ticks(1)
        end
        -- The use verb waits for a page or a changed pack; the make-menu is
        -- neither, so the use is fired and the menu is what is checked.
        local function menu(name, a, b, letter, product, want)
            t.player.use_item_on_item(a, b)
            t.exec(name .. ".menu", t.ui.make, letter or "a")
            t.exec(name .. ".made", t.inv.await, product, want or 1, 10)
            t.ticks(2)
        end

        -- Herblore: two guam leaves, two vials -> the menu; one leaf -> none.
        give("guam_leaf 2", "vial_water 3", "marentill 1")
        menu("unf", "guam_leaf", "vial_water", "a", "guamvial", 1)
        t.exec("unf.single", t.player.use_item_on_item, "marentill", "vial_water")
        t.exec("unf.single.made", t.inv.await, "marrentillvial", 1, 10)
        local single = t.ui.await_open("skillmulti", 2)
        t.check("unf.single.nopanel", single ~= "ok" and "ok" or "hollow", "skillmulti " .. tostring(single))
        give("guamvial 2", "eye_of_newt 2")
        menu("finished", "eye_of_newt", "guamvial", "a", "3dose1attack", 1)

        give("chisel 1", "uncut_sapphire 2")
        menu("gem", "chisel", "uncut_sapphire", "a", "sapphire", 1)

        give("feather 30", "arrow_shaft 30")
        menu("feather", "feather", "arrow_shaft", "a", "headless_arrow", 15)
        give("bronze_arrowheads 30", "headless_arrow 30")
        menu("tips", "bronze_arrowheads", "headless_arrow", "a", "bronze_arrow", 15)

        give("ball_of_wool 2", "unstrung_gold_amulet 2")
        menu("string", "ball_of_wool", "unstrung_gold_amulet", "a", "strung_gold_amulet", 1)
        give("grapes 2", "jug_water 2")
        menu("wine", "grapes", "jug_water", "a", "jug_unfermented_wine", 1)
        give("knife 1", "bread 2")
        menu("bread", "knife", "bread", "a", "hundred_pirate_breadcrumbs", 1)

        -- A fire, two raw shrimps on it.
        give("logs 1", "tinderbox 1", "raw_shrimp 2")
        t.exec("fire.light", t.player.use_item_on_item, "tinderbox", "logs")
        t.ticks(12)
        t.player.use_on("raw_shrimp", t.player.by_symbol("loc", "fire"))
        t.exec("fire.menu", t.ui.make, "a")
        t.exec("fire.cooked", t.inv.await, "shrimp", 1, 15)

        -- Lumbridge's kitchen range, raw beef: Sinew | Cooked meat.
        t.cheat("::setvar cookquest ^cook_complete")
        give("raw_beef 2")
        t.cheat("::tele 0_50_50_9_14")
        t.ticks(4)
        t.player.use_on("raw_beef", t.player.by_symbol("loc", "cooksquestrange"))
        t.exec("sinew.menu", t.ui.make, "a")
        t.exec("sinew.made", t.inv.await, "xbows_sinew", 1, 15)

        -- Aggie's Make-dyes: blue, red, yellow; red is cell b.
        give("coins 100", "redberries 3")
        t.cheat("::tele 0_48_50_14_58")
        t.ticks(4)
        -- Asking for dyes once is what gives Aggie her Dyes op.
        t.exec("dye.ask", t.player.talk_to, "aggie", 1)
        t.exec("dye.ask-dialog", t.chat.play, {
            "npc:What can I help you with?",
            "choose:Can you make dyes for me please?",
            "player:Can you make dyes for me please?",
            "npc:What sort of dye would you like?",
            "choose:No thanks, I am happy the colour I am.",
            "*",
        })
        t.ticks(2)
        t.player.talk_to("aggie", 3)
        t.exec("dye.menu", t.ui.make, "b")
        t.exec("dye.box", t.ui.await_open, "objectbox_double", 10)
        t.exec("dye.text", t.ui.expect_text, "objectbox_double:text", "You hand the redberries and payment to Aggie.", 5)
        t.check("dye.made", count("reddye") == 1 and count("redberries") == 0 and "ok" or "hollow",
            "red dye " .. count("reddye") .. ", redberries " .. count("redberries"))
    end,
}
