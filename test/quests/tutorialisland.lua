-- Tutorial Island -- the quest Learning the Ropes, as the October 2025
-- tutorial plays it -- end to end by a character on its first login.
--
-- Graded against the OSRS Wiki, not the content: every npc line, choice and
-- box text checked below is a fragment of Transcript:Learning_the_Ropes
-- (pinned in docs/areas/tutorial/sources/), and the step order is the wiki's.
-- The step NUMBERS are Jagex's, read off RSProx (index 4289, a whole rev-241
-- run, 2026-10-20; docs/rsprox-analysis/tutorial/README.md). Before that they
-- were this port's own, held only to the three the cache fixes (200, 400, 671).
-- Content: server/scripts/tutorial/.
--
-- Start: fresh_tutorial.ini, a save with no varps standing in the Gielinor
-- Guide's house. The design panel, then "How familiar are you with Old School
-- RuneScape?" -- answered "I'm brand new!" -- then every instructor in turn.
-- Every side tab is opened by clicking its flashing stone (the client sends
-- IF_BUTTON1 on it); every item comes from the instructor or skill that
-- hands it over; every door is clicked and every ladder climbed. Wind Strike
-- on a chicken completes the quest; Home Teleport leaves the island with the
-- wiki's reward kit.
--
-- Spawns: areas/world/configs/m48_48.spawn, m48_148.spawn, m49_48.spawn.
return {
    id = "tutorialisland",
    fixture = "fresh_tutorial.ini",
    setup = {},

    run = function(t)
        local VARP = "varp281_tutorial"
        local FLASH = "varb3756_flashside"
        local S = {
            designed_character = 1, talk_to_guide = 2, open_settings = 3, opened_settings = 7,
            guide_door = 10,
            talk_to_survival = 20, open_inventory = 30, fish_shrimp = 40, open_skills = 50,
            opened_skills = 60, cut_tree = 70, light_fire = 80, cook_shrimp = 90, survival_gate = 120,
            chef_door = 130, talk_to_chef = 140, make_dough = 150, bake_bread = 160, chef_exit = 170,
            run_hint = 200, run_toggled = 210,
            talk_to_quest_guide = 220, open_quest_list = 230, opened_quest_list = 240, quest_ladder = 250,
            talk_to_miner = 260, mine_first = 300, mine_second = 310, smelt = 320, bar_made = 330,
            smith_dagger = 340, smithing_menu = 350, mining_gate = 360,
            talk_to_vannaka = 370, open_worn = 390, open_equipment_stats = 400, equip_dagger = 405,
            dagger_equipped = 410, equip_sword_shield = 420, open_combat = 430, enter_pen = 440,
            attack_rat = 450, melee_rat_killed = 470, range_rat = 480, ranged_rat_killed = 500,
            open_bank = 510, use_poll_booth = 520, account_door = 525, talk_to_account_guide = 530,
            open_account = 531, opened_account = 532, account_exit = 540,
            talk_to_brace = 550, open_prayer = 560, opened_prayer = 570, chapel_exit = 610,
            talk_to_terrova = 620, open_magic = 630, opened_magic = 640, cast_wind_strike = 650,
            quest_complete = 671, to_the_mainland = 680, complete = 1000,
        }

        local function stage(name, ticks)
            t.exec("quest.stage." .. name, t.var.await, VARP, S[name], ticks or 10)
        end
        local function here()
            local r, tl = t.world.tile()
            return r == "ok" and tl or nil
        end
        local function tile_str()
            local tl = here()
            if not tl then
                return "nil"
            end
            return tl.x .. "," .. tl.z .. "," .. tl.level
        end
        local function on_tile(name, x, z, level, slack)
            slack = slack or 0
            local tl = here()
            t.check(name, tl ~= nil and math.abs(tl.x - x) <= slack and math.abs(tl.z - z) <= slack
                and tl.level == level, "at " .. tile_str() .. ", want " .. x .. "," .. z .. "," .. level
                .. (slack > 0 and (" +-" .. slack) or ""))
        end
        -- A tab the instructor points at: its stone is flashing
        -- (%varb3756_flashside is the tab number plus one), and the player
        -- clicks it -- the stone's IF_BUTTON1 is what moves the step on.
        local function open_tab(name, tab, tab_number, next_stage)
            t.exec(name .. ".flash", t.var.await, FLASH, tab_number + 1, 5)
            local tab_result, tab_detail = t.ui.tab(tab)
            t.check(name, tab_result, "ui.tab(" .. tab .. "): " .. tostring(tab_result) .. " " .. tostring(tab_detail))
            stage(next_stage, 5)
            t.exec(name .. ".unflash", t.var.await, FLASH, 0, 5)
        end
        local function talk(name, npc, lines, opts)
            t.exec(name, t.player.talk_to, npc, 1, opts)
            t.exec(name .. "-dialog", t.chat.play, lines)
            t.ticks(1)
        end
        -- XP rows: the delta since `snap`, against the content's own amounts
        -- (tutorial_give_xp's tenths), which are the OSRS wiki's.
        local snap
        local function snapshot()
            local _, sn = t.skill.snapshot()
            snap = sn
        end
        local function gain(name, skill, xp)
            t.exec(name, t.skill.expect_gain, skill, xp, snap)
        end
        -- A fire is lit some ticks after "You attempt to light the logs.":
        -- ~light_logs_inv re-tries every 4 ticks (p_opobj(4)) at about one in
        -- four for a level-1 player (stat_random 64..512 -> 65/256). Seed tutA
        -- misses 31 times in a row (127 ticks); the stream was checked and is
        -- sound (25.4% over 2e8 draws, long runs at the geometric rate, no
        -- stride bias), so that is a real 1-in-several-thousand streak, and the
        -- wait is 400.
        local function fire_lit(name)
            local where = "none"
            t.exec(name, t.await, {
                level = function()
                    local r, f = t.world.loc_near("fire", 2)
                    if r == "ok" then
                        where = f.tile_x .. "," .. f.tile_z
                    end
                    return r == "ok"
                end,
                note = "a fire beside the player",
            }, 400)
            t.note("fire at " .. where)
        end
        -- Kill one giant rat (3 hitpoints: about 25 ticks). A press can be
        -- refused ("I can't reach that!") for a rat the pen's torches and
        -- standards hide, and a ranged fight stalls when the rat walks behind
        -- one (the approach finds no tile with line of sight and waits) -- a
        -- player clicks another rat, and so does this: a refused or stalled
        -- rat is noted on the next row and the next copy is tried. `home` is
        -- the tile to shoot from, walked back to before every press.
        -- `stage_name` is the step the kill's [ai_queue3] writes
        -- (~tut_rat_credit_kill).
        local function kill_rat(name, stage_name, home)
            local refused = {}
            local last_pick = nil
            for attempt = 1, 6 do
                if type(home) == "function" and attempt > 1 then
                    -- `home` answers true when it had to bring the player
                    -- back: a stall from out there says nothing about the rat.
                    if home(attempt) and last_pick ~= nil then
                        refused[last_pick] = nil
                    end
                elseif home and attempt > 1 then
                    t.player.walk_to(home[1], home[2], 10)
                end
                local n = name .. (attempt > 1 and attempt or "")
                -- The nearest copy no press has been refused on yet.
                local _, _, rows = t.npc.tiles("newbiegiantrat", 20)
                local pick = nil
                for _, row in ipairs(rows or {}) do
                    if pick == nil and not refused[row.slot] then
                        pick = row.slot
                    end
                end
                last_pick = pick
                local ar, ad = t.player.attack("newbiegiantrat", 2, nil, { slot = pick })
                if ar == "refused" and attempt < 6 then
                    -- A rat behind the pen's fence: a player clicks another.
                    t.note(n .. " refused from " .. tile_str() .. ", next rat: " .. tostring(ad))
                else
                    t.check(n, ar, tostring(ad))
                end
                if ar == "ok" then
                    local kr, kd = t.npc.await_dead_engaged(100)
                    if kr == "ok" or attempt == 6 then
                        t.check(n .. ".kill", kr, tostring(kd))
                        break
                    end
                    t.note(n .. " stalled at " .. tile_str() .. ", next rat: " .. tostring(kd))
                    if pick ~= nil then
                        refused[pick] = true
                    end
                elseif pick ~= nil then
                    refused[pick] = true
                end
                t.ticks(2)
            end
            stage(stage_name, 5)
        end
        -- One Wind Strike on a chicken, hit or splash: either completes the
        -- quest. The pen's fence hides some copies ("I can't reach that!"),
        -- so a refused cast moves on to the next copy.
        local function cast_chicken(name, stage_name)
            -- From the bay south of the railing (blockrange=0): the pen's
            -- diagonal corners hide it from the instructor's own tile, and a
            -- cast pressed from there routes round the brick wall instead.
            t.exec("walk-" .. name, t.player.walk_to, 3140, 3091, 10)
            local refused = {}
            for attempt = 1, 4 do
                local n = name .. (attempt > 1 and attempt or "")
                local _, _, rows = t.npc.tiles("newbiechicken", 20)
                local pick = nil
                for _, row in ipairs(rows or {}) do
                    if pick == nil and not refused[row.slot] then
                        pick = row.slot
                    end
                end
                local cr, cd = t.player.cast("wind_strike", "newbiechicken", 12, nil, { slot = pick })
                if cr == "refused" and attempt < 4 then
                    t.note(n .. " refused, next chicken: " .. tostring(cd))
                else
                    t.check(n, cr, tostring(cd))
                end
                if cr == "ok" then
                    break
                end
                if pick ~= nil then
                    refused[pick] = true
                end
                t.ticks(2)
            end
            stage(stage_name)
        end
        local function loc(sym)
            return t.player.by_symbol("loc", sym)
        end
        -- Every tutorial door and gate is the reference's: pressed, its leaf
        -- swings for three ticks and the player is put through
        -- (~tut_walk_through), so pass_door grades it on the leaf leaving its
        -- tile and the player standing past it.
        local function door(name, sym, at, near, far)
            t.exec(name, t.player.pass_door, {
                closed = sym, at = { at[1], at[2], at[3] or 0 }, near = near, far = far,
            })
        end
        local function closed_check(name, interface)
            local r = t.ui.await_close(interface, 5)
            t.check(name, r, interface .. " closed: " .. tostring(r))
        end

        ---------------------------------------------------------------
        -- The Gielinor Guide's house
        ---------------------------------------------------------------
        on_tile("login.tile", 3094, 3106, 0)
        stage("designed_character")
        t.exec("design.open", t.ui.await_open, "player_design", 5)
        local cr, confirm = t.ui.widget("player_design:confirm")
        t.check("design.confirm", t.ui.invoke(confirm, 1), "player_design:confirm " .. tostring(cr))
        closed_check("design.closed", "player_design")
        t.exec("experience.open", t.ui.await_open, "tutorial_player_experience", 5)
        -- Walked away from instead of answered: the question is closed with
        -- no answer and the step still 0. The Gielinor Guide -- the only way
        -- out of the house -- asks it again rather than saying nothing.
        t.exec("experience.walkAway", t.player.walk_to, 3095, 3106, 5)
        closed_check("experience.dismissed", "tutorial_player_experience")
        t.exec("experience.unanswered", t.var.await, VARP, 0, 3)
        local gr, gd = t.player.talk_to("newbie_basics_instructor", 1)
        t.note("talk to the guide with no answer: " .. tostring(gr) .. " " .. tostring(gd))
        t.exec("experience.reopened", t.ui.await_open, "tutorial_player_experience", 5)
        local er, brand_new = t.ui.widget("tutorial_player_experience:button_new")
        t.check("experience.new", t.ui.invoke(brand_new, 1), "button_new " .. tostring(er))
        closed_check("experience.closed", "tutorial_player_experience")
        stage("talk_to_guide")
        t.exec("overlay", t.ui.await_open, "tutorial_overlay", 5)

        talk("talkToGuide", "newbie_basics_instructor", {
            "npc:I see you are a new arrival to the world of Gielinor",
            "npc:talking to other people!",
            "npc:By clicking on them you can talk to them",
            "npc:let's start by looking at your settings menu",
        })
        stage("open_settings")
        open_tab("settingsTab", "options", 11, "opened_settings")
        talk("talkToGuide2", "newbie_basics_instructor", {
            "npc:Looks like you're making good progress!",
            "npc:time for you to go and meet your first instructor!",
        })
        stage("guide_door")
        door("openGuideDoor", "newbie_door1", { 3098, 3107 }, { 3097, 3107 }, { 3098, 3107 })
        stage("talk_to_survival")

        ---------------------------------------------------------------
        -- The Survival Expert
        ---------------------------------------------------------------
        t.exec("walk-survival", t.player.walk_to, 3102, 3096, 40)
        talk("talkToSurvival", "newbie_survival_instructor", {
            "npc:My name is Brynna",
            "npc:The first skill we're going to look at is Fishing",
            "*",
        })
        stage("open_inventory")
        t.exec("talkToSurvival.net", t.inv.await, "net", 1, 5)
        open_tab("inventoryTab", "inventory", 3, "fish_shrimp")
        snapshot()
        t.exec("catchShrimp", t.player.talk_to, "0_48_48_newbiefishing", 1)
        t.exec("catchShrimp.shrimp", t.inv.await, "raw_shrimp", 1, 20)
        -- Walked off, not continued: the catch moves the step on, not the box.
        -- With the step written after the pausing box, a player who walked
        -- away held the shrimp and Brynna still said "Let's try and catch
        -- some."
        local _, shrimp_box = t.chat.text()
        t.check("catchShrimp.box", t.chat.kind() == "objbox"
            and string.find(tostring(shrimp_box), "You manage to catch some shrimp", 1, true) ~= nil,
            "chat " .. tostring(t.chat.kind()) .. " '" .. tostring(shrimp_box) .. "'")
        t.exec("catchShrimp.walkOff", t.player.walk_to, 3102, 3096, 10)
        t.check("catchShrimp.boxClosed", t.chat.kind() ~= "objbox", "chat " .. tostring(t.chat.kind()))
        gain("catchShrimp.xp", "fishing", 10)
        stage("open_skills")
        open_tab("skillsTab", "stats", 1, "opened_skills")
        talk("talkToSurvival2", "newbie_survival_instructor", {
            "player:I've managed to catch some shrimp.",
            "npc:This brings us on to the Woodcutting and Firemaking skills",
            "*",
        })
        stage("cut_tree")
        t.exec("talkToSurvival2.items", t.inv.await_all, { bronze_axe = 1, tinderbox = 1 }, 5)

        snapshot()
        t.exec("chopTree", t.player.click_loc, "newbietree", 1)
        t.exec("chopTree.logs", t.inv.await, "logs", 1, 15)
        t.exec("chopTree.box", t.chat.play, { "*" })
        stage("light_fire")
        gain("chopTree.xp", "woodcutting", 25)
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        fire_lit("lightFire.fire")
        gain("lightFire.xp", "firemaking", 40)
        stage("cook_shrimp")

        -- Cooked or burnt, the first shrimp moves the player on (the
        -- transcript has a "Moving on" box for each).
        snapshot()
        t.exec("cookShrimp", t.player.use_on, "raw_shrimp", loc("fire"))
        t.exec("cookShrimp.done", t.inv.expect_absent, "raw_shrimp")
        local _, have = t.inv.count("shrimp")
        local _, burnt = t.inv.count("burntfish1")
        t.check("cookShrimp.result", (have or 0) + (burnt or 0) == 1,
            "shrimp " .. tostring(have) .. ", burnt shrimp (burntfish1) " .. tostring(burnt))
        if (have or 0) > 0 then
            gain("cookShrimp.xp", "cooking", 30)
        end
        stage("survival_gate")

        ---------------------------------------------------------------
        -- The Master Chef
        ---------------------------------------------------------------
        door("openChefGate", "newbiegateclosedl2", { 3089, 3092 }, { 3090, 3092 }, { 3089, 3092 })
        stage("chef_door")
        door("openChefDoor", "newbie_door2", { 3079, 3084 }, { 3080, 3084 }, { 3078, 3084 })
        stage("talk_to_chef")
        talk("talkToChef", "newbie_cook_instructor", {
            "npc:I am the Master Chef, Lev",
            "player:I already know how to cook. Brynna taught me just now.",
            "npc:You call THAT cooking?",
            "*",
        })
        stage("make_dough")
        t.exec("talkToChef.items", t.inv.await_all, { newbie_pot_flour = 1, bucket_water = 1 }, 5)
        t.exec("makeDough", t.player.use_item_on_item, "bucket_water", "newbie_pot_flour")
        t.exec("makeDough.dough", t.inv.await, "bread_dough", 1, 5)
        t.exec("makeDough.box", t.chat.play, { "*" })
        stage("bake_bread")
        snapshot()
        -- "To do so, just click on the indicated range."
        t.exec("bakeBread", t.player.click_loc, "newbierange", 1)
        t.exec("bakeBread.bread", t.inv.await, "bread", 1, 10)
        t.exec("bakeBread.box", t.chat.play, { "*" })
        gain("bakeBread.xp", "cooking", 40)
        stage("chef_exit")
        door("leaveChef", "newbie_door3", { 3072, 3090 }, { 3073, 3090 }, { 3072, 3090 })
        stage("run_hint")

        local rr, run = t.ui.widget("orbs:runbutton")
        t.check("toggleRun", t.ui.invoke(run, 1), "orbs:runbutton " .. tostring(rr))
        t.exec("toggleRun.on", t.var.await, "varp173_option_run", 1, 5)
        stage("run_toggled")

        ---------------------------------------------------------------
        -- The Quest Guide
        ---------------------------------------------------------------
        door("openQuestDoor", "newbie_door4", { 3086, 3126 }, { 3086, 3127 }, { 3086, 3125 })
        stage("talk_to_quest_guide")
        talk("talkToQuestGuide", "newbie_quest_instructor", {
            "npc:I'm here to tell you all about quests",
        })
        stage("open_quest_list")
        -- The tab is handed over on the quest list, never the account summary
        -- (RSProx index 4289 [509]: `questlist` (399) into side_journal:tab_container).
        t.expect("questTab.questlist", t.ui.await_open("questlist", 5))
        t.expect("questTab.no_summary", t.ui.await_close("account_summary_sidepanel", 1))
        open_tab("questTab", "quests", 2, "opened_quest_list")
        talk("talkToQuestGuide2", "newbie_quest_instructor", {
            "npc:Now that you have the quest list open",
            "npc:If you haven't started the quest",
            "npc:Just look out for the quest icon on your minimap",
            "objbox:Look for the icon to the left to find quest start points",
            "npc:collecting beads to hunting down dragons",
            "npc:You have to experience the thrill of it yourself",
        })
        stage("quest_ladder")
        t.player.climb({ loc = "newbieladdertop1", at = { 3088, 3119, 0 }, dest = { 3088, 9519, 0 }, slack = 2 })
        stage("talk_to_miner")

        ---------------------------------------------------------------
        -- The Mining Instructor
        ---------------------------------------------------------------
        t.exec("walk-mining", t.player.walk_to, 3080, 9505, 40)
        talk("talkToMining", "newbie_mining_instructor", {
            "npc:So what do I call you?",
            "player:You can call me",
            "npc:My name is Dezzick and I'm a miner by trade",
            "npc:all you need is a pickaxe",
            "*",
        })
        stage("mine_first")
        t.exec("talkToMining.pickaxe", t.inv.await, "bronze_pickaxe", 1, 5)
        snapshot()
        t.exec("mineTin", t.player.click_loc, "newbietinrock", 1)
        t.exec("mineTin.ore", t.inv.await, "tin_ore", 1, 15)
        -- An objectbox with the ore, not a mesbox (RSProx index 4289 [595]).
        local tin_kind = t.chat.kind()
        t.check("mineTin.objbox", tin_kind == "objbox", "chat.kind() -> " .. tostring(tin_kind))
        t.exec("mineTin.box", t.chat.play, { "*" })
        stage("mine_second")
        t.exec("mineCopper", t.player.click_loc, "newbiecopperrock", 1)
        t.exec("mineCopper.ore", t.inv.await, "copper_ore", 1, 15)
        local copper_kind = t.chat.kind()
        t.check("mineCopper.objbox", copper_kind == "objbox", "chat.kind() -> " .. tostring(copper_kind))
        t.exec("mineCopper.box", t.chat.play, { "*" })
        stage("smelt")
        gain("mineOres.xp", "mining", 35)
        snapshot()
        -- "To do so, just click on the indicated furnace."
        -- The furnace answers with no dialogue at all (RSProx index 4289
        -- [613..622]): the swing, then "You smelt..." and "You retrieve a bar of
        -- bronze." in the step box's overlay, which the click settle has no arm
        -- for. Its silence is not a failure; the bar is the evidence.
        local smelt_r, smelt_d = t.player.click_loc("newbiefurnace", 1)
        t.check("smeltBronze", smelt_r == "ok" or smelt_r == "timeout",
            "click_loc newbiefurnace: " .. tostring(smelt_r) .. " " .. tostring(smelt_d))
        t.exec("smeltBronze.bar", t.inv.await, "bronze_bar", 1, 10)
        -- 6.2 xp; the client's stat reading is whole xp, so it reads +6.
        gain("smeltBronze.xp", "smithing", 6)
        stage("bar_made")
        talk("talkToMining2", "newbie_mining_instructor", {
            "player:I have a bronze bar. What now?",
            "npc:To smith something, you need a hammer and an anvil",
            "*",
        })
        stage("smith_dagger")
        t.exec("talkToMining2.hammer", t.inv.await, "hammer", 1, 5)
        -- The anvil's answer is interface 312 `smithing` (RSProx index 4289
        -- [652]), which the press's settle does not count as a page; the
        -- modal and its dagger cell grade it.
        snapshot()
        local ar, ad = t.player.use_on("bronze_bar", loc("anvil"))
        t.note("use bronze_bar on anvil: " .. tostring(ar) .. " " .. tostring(ad))
        t.exec("smithDagger", t.ui.await_open, "smithing", 10)
        local da_r, da = t.ui.widget("smithing:dagger")
        t.check("smithDagger.dagger", t.ui.invoke(da, 1), "smithing:dagger " .. tostring(da_r))
        closed_check("smithDagger.closed", "smithing")
        t.exec("smithDagger.made", t.inv.await, "bronze_dagger", 1, 10)
        -- 12.5 xp: 6.2 -> 18.7, which the whole-xp reading shows as 6 -> 18.
        gain("smithDagger.xp", "smithing", 12)
        stage("mining_gate")
        t.exec("walk-combatGate", t.player.walk_to, 3092, 9503, 30)
        door("openCombatGate", "newbiedoor4l", { 3094, 9503 }, { 3093, 9503 }, { 3095, 9503 })
        stage("talk_to_vannaka")

        ---------------------------------------------------------------
        -- The Combat Instructor
        ---------------------------------------------------------------
        t.exec("walk-combat", t.player.walk_to, 3105, 9508, 30)
        talk("talkToCombat", "newbie_combat_instructor", {
            "player:Hi! My name's",
            "npc:Do I look like I care?",
            "npc:I am Vannaka, the greatest swordsman alive",
            "npc:Let's get started by teaching you to wield a weapon.",
        })
        stage("open_worn")
        open_tab("wornTab", "equipment", 4, "open_equipment_stats")
        -- The cache's own flash on the stats button (equipment_icon_flash at
        -- exactly 400), then the click that opens the stats window.
        local sr, stats = t.ui.widget("wornitems:equipment")
        t.check("equipmentStats", t.ui.invoke(stats, 1), "wornitems:equipment " .. tostring(sr))
        t.exec("equipmentStats.open", t.ui.await_open, "equipment", 5)
        stage("equip_dagger")
        t.exec("equipDagger", t.player.equip, "bronze_dagger")
        stage("dagger_equipped")
        talk("talkToCombat2", "newbie_combat_instructor", {
            "npc:that little butter knife isn't going to protect you much",
            "*",
        })
        stage("equip_sword_shield")
        t.exec("talkToCombat2.items", t.inv.await_all, { bronze_sword = 1, wooden_shield = 1 }, 5)
        t.exec("equipSword", t.player.equip, "bronze_sword")
        t.exec("equipShield", t.player.equip, "wooden_shield")
        stage("open_combat")
        open_tab("combatTab", "combat", 0, "enter_pen")
        door("enterRatPen", "newbiedoor5_l", { 3111, 9518 }, { 3112, 9518 }, { 3110, 9518 })
        stage("attack_rat")
        -- A rat that walks onto the player's tile makes the player step out
        -- from under it (as OSRS does), and while the gate stands open (three
        -- ticks, ^tut_door_open_ticks) east is the doorway: the gate shuts with
        -- the player outside. A player clicks the gate again, which at this
        -- step lets them back in; so does this, before the next press.
        kill_rat("attackRat", "melee_rat_killed", function(attempt)
            local tl = here()
            if tl and tl.x >= 3111 then
                door("reenterRatPen" .. attempt, "newbiedoor5_l", { 3111, 9518 }, { 3112, 9518 }, { 3110, 9518 })
                return true
            end
            return false
        end)
        door("leaveRatPen", "newbiedoor5_l", { 3111, 9518 }, { 3110, 9518 }, { 3111, 9518 })
        t.exec("walk-combat2", t.player.walk_to, 3105, 9508, 30)
        talk("talkToCombat3", "newbie_combat_instructor", {
            "player:I did it! I killed a giant rat!",
            "npc:Now that you have grasped basic swordplay",
            "npc:Next up we have ranged combat",
            "*",
        })
        stage("range_rat")
        t.exec("talkToCombat3.items", t.inv.await_all, { shortbow = 1, bronze_arrow = 50 }, 5)
        t.exec("equipBow", t.player.equip, "shortbow")
        t.exec("equipArrows", t.player.equip, "bronze_arrow")
        -- From beside the pen gate, outside: the whole pen is in view there.
        t.exec("walk-penSide", t.player.walk_to, 3112, 9516, 30)
        local _, ranged_before = t.skill.read("ranged")
        kill_rat("rangeRat", "ranged_rat_killed", { 3112, 9516 })
        local _, ranged_after = t.skill.read("ranged")
        local _, arrows_left = t.ui._worn_count("bronze_arrow")
        t.check("rangeRat.ranged", ranged_after.experience > ranged_before.experience and arrows_left < 50,
            "ranged xp " .. ranged_before.experience .. " -> " .. ranged_after.experience
            .. ", arrows worn 50 -> " .. tostring(arrows_left) .. " (from outside the pen)")
        t.player.climb({ loc = "newbieladder2", at = { 3111, 9526, 0 }, dest = { 3111, 3125, 0 }, slack = 2 })
        stage("open_bank")

        ---------------------------------------------------------------
        -- The bank, the poll booth and the Account Guide
        ---------------------------------------------------------------
        -- "To open your bank, just click on the indicated booth."
        t.exec("openBank", t.player.click_loc, "newbiebankbooth", 1)
        stage("use_poll_booth")
        t.exec("bank.coins", t.bank.count, "coins")
        t.check("bank.close", t.bank.close(), "closed the bank")
        t.exec("pollBooth", t.player.click_loc, "pollbooth_green", 1)
        t.exec("pollBooth.boxes", t.chat.play, {
            "mesbox:Poll booths are found in towns across the world",
            "mesbox:Voting is open to",
            "mesbox:A flag appears on the booth",
        })
        stage("account_door")
        door("openAccountDoor", "newbie_door6", { 3125, 3124 }, { 3124, 3124 }, { 3125, 3124 })
        stage("talk_to_account_guide")
        talk("talkToAccountGuide", "newbie_account_instructor", {
            "player:Hello. Who are you?",
            "npc:I'm the Account Guide",
            "player:What's an account?",
            "npc:Your character IS your account!",
            "npc:Let's take a look at it now.",
        })
        stage("open_account")
        open_tab("accountTab", "account", 8, "opened_account")
        talk("talkToAccountGuide2", "newbie_account_instructor", {
            "npc:there's a few things you can do with your account",
            "npc:First, you have membership",
            "player:What's a world?",
            "npc:A world is what you play the game on",
            "npc:You are currently on a",
            "npc:Bonds are another thing",
            "player:But why would I use a Bond",
            "npc:Unlike membership, Bonds can be traded",
            "player:without spending any real money?",
            "npc:Exactly!",
            "npc:you have your inbox and name changer",
            "npc:The name changer allows you",
            "player:Is there anything else I should know about accounts?",
            "npc:Bank PINs and two-factor authentication",
            "npc:Customer Support webpage",
            "npc:that's everything I have to tell you about accounts",
        })
        stage("account_exit")
        door("leaveAccountGuide", "newbie_door7", { 3130, 3124 }, { 3129, 3124 }, { 3130, 3124 })
        stage("talk_to_brace")

        ---------------------------------------------------------------
        -- Brother Brace
        ---------------------------------------------------------------
        t.exec("walk-chapel", t.player.walk_to, 3124, 3107, 40)
        talk("talkToBrace", "brother_noob", {
            "player:Good day, brother.",
            "npc:I'm here to tell you all about Prayer",
        })
        stage("open_prayer")
        open_tab("prayerTab", "prayer", 5, "opened_prayer")
        talk("talkToBrace2", "brother_noob", {
            "npc:This is your Prayer list",
            "npc:recharge by finding an altar",
            "npc:Burying bones",
            "npc:That's all I have to teach you for now",
        })
        stage("chapel_exit")
        door("leaveChapel", "newbie_door8", { 3122, 3102 }, { 3122, 3103 }, { 3122, 3102 })
        stage("talk_to_terrova")

        ---------------------------------------------------------------
        -- The Magic Instructor
        ---------------------------------------------------------------
        t.exec("walk-magic", t.player.walk_to, 3140, 3089, 60)
        talk("talkToMagic", "newbie_magic_instructor", {
            "player:Hello.",
            "npc:My name is Terrova",
        })
        stage("open_magic")
        open_tab("magicTab", "magic", 6, "opened_magic")
        talk("talkToMagic2", "newbie_magic_instructor", {
            "npc:you can only cast one offensive spell called Wind Strike",
            "*",
        })
        stage("cast_wind_strike")
        t.exec("talkToMagic2.runes", t.inv.await_all, { airrune = 5, mindrune = 5 }, 5)
        local _, cast_tick = t.tick()
        cast_chicken("castWindStrike", "quest_complete")
        t.exec("questComplete", t.ui.await_open, "questscroll", 10)
        t.exec("questComplete.title", t.ui.expect_text, "questscroll:quest_title", "Learning the Ropes")
        local qr, qclose = t.ui.widget("questscroll:close_button")
        t.check("questComplete.close", t.ui.invoke(qclose, 1), "questscroll:close_button " .. tostring(qr))
        closed_check("questComplete.closed", "questscroll")
        -- The cast is the chicken's alone. Completing the quest hints Terrova
        -- with an npc_find; run before the hit resolved, that repointed the
        -- active npc and the instructor took the splat and blocked.
        local tr, terrova = t.npc.state("newbie_magic_instructor")
        t.check("castWindStrike.terrovaUntouched",
            (tr == "ok" and terrova.health_ratio < 0 and terrova.seq_tick < cast_tick) and "ok" or "hollow",
            tr == "ok" and t.npc.state_text(terrova) or tostring(terrova))
        -- RSProx index 4289 [1253..1335].
        talk("talkToMagic3", "newbie_magic_instructor", {
            "npc:I'll give you a reasonable number of runes when you leave",
            "choose:Yes.",
            "npc:talk to the Ironman tutor first",
            "choose:Yes, I'd like to go to the mainland",
            "player:Yes, I'd like to go to the mainland",
            "npc:talk to my friend Adventurer Jon",
            "npc:a huge sword",
            "*",
            "npc:use your home teleport spell to teleport to Lumbridge!",
        })
        stage("to_the_mainland")

        ---------------------------------------------------------------
        -- Graduation
        ---------------------------------------------------------------
        t.exec("homeTeleport", t.player.cast, "teleport_home_standard", nil, 12)
        stage("complete")
        on_tile("homeTeleport.lumbridge", 3222, 3218, 0, 2)
        local KIT = {
            bronze_axe = 1, tinderbox = 1, net = 1, shrimp = 1, bucket_empty = 1, pot_empty = 1,
            bread = 1, bronze_pickaxe = 1, bronze_dagger = 1, bronze_sword = 1, wooden_shield = 1,
            shortbow = 1, bronze_arrow = 25, airrune = 25, mindrune = 15, waterrune = 6,
            earthrune = 4, bodyrune = 2,
        }
        t.exec("graduation.kit", t.inv.await_all, KIT, 5)
        -- The kit is the whole of what a graduate owns: nothing stays worn
        -- (the island leaves a shortbow and arrows equipped).
        for _, item in ipairs({ "shortbow", "bronze_arrow", "bronze_sword", "wooden_shield", "bronze_dagger" }) do
            local _, carried = t.inv.count(item)
            local _, worn = t.ui._worn_count(item)
            t.check("graduation.only." .. item, carried == KIT[item] and worn == 0,
                item .. ": " .. tostring(carried) .. " carried (want " .. KIT[item] .. "), "
                .. tostring(worn) .. " worn (want 0)")
        end
        closed_check("graduation.overlay", "tutorial_overlay")
        t.exec("graduation.welcome", t.chat.play, { "*" })
        t.finish(0)
    end,
}
