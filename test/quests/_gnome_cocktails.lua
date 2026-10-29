-- Gnome cocktails and the generic rows of RSProx index 4400 ("RFD Start",
-- rev 241; docs/rsprox-analysis/quests/rfd_start_generic.md):
--   * the shaker's Mix-cocktail opens aluft_cooking (49); Create at Cooking 4
--     refuses with the recorded mesbox [905]; at Cooking 6 it takes the fruit
--     and the shaker, leaves a Mixed blast, 30 xp and the recorded mesbox [1178];
--   * Pour with no lemon slices: "You need a slice of lemon to finish this
--     cocktail." [1205]; knife on lemon: the Slice it / Dice it make-menu, "You
--     cut the lemon into slices." [1260..1263]; Pour: fruit blast + the shaker
--     back, 20 xp, "You pour the cocktail and add the finishing touches." [1269];
--   * Hudo's Talk-to pages then his shop [824..833]; the gnome waiter's pages
--     then Gianne's Restaurant [804..815];
--   * the necklace of passage's Rub -> The Outpost lands on 2429,3348 and
--     leaves the (3) [582..585];
--   * Pelters' Veg Stall: the crate's Buy opens the shop, a rotten tomato is
--     100 coins [119..129].
--
--     python3 tools/quest_gate/run.py --script test/quests/_gnome_cocktails.lua --name gnome_cocktails
return {
    id = "gnome_cocktails",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::clearinv",
        "::setlevel cooking 4",
        "::give cocktail_shaker 1",
        "::give pineapple 1",
        "::give lemon 2",
        "::give orange 1",
        "::give knife 1",
        "::give cocktail_glass_empty 1",
        "::give necklace_of_passage_4 1",
        "::give coins 1000",
    },

    run = function(t)
        t.ticklog.start()
        t.ticks(3)
        local function count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end
        local function newest()
            local r, rows = t.chat.lines(0)
            local s = 0
            if r == "ok" then
                for _, row in ipairs(rows) do
                    if row.serial > s then s = row.serial end
                end
            end
            return s
        end
        local function said(since, text)
            local r, rows = t.chat.lines(since)
            if r ~= "ok" then return false, tostring(r) end
            local seen = {}
            for _, row in ipairs(rows) do
                seen[#seen + 1] = tostring(row.text)
                if tostring(row.text):find(text, 1, true) then return true, row.text end
            end
            return false, table.concat(seen, " / ")
        end
        local function create()
            local wr, w = t.ui.widget("aluft_cooking:confirmation")
            if wr ~= "ok" then return wr, "aluft_cooking:confirmation " .. tostring(w) end
            return t.ui.invoke(w, 1)
        end

        -- Mix-cocktail at Cooking 4: refused.
        t.player.inv_op("cocktail_shaker", 1)
        t.exec("mix.open", t.ui.await_open, "aluft_cooking", 5)
        t.check("mix.low.create", create())
        t.exec("mix.low.box", t.ui.await_open, "messagebox", 5)
        t.exec("mix.low.refused", t.chat.play,
            { "mesbox:You either don't have all the ingredients, or the cooking level required (or both) to make this." })
        t.check("mix.low.kept", (count("cocktail_shaker") == 1 and count("pineapple") == 1 and count("lemon") == 2
            and count("orange") == 1 and count("aluft_shaker_fruit_blast") == 0) and "ok" or "hollow",
            "shaker " .. count("cocktail_shaker") .. ", lemon " .. count("lemon") .. ", mixed " .. count("aluft_shaker_fruit_blast"))
        local closed = t.ui.await_close("aluft_cooking", 3)
        t.check("mix.low.closed", closed, "aluft_cooking closed: " .. tostring(closed))

        -- At Cooking 6: the Mixed blast.
        t.cheat("::setlevel cooking 6")
        t.ticks(2)
        local _, snap = t.skill.snapshot()
        t.player.inv_op("cocktail_shaker", 1)
        t.exec("mix.open2", t.ui.await_open, "aluft_cooking", 5)
        t.check("mix.create", create())
        t.exec("mix.box", t.ui.await_open, "messagebox", 5)
        t.exec("mix.made", t.chat.play,
            { "mesbox:You just need to pour this into an empty cocktail glass and garnish with sliced lemon before serving." })
        t.check("mix.inv", (count("aluft_shaker_fruit_blast") == 1 and count("cocktail_shaker") == 0 and count("pineapple") == 0
            and count("orange") == 0 and count("lemon") == 1) and "ok" or "hollow",
            "mixed " .. count("aluft_shaker_fruit_blast") .. ", shaker " .. count("cocktail_shaker") .. ", lemon " .. count("lemon"))
        t.check("mix.xp", t.skill.expect_gain("cooking", 30, snap))

        -- Pour with no slices: the recorded line, nothing changes.
        local since = newest()
        t.player.inv_op("aluft_shaker_fruit_blast", 1)
        t.ticks(2)
        local ok1, line1 = said(since, "You need a slice of lemon to finish this cocktail.")
        t.check("pour.noslice", ok1 and "ok" or "hollow", tostring(line1))

        -- Knife on lemon: Slice it.
        since = newest()
        t.player.use_item_on_item("knife", "lemon")
        t.exec("cut.menu", t.ui.make, "a")
        t.exec("cut.slices", t.inv.await, "lemon_slices", 1, 6)
        local ok2, line2 = said(since, "You cut the lemon into slices.")
        t.check("cut.line", ok2 and "ok" or "hollow", tostring(line2))

        -- Pour: the fruit blast.
        _, snap = t.skill.snapshot()
        t.player.inv_op("aluft_shaker_fruit_blast", 1)
        t.exec("pour.box", t.ui.await_open, "messagebox", 5)
        t.exec("pour.done", t.chat.play, { "mesbox:You pour the cocktail and add the finishing touches." })
        t.check("pour.inv", (count("fruit_blast") == 1 and count("cocktail_shaker") == 1 and count("cocktail_glass_empty") == 0
            and count("lemon_slices") == 0 and count("aluft_shaker_fruit_blast") == 0) and "ok" or "hollow",
            "blast " .. count("fruit_blast") .. ", shaker " .. count("cocktail_shaker") .. ", glass " .. count("cocktail_glass_empty"))
        t.check("pour.xp", t.skill.expect_gain("cooking", 20, snap))

        -- Hudo, upstairs in the Grand Tree.
        t.cheat("::tele 1_38_54_18_53")
        t.ticks(4)
        t.exec("hudo.talk", t.player.talk_to, "gnome_hudo", 1)
        t.exec("hudo.pages", t.chat.play, {
            "player:Hello there.",
            "npc:Hello there traveller. Would you like some groceries? I have a large selection.",
            "choose:I'll have a look.",
            "player:I'll have a look.",
            "npc:Great stuff.",
        })
        t.exec("hudo.shop", t.ui.await_open, "shopmain", 5)
        t.ui.close_modal()
        t.ticks(2)

        -- The gnome waiter.
        t.exec("waiter.talk", t.player.talk_to, "gnome_waiter", 1)
        t.exec("waiter.pages", t.chat.play, {
            "player:Hello.",
            "npc:can I tempt you with any of the dishes on our new menu?",
            "choose:I'll take a look.",
            "player:I'll take a look.",
            "npc:I hope you like what you see. Although all these premade dishes are good to eat - they were made by the last human assistant chef.  He wasn't up to Chef's exacting standards - and you may find that they will",
            "npc:not be accepted by some people as the 'real' thing.",
        })
        t.exec("waiter.shop", t.ui.await_open, "shopmain", 5)
        t.ui.close_modal()
        t.ticks(2)

        -- Necklace of passage: Rub -> The Outpost.
        t.player.inv_op("necklace_of_passage_4", 4, { subop = 2 })
        t.ticks(8)
        local tr, tile = t.world.tile()
        t.check("passage.landed", (tr == "ok" and tile.x == 2429 and tile.z == 3348) and "ok" or "hollow",
            "tile " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))
        t.check("passage.charge", (count("necklace_of_passage_3") == 1 and count("necklace_of_passage_4") == 0) and "ok" or "hollow",
            "(3) " .. count("necklace_of_passage_3") .. ", (4) " .. count("necklace_of_passage_4"))

        -- The Stronghold gate with Femi beside it [659..718]: her boxes first,
        -- then the gate crossing north.
        t.cheat("::tele 0_38_52_28_54")
        t.ticks(4)
        -- The boxes beside her [651]..[653]: the search line, nothing else.
        t.exec("boxes3.search", t.player.click_loc, "boxes3", 1)
        t.ticks(3)
        t.expect("boxes3.line", t.msg.expect("There is nothing interesting in these boxes."))
        t.exec("femi.gate", t.player.click_loc, "gnome_areagate", 1)
        t.exec("femi.pages", t.chat.play, {
            "npc:Hello there.",
            "player:Hi!",
            "npc:Could you help me lift these boxes? They are really heavy!",
            "choose:Okay then.",
            "player:Okay then.",
            "npc:Thanks traveller!",
        })
        t.exec("femi.thanks", t.ui.await_open, "chat_left", 15)
        t.exec("femi.thanks.page", t.chat.play, { "npc:Thanks again friend!" })
        t.ticks(2)
        -- No page and no item: the crossing itself is the answer, read below.
        t.player.click_loc("gnome_areagate", 1)
        t.ticks(10)
        local gr, gt = t.world.tile()
        t.check("gate.through", (gr == "ok" and gt.z >= 3385) and "ok" or "hollow",
            "tile " .. tostring(gt and gt.x) .. "," .. tostring(gt and gt.z))

        -- The Stronghold's spirit tree, The Grand Tree not started [752].
        t.cheat("::tele 0_38_53_27_61")
        t.ticks(4)
        t.exec("ent.talk", t.player.click_loc, "stronghold_ent", 2)
        t.exec("ent.page", t.chat.play, { "mesbox:The tree doesn't feel like talking." })

        -- Pelters' Veg Stall at Varrock's pillory.
        t.cheat("::tele 0_50_53_26_22")
        t.ticks(4)
        t.exec("pelters.click", t.player.click_loc, "pillory_rotten_tomato_crate", 1)
        t.exec("pelters.open", t.ui.await_open, "shopmain", 5)
        t.exec("pelters.attach", t.shop.attach, "pillory_rottenfruitshop")
        local coins = count("coins")
        t.exec("pelters.buy", t.shop.buy, "rotten_tomato", 1)
        t.check("pelters.price", (coins - count("coins") == 100) and "ok" or "hollow",
            "paid " .. (coins - count("coins")))
        t.ui.close_modal()
        t.ticks(2)
    end,
}
