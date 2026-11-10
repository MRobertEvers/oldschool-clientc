-- The Giant Dwarf's recorded flow, RSProx index 4491 (the boatman, the arrest,
-- Veldaban), 4492 (Blasidar), 4494 (Riki and the head) and 4501 (the Consortium).
-- Reaches each conversation by teleport; docs/rsprox-analysis/quests/the_giant_dwarf.md.
--
--     python3 tools/quest_gate/run.py --no-build --script test/quests/_giantdwarf_recorded.lua --name giantdwarf_recorded
return {
    id = "giantdwarf_recorded",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::setlevel crafting 12",
        "::setlevel firemaking 16",
        "::setlevel magic 45",
        "::setlevel thieving 20",
        "::setlevel hitpoints 99",
    },

    run = function(t)
        t.ticklog.start()
        local function stage(name, want)
            local _, v = t.var.server("varb571_giantdwarf_quest")
            t.check("stage." .. name, v == want, "stage " .. tostring(v) .. " want " .. tostring(want))
        end
        local function await_page(n)
            for _ = 1, n or 12 do
                if t.chat.kind() ~= "none" then return end
                t.ticks(1)
            end
        end
        local function talk(name, npc, pages)
            local r, d
            for try = 1, 6 do
                r, d = t.player.talk_to(npc, 1)
                if r == "ok" or t.chat.kind() ~= "none" then break end
                t.note(name .. "-try" .. try .. ": " .. tostring(r) .. " " .. tostring(d))
                t.ticks(3)
            end
            await_page()
            t.exec(name .. "-dialog", t.chat.play, pages)
            t.ticks(2)
        end

        -- The boatman, index 4491 [309..331].
        t.check("boatman.tele", t.cheat("::tele 0_44_158_24_16") == "ok", "boatman bank")
        t.ticks(3)
        talk("boatman", "dwarf_city_boatman_mines", {
            "npc:Ho there, human!",
            "player:",
            "npc:Ho there,",
            "player:Where are you going? Across the river?",
            "npc:No no, that's what the ferryman is for!",
            "player:How much will that cost me then?",
            "npc:For a human like you, I can do it for free!",
            "choose:That's a deal!",
            "player:That's a deal!",
            "npc:Excellent! I'm just waiting for my ship",
            "npc:Mind, this trip could take a few minutes!",
            "choose:Yes.",
            "player:Yes, I'm ready and don't mind",
            "npc:Well, let's not waste any more time then!",
        })
        t.ticks(2)
        stage("ride", 5)
        -- The ride, the crash and the arrest advance by themselves (index 4491 [478..532]).
        for _ = 1, 320 do
            local r, tl = t.world.tile()
            if r == "ok" and tl.x >= 2825 and tl.x <= 2829 and tl.z >= 10208 and tl.z <= 10217 then break end
            t.ticks(1)
        end
        t.ticks(6)
        await_page(40)
        t.exec("veldaban-dialog", t.chat.play, {
            "npc:Identify yourself, human!",
            "player:I err, I'm",
            "npc:Ah, so you're the one who knocked over",
            "player:I suppose so...",
            "npc:Ah, don't worry about it!",
            "npc:We haven't been ruled by a king",
            "npc:But let me introduce myself.",
            "player:Well, ehm... nice to meet you, Veldaban.",
            "npc:Now listen carefully.",
            "npc:I want you to go and see the local sculptor",
            "npc:Will you do this for us?",
            "choose:Yes, I will do this.",
            "player:Yes, I will do this.",
            "npc:You are a good human,",
            "npc:Now, I need you to go to Blasidar",
            "player:To be honest",
            "npc:Oh, don't worry!",
            "npc:I'm sure there are many tasks",
            "player:I'll see what I can do.",
        })
        t.ticks(2)
        stage("veldaban", 10)

        -- Blasidar, index 4492 [437].
        t.check("blasidar.tele", t.cheat("::tele 0_45_159_27_29") == "ok", "blasidar's house")
        t.ticks(3)
        talk("blasidar", "dwarf_city_shop_sculpture", {
            "player:Excuse me, are you Blasidar",
            "npc:Why, yes, yes, that I am!",
            "player:Oh, well, I'm",
            "npc:Oh, I don't need an assistant sculptor",
            "player:I'm not a sculptor.",
            "npc:And I've also got a model here",
            "player:I'm not a model!",
            "npc:But come to think of it",
            "npc:Yes, you will do just fine.",
            "player:Ehm, right, okay.",
            "player:So what did you need doing?",
            "npc:Well, see...",
            "npc:But the statue itself",
            "player:Not a very artistic race",
            "npc:No, that's not what I said",
            "npc:We're excellent with",
            "npc:But I digress",
            "npc:Will you be able to help me with this",
            "choose:Yes, I will do this.",
            "player:Yes, I will do this.",
            "npc:Excellent, excellent!",
            "npc:I will need a pair of boots",
            "npc:For the torso",
            "npc:Also, I need a battleaxe.",
            "player:Right, so I need",
            "npc:For now,",
            "player:But... wait, wait",
            "npc:I don't know!",
            "npc:But it's a beautiful city",
        })
        t.ticks(2)
        stage("blasidar", 20)

        -- Riki, in the recorded order: boots, clothes, axe (4492 [2619], 4494 [1956], [4654]).
        t.cheat("::give dwarf_perfect_pair_of_boots")
        t.cheat("::give dwarf_clothes")
        t.cheat("::give dwarf_battleaxe_new")
        t.ticks(2)
        talk("riki.boots", "dwarf_city_shop_sculpture_model_multi", {
            "player:Here, I have these boots for you to wear.",
            "npc:Thank... you.",
        })
        talk("riki.clothes", "dwarf_city_shop_sculpture_model_multi", {
            "player:I have some clothes for you to wear.",
            "npc:Thank... you.",
        })
        talk("riki.axe", "dwarf_city_shop_sculpture_model_multi", {
            "player:I have this battleaxe for you.",
            "npc:Thank... you.",
        })
        local _, model = t.var.server("varb576_giantdwarf_model_state")
        t.check("model.finished", model == 7, "model_state " .. tostring(model))

        talk("blasidar.head", "dwarf_city_shop_sculpture", {
            "player:I've all the items you asked me",
            "npc:Yes, yes, that will do just fine.",
            "player:So you need me to find a crown",
            "npc:Oh no, no no",
            "player:So what is the head supposed",
            "npc:I don't think they've quite made a decision",
            "player:What? But I thought",
            "npc:Yes, the Consortium can be a little fickle",
            "npc:But can you go and see",
            "player:No, I suppose you can't.",
            "npc:Great!",
        })
        t.ticks(2)
        stage("head", 30)

        -- The Grand Exchange trapdoor and the cart back (index 4494 [2816..2838], 4495 [1294..1316]).
        t.check("trapdoor.tele", t.cheat("::tele 0_49_54_5_48") == "ok", "the Grand Exchange trapdoor")
        t.ticks(3)
        t.exec("trapdoor", t.player.click_loc, "ge_keldagrim_trapdoor", 2, { at = { 3140, 3504, 0 } })
        local tl
        for _ = 1, 60 do
            local r
            r, tl = t.world.tile()
            if r == "ok" and tl.x == 2909 and tl.z == 10174 then break end
            t.ticks(1)
        end
        t.check("trapdoor.landed", tl and tl.x == 2909 and tl.z == 10174, "at " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z))
        t.check("cart.tele", t.cheat("::tele 0_45_158_43_58") == "ok", "the front of track 3")
        t.ticks(3)
        t.exec("cart", t.player.click_loc, "keldagrim_train_cart", 1, { at = { 2923, 10171, 0 } })
        for _ = 1, 70 do
            local r
            r, tl = t.world.tile()
            if r == "ok" and tl.x == 3141 and tl.z == 3504 then break end
            t.ticks(1)
        end
        t.check("cart.landed", tl and tl.x == 3141 and tl.z == 3504, "at " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z))

        -- A secretary and her task, index 4495 [1203..1422].
        t.check("palace.tele", t.cheat("::tele 1_44_159_60_20") == "ok", "the palace floor")
        t.ticks(3)
        t.exec("secretary", t.player.talk_to, "dwarf_city_secretary_green_gemstone", 1)
        await_page()
        local kind = t.chat.kind()
        for _ = 1, 3 do
            if t.chat.kind() == "options" then break end
            t.chat.continue_()
            t.ticks(2)
        end
        t.check("secretary.menu", t.chat.kind() == "options", "kind " .. tostring(kind))
        t.chat.choose("I have to go!")
        for _ = 1, 4 do
            if t.chat.kind() == "none" then break end
            t.chat.continue_()
            t.ticks(2)
        end

        -- The deliveries, the hidden points and joining (index 4495..4501).
        local TASK = { [1] = "clay", [2] = "copper_ore", [3] = "tin_ore", [4] = "iron_ore", [5] = "silver_ore",
            [6] = "gold_ore", [7] = "mithril_ore", [8] = "coal", [11] = "bronze_bar", [12] = "iron_bar",
            [13] = "steel_bar", [14] = "silver_bar", [15] = "gold_bar", [16] = "mithril_bar" }
        local function points()
            local _, p = t.var.server("varp9543_gdwarf_points_green_gemstone")
            return p or 0
        end
        local function task()
            local _, i = t.var.server("varp9548_gdwarf_task_item")
            local _, n = t.var.server("varp9549_gdwarf_task_amount")
            return i or 0, n or 0
        end
        -- Press the first row that exists; a menu's rows change with the state.
        local function drive(rows, name)
            for _ = 1, 40 do
                local kind = t.chat.kind()
                if kind == "none" then return end
                if kind == "options" then
                    local done = false
                    for _, row in ipairs(rows) do
                        if t.chat.choose(row) == "ok" then done = true; break end
                    end
                    t.check(name .. ".row", done, "no row of this menu was expected")
                    if not done then return end
                else
                    t.chat.continue_()
                end
                t.ticks(2)
            end
        end
        local function converse(npc, rows, name)
            local r, d
            for try = 1, 8 do
                r, d = t.player.talk_to(npc, 1)
                if r == "ok" or t.chat.kind() ~= "none" then break end
                t.ticks(3)
            end
            t.check(name .. ".talk", r == "ok" or t.chat.kind() ~= "none", tostring(r) .. " " .. tostring(d))
            await_page()
            drive(rows, name)
            t.ticks(2)
        end

        local SEC = "dwarf_city_secretary_green_gemstone"
        local DIR = "dwarf_city_director_green_gemstone"
        for i = 1, 12 do
            if points() >= 75 then break end
            converse(SEC, { "I'll take it.", "Is there anything I can help you with?", "Do you have another task for me?" }, "ore" .. i)
            local item, n = task()
            t.check("ore" .. i .. ".task", item > 0 and n >= 3, "item " .. item .. " x" .. n)
            t.cheat("::give " .. TASK[item] .. " " .. n)
            t.ticks(1)
            converse(SEC, { "I have to go!" }, "ore" .. i .. ".handin")
        end
        t.check("points.secretary", points() == 75, "points " .. points())

        for i = 1, 6 do
            if points() >= 100 then break end
            converse(DIR, { "I'll take it.", "Do you have any more tasks for me?" }, "bar" .. i)
            local item, n = task()
            t.check("bar" .. i .. ".task", item > 10 and n >= 3, "item " .. item .. " x" .. n)
            t.cheat("::give " .. TASK[item] .. " " .. n)
            t.ticks(1)
            converse(DIR, { "Thank you for your time." }, "bar" .. i .. ".handin")
        end
        t.check("points.director", points() >= 100, "points " .. points())

        converse(DIR, { "I'd like to officially join your company.", "Thank you for your time." }, "join")
        for _ = 1, 3 do
            if t.chat.kind() == "none" then break end
            t.chat.choose("Thank you for your time.")
            t.ticks(2)
        end
        local _, co = t.var.server("varb578_giantdwarf_current_company")
        t.check("joined", co == 4, "company " .. tostring(co))

        converse(DIR, { "Yes! Long live the Green Gemstone!", "I would support you.", "Blasidar the sculptor has sent me." }, "pledge")
        stage("pledged", 40)

        -- Veldaban and the meeting, index 4501 [728..1099].
        t.check("veldaban.tele", t.cheat("::tele 0_44_159_11_38") == "ok", "the Black Guard's headquarters")
        t.ticks(3)
        converse("dwarf_city_black_guard_leader", { "I want to see the meeting.", "I'm ready." }, "meeting")
        local final
        for i = 1, 420 do
            local _, v = t.var.server("varb571_giantdwarf_quest")
            final = v
            if v == 50 then break end
            t.ticks(1)
        end
        t.check("stage.complete", final == 50, "stage " .. tostring(final))
        t.ticks(3)
        t.check("notes", select(2, t.inv.count("dwarf_meeting_notes1")) == 1, "meeting notes in the pack")
    end,
}
