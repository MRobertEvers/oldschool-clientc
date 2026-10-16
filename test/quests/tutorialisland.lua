-- Tutorial Island, played end to end by a character on its first login.
-- Content: server/scripts/tutorial/ (tutorial.rs2 is the spine and the step
-- table; guides/*.rs2 the nine instructors; tutorial_doors.rs2 every door,
-- gate and ladder; tutorial_hooks.rs2 the seams the ordinary skills call).
--
-- Start: fresh_tutorial.ini, a save with no varps standing on the tutorial
-- home tile (3094,3106, the RuneScape Guide's house). The login runs
-- ~newplayer_setup, which sees the island and starts %varp281_tutorial at 0,
-- then opens the design panel; closing it asks "How familiar are you with Old
-- School RuneScape?" (interface 929). The account answers "brand new" and
-- plays every step of varp 281's ladder, 0 -> 670, then casts Home Teleport,
-- which is the graduation (~tutorial_home_teleport): Lumbridge, varp 281 =
-- 1000, and the eighteen-obj graduation kit and nothing else.
--
-- Nothing is cheated: every item comes from the instructor or skill that
-- hands it over, every door is clicked, every ladder climbed. Sidebar tabs
-- are handed over and flashed, never waited on (tutorial.rs2 section 2: rev
-- 239 cannot report a tab click), so a tab step is graded on the panel being
-- mounted and %varb3756_flashside naming it.
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
            start = 0, designed_character = 1, interact_with_scenery = 4, interacted_with_door = 10,
            cut_tree = 30, build_fire = 40, opened_skill_menu = 60, fish_shrimps = 70,
            cook_shrimps = 80, burnt_shrimps = 90, survival_complete = 120,
            opened_gate_to_chef = 130, cook_start = 140, make_dough = 150, mixed_dough = 160,
            opened_music_tab = 180, cook_completed = 190, toggled_on_run = 200,
            quest_start = 220, quest_opened_menu = 240, quest_finished = 250,
            mining_start = 260, begin_prospecting = 270, prospected_tin_first = 275,
            prospected_copper_second = 280, mining_mining_start = 290, mined_tin_first = 295,
            before_smelt = 320, after_smelt = 330, smith_a_dagger = 340, mining_finished = 350,
            combat_start = 360, worn_inventory = 380, dagger_equipped = 390,
            unequipping_items = 400, opened_combat_interface = 420, before_attacking_melee = 430,
            after_rat_kill_melee = 450, before_attacking_ranged = 460, after_attacking_ranged = 470,
            open_banking = 500, opened_bank = 510, account_door = 520, account_talked_to = 530,
            exit_account_room = 540, opened_prayer_tab = 560, after_prayer_tab = 570,
            after_friends_tab = 590, brace_finish = 600, exit_chapel = 610,
            opened_magic_tab = 630, cast_wind_strike = 640, unsuccessful_wind_strike = 650,
            successful_wind_strike = 660, magic_finished = 670, complete = 1000,
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
        -- A tab handed over: its panel mounted, and the stone flashing it.
        local function tab_given(name, panel, tab_number)
            t.exec(name .. ".panel", t.ui.await_open, panel, 5)
            t.exec(name .. ".flash", t.var.await, FLASH, tab_number + 1, 5)
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
        -- four for a level-1 player, so a long run of misses is ordinary.
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
            }, 100)
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
            for attempt = 1, 4 do
                if home and attempt > 1 then
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
                local ar, ad = t.player.attack("newbiegiantrat", 2, nil, { slot = pick })
                if ar == "refused" and attempt < 4 then
                    -- A rat behind the pen's fence: a player clicks another.
                    t.note(n .. " refused, next rat: " .. tostring(ad))
                else
                    t.check(n, ar, tostring(ad))
                end
                if ar == "ok" then
                    local kr, kd = t.npc.await_dead_engaged(100)
                    if kr == "ok" or attempt == 4 then
                        t.check(n .. ".kill", kr, tostring(kd))
                        break
                    end
                    t.note(n .. " stalled, next rat: " .. tostring(kd))
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
        -- One Wind Strike on a chicken. The pen's fence hides some copies
        -- ("I can't reach that!"), so a refused cast moves on to the next
        -- copy. The first cast is forced to splash and the second to land
        -- (~tutorial_chicken_spell_lands): each one is its own step.
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
        -- The RuneScape Guide's house
        ---------------------------------------------------------------
        on_tile("login.tile", 3094, 3106, 0)
        stage("designed_character")
        t.exec("design.open", t.ui.await_open, "player_design", 5)
        local cr, confirm = t.ui.widget("player_design:confirm")
        t.check("design.confirm", t.ui.invoke(confirm, 1), "player_design:confirm " .. tostring(cr))
        closed_check("design.closed", "player_design")
        stage("start")
        t.exec("experience.open", t.ui.await_open, "tutorial_player_experience", 5)
        local er, brand_new = t.ui.widget("tutorial_player_experience:button_new")
        t.check("experience.new", t.ui.invoke(brand_new, 1), "button_new " .. tostring(er))
        closed_check("experience.closed", "tutorial_player_experience")
        -- %varp7137_tutorial_experience is transmit=no, so no client (and no
        -- scriptrun bot) can read the answer back; what it visibly does is
        -- the grade: "brand new" keeps the tutorial at step 0 with its hint
        -- box up, where "experienced" would have been the skip.
        t.ticks(2)
        stage("start")
        t.exec("overlay", t.ui.await_open, "tutorial_overlay", 5)

        talk("talkToGuide", "newbie_basics_instructor", {
            "npc:I see you are a new arrival to this land",
            "npc:Talking to other people!",
            "npc:By clicking on them with your mouse",
            "npc:go through that door over there",
        })
        stage("interact_with_scenery")
        door("openGuideDoor", "newbie_door1", { 3098, 3107 }, { 3097, 3107 }, { 3098, 3107 })
        stage("interacted_with_door")

        ---------------------------------------------------------------
        -- The Survival Expert
        ---------------------------------------------------------------
        t.exec("walk-survival", t.player.walk_to, 3102, 3096, 40)
        talk("talkToSurvival", "newbie_survival_instructor", {
            "npc:My name is Brynna",
            "npc:making a fire",
            "*",
        })
        tab_given("inventoryTab", "inventory", 3)
        stage("cut_tree")
        t.exec("talkToSurvival.items", t.inv.await_all, { bronze_axe = 1, tinderbox = 1 }, 5)

        snapshot()
        t.exec("chopTree", t.player.click_loc, "newbietree", 1)
        t.exec("chopTree.logs", t.inv.await, "logs", 1, 15)
        t.exec("chopTree.box", t.chat.play, { "*" })
        stage("build_fire")
        gain("chopTree.xp", "woodcutting", 25)
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        fire_lit("lightFire.fire")
        gain("lightFire.xp", "firemaking", 40)
        tab_given("statsTab", "stats", 1)
        stage("opened_skill_menu")

        talk("talkToSurvival2", "newbie_survival_instructor", {
            "npc:There's nothing like a good fire",
            "npc:shrimp in the pond there",
            "*",
        })
        stage("fish_shrimps")
        t.exec("talkToSurvival2.net", t.inv.await, "net", 1, 5)
        snapshot()
        t.exec("catchShrimp", t.player.talk_to, "0_48_48_newbiefishing", 1)
        t.exec("catchShrimp.shrimp", t.inv.await, "raw_shrimp", 1, 20)
        gain("catchShrimp.xp", "fishing", 10)
        stage("cook_shrimps")

        -- Cook on the fire (it may be gone by now: light another). A burn is
        -- step 90 and the ladder goes on to 120 on the next good one.
        local cooked = false
        for attempt = 1, 6 do
            if cooked then
                break
            end
            local n = "cookShrimp" .. (attempt > 1 and attempt or "")
            if select(2, t.inv.count("raw_shrimp")) < 1 then
                t.exec(n .. ".fish", t.player.talk_to, "0_48_48_newbiefishing", 1)
                t.exec(n .. ".fished", t.inv.await, "raw_shrimp", 1, 20)
            end
            local fr, fire = t.world.loc_near("fire", 6)
            if fr ~= "ok" then
                if select(2, t.inv.count("logs")) < 1 then
                    t.exec(n .. ".chop", t.player.click_loc, "newbietree", 1)
                    t.exec(n .. ".logs", t.inv.await, "logs", 1, 15)
                    t.exec(n .. ".box", t.chat.play, { "*" })
                end
                t.exec(n .. ".light", t.player.use_item_on_item, "tinderbox", "logs")
                fire_lit(n .. ".fire")
            end
            snapshot()
            t.exec(n, t.player.use_on, "raw_shrimp", loc("fire"))
            t.exec(n .. ".done", t.inv.expect_absent, "raw_shrimp")
            local _, have = t.inv.count("shrimp")
            local _, burnt = t.inv.count("burntfish1")
            cooked = (have or 0) > 0
            t.check(n .. ".result", cooked or (burnt or 0) > 0,
                "shrimp " .. tostring(have) .. ", burnt shrimp (burntfish1) " .. tostring(burnt))
            if cooked then
                gain(n .. ".xp", "cooking", 30)
            else
                stage("burnt_shrimps")
            end
        end
        stage("survival_complete")

        ---------------------------------------------------------------
        -- The Master Chef
        ---------------------------------------------------------------
        door("openChefGate", "newbiegateclosedl2", { 3089, 3092 }, { 3090, 3092 }, { 3089, 3092 })
        stage("opened_gate_to_chef")
        door("openChefDoor", "newbie_door2", { 3079, 3084 }, { 3080, 3084 }, { 3078, 3084 })
        stage("cook_start")
        talk("talkToChef", "newbie_cook_instructor", {
            "npc:I am Lev, the chef",
            "player:I already know how to cook",
            "npc:HA HA HA",
            "npc:no fine meal is complete without good music",
            "*",
        })
        stage("make_dough")
        t.exec("talkToChef.items", t.inv.await_all, { newbie_pot_flour = 1, bucket_water = 1 }, 5)
        t.exec("makeDough", t.player.use_item_on_item, "bucket_water", "newbie_pot_flour")
        t.exec("makeDough.dough", t.inv.await, "bread_dough", 1, 5)
        stage("mixed_dough")
        snapshot()
        t.exec("bakeBread", t.player.use_on, "bread_dough", loc("newbierange"))
        t.exec("bakeBread.bread", t.inv.await, "bread", 1, 10)
        gain("bakeBread.xp", "cooking", 40)
        tab_given("musicTab", "music", 13)
        stage("opened_music_tab")
        door("leaveChef", "newbie_door3", { 3072, 3090 }, { 3073, 3090 }, { 3072, 3090 })
        stage("cook_completed")

        local rr, run = t.ui.widget("orbs:runbutton")
        t.check("toggleRun", t.ui.invoke(run, 1), "orbs:runbutton " .. tostring(rr))
        t.exec("toggleRun.on", t.var.await, "varp173_option_run", 1, 5)
        stage("toggled_on_run")

        ---------------------------------------------------------------
        -- The Quest Guide
        ---------------------------------------------------------------
        door("openQuestDoor", "newbie_door4", { 3086, 3126 }, { 3086, 3127 }, { 3086, 3125 })
        stage("quest_start")
        talk("talkToQuestGuide", "newbie_quest_instructor", {
            "npc:I'm here to tell you all about quests",
        })
        tab_given("journalTab", "side_journal", 2)
        stage("quest_opened_menu")
        talk("talkToQuestGuide2", "newbie_quest_instructor", {
            "npc:all the quests are shown in red",
            "npc:change colour to yellow",
            "npc:star icons on the minimap",
            "npc:collecting beads to hunting down dragons",
            "npc:caves under my house",
        })
        stage("quest_finished")
        t.player.climb({ loc = "newbieladdertop1", at = { 3088, 3119, 0 }, dest = { 3088, 9519, 0 }, slack = 2 })
        stage("mining_start")

        ---------------------------------------------------------------
        -- The Mining Instructor
        ---------------------------------------------------------------
        t.exec("walk-mining", t.player.walk_to, 3080, 9505, 40)
        talk("talkToMining", "newbie_mining_instructor", {
            "npc:So what do I call you?",
            "player:You can call me",
            "npc:My name is Dezzick",
        })
        stage("begin_prospecting")
        t.exec("prospectTin", t.player.click_loc, "newbietinrock", 2)
        t.exec("prospectTin.box", t.chat.play, { "mesbox:This rock contains tin." })
        stage("prospected_tin_first")
        t.exec("prospectCopper", t.player.click_loc, "newbiecopperrock", 2)
        t.exec("prospectCopper.box", t.chat.play, { "mesbox:This rock contains copper." })
        stage("prospected_copper_second")
        talk("talkToMining2", "newbie_mining_instructor", {
            "player:I prospected both types of rock!",
            "npc:smelted together to make bronze",
            "npc:you'll need this to start with",
            "*",
        })
        stage("mining_mining_start")
        t.exec("talkToMining2.pickaxe", t.inv.await, "bronze_pickaxe", 1, 5)
        snapshot()
        t.exec("mineTin", t.player.click_loc, "newbietinrock", 1)
        t.exec("mineTin.ore", t.inv.await, "tin_ore", 1, 15)
        t.exec("mineTin.box", t.chat.play, { "mesbox:You manage to mine some tin." })
        stage("mined_tin_first")
        t.exec("mineCopper", t.player.click_loc, "newbiecopperrock", 1)
        t.exec("mineCopper.ore", t.inv.await, "copper_ore", 1, 15)
        t.exec("mineCopper.box", t.chat.play, { "mesbox:You manage to mine some copper." })
        stage("before_smelt")
        gain("mineOres.xp", "mining", 35)
        snapshot()
        t.exec("smeltBronze", t.player.use_on, "tin_ore", loc("newbiefurnace"))
        t.exec("smeltBronze.bar", t.inv.await, "bronze_bar", 1, 10)
        -- 6.2 xp; the client's stat reading is whole xp, so it reads +6.
        gain("smeltBronze.xp", "smithing", 6)
        stage("after_smelt")
        talk("talkToMining3", "newbie_mining_instructor", {
            "player:How do I make a weapon out of this?",
            "npc:You'll be needing this",
            "*",
        })
        stage("smith_a_dagger")
        t.exec("talkToMining3.hammer", t.inv.await, "hammer", 1, 5)
        -- The anvil's answer is the make-menu (~skill_multi4), which the
        -- press's settle does not count as a page; the menu row grades it.
        snapshot()
        local ar, ad = t.player.use_on("bronze_bar", loc("anvil"))
        t.note("use bronze_bar on anvil: " .. tostring(ar) .. " " .. tostring(ad))
        t.exec("smithDagger", t.ui.await_open, "skillmulti", 10)
        local fa_r, fa = t.ui.widget("skillmulti:a")
        t.check("smithDagger.weapons", t.ui.invoke(fa, 1), "skillmulti:a " .. tostring(fa_r))
        t.ticks(2)
        t.exec("smithDagger.menu", t.ui.await_open, "skillmulti", 10)
        local da_r, da = t.ui.widget("skillmulti:a")
        t.check("smithDagger.dagger", t.ui.invoke(da, 1), "skillmulti:a " .. tostring(da_r))
        t.exec("smithDagger.made", t.inv.await, "bronze_dagger", 1, 10)
        -- 12.5 xp: 6.2 -> 18.7, which the whole-xp reading shows as 6 -> 18.
        gain("smithDagger.xp", "smithing", 12)
        stage("mining_finished")
        t.exec("walk-combatGate", t.player.walk_to, 3092, 9503, 30)
        door("openCombatGate", "newbiedoor4l", { 3094, 9503 }, { 3093, 9503 }, { 3095, 9503 })
        stage("combat_start")

        ---------------------------------------------------------------
        -- The Combat Instructor
        ---------------------------------------------------------------
        t.exec("walk-combat", t.player.walk_to, 3105, 9508, 30)
        talk("talkToCombat", "newbie_combat_instructor", {
            "player:Hi! My name's",
            "npc:Do I look like I care?",
            "npc:I am Vannaka",
            "npc:teaching you to wield a weapon",
        })
        tab_given("wornTab", "wornitems", 4)
        stage("worn_inventory")
        t.exec("equipDagger", t.player.equip, "bronze_dagger")
        stage("dagger_equipped")
        talk("talkToCombat2", "newbie_combat_instructor", {
            "npc:little butter knife",
            "*",
        })
        stage("unequipping_items")
        t.exec("talkToCombat2.items", t.inv.await_all, { bronze_sword = 1, wooden_shield = 1 }, 5)
        t.exec("equipSword", t.player.equip, "bronze_sword")
        t.exec("equipShield", t.player.equip, "wooden_shield")
        talk("talkToCombat3", "newbie_combat_instructor", {
            "npc:Now let's have a look at how you fight",
        })
        tab_given("combatTab", "combat_interface", 0)
        stage("opened_combat_interface")
        door("enterRatPen", "newbiedoor5_l", { 3111, 9518 }, { 3112, 9518 }, { 3110, 9518 })
        stage("before_attacking_melee")
        kill_rat("attackRat", "after_rat_kill_melee")
        door("leaveRatPen", "newbiedoor5_l", { 3111, 9518 }, { 3110, 9518 }, { 3111, 9518 })
        t.exec("walk-combat2", t.player.walk_to, 3105, 9508, 30)
        talk("talkToCombat4", "newbie_combat_instructor", {
            "player:I did it! I killed a Giant Rat!",
            "npc:basic swordplay",
            "npc:ranged attacking",
            "*",
        })
        stage("before_attacking_ranged")
        t.exec("talkToCombat4.items", t.inv.await_all, { shortbow = 1, bronze_arrow = 50 }, 5)
        t.exec("equipBow", t.player.equip, "shortbow")
        t.exec("equipArrows", t.player.equip, "bronze_arrow")
        -- From beside the pen gate, outside: the whole pen is in view there.
        t.exec("walk-penSide", t.player.walk_to, 3112, 9516, 30)
        local _, ranged_before = t.skill.read("ranged")
        kill_rat("rangeRat", "after_attacking_ranged", { 3112, 9516 })
        local _, ranged_after = t.skill.read("ranged")
        local _, arrows_left = t.ui._worn_count("bronze_arrow")
        t.check("rangeRat.ranged", ranged_after.experience > ranged_before.experience and arrows_left < 50,
            "ranged xp " .. ranged_before.experience .. " -> " .. ranged_after.experience
            .. ", arrows worn 50 -> " .. tostring(arrows_left) .. " (from outside the pen)")
        t.player.climb({ loc = "newbieladder2", at = { 3111, 9526, 0 }, dest = { 3111, 3125, 0 }, slack = 2 })
        stage("open_banking")

        ---------------------------------------------------------------
        -- The bank and the Financial Advisor
        ---------------------------------------------------------------
        talk("talkToBanker", "noobbanker", {
            "npc:would you like to access your bank account?",
            "choose:Yes.",
        })
        stage("opened_bank")
        t.exec("bank.coins", t.bank.count, "coins")
        t.check("bank.close", t.bank.close(), "closed the bank")
        door("openAdvisorDoor", "newbie_door6", { 3125, 3124 }, { 3124, 3124 }, { 3125, 3124 })
        stage("account_door")
        talk("talkToAdvisor", "newbie_account_instructor", {
            "player:Who are you?",
            "npc:I'm a financial advisor",
            "player:How can I make money then?",
            "npc:how can you make money. Quite.",
            "npc:Combat, Quests and Trading",
            "npc:drop items when they die",
            "npc:the next way to earn money quickly is by quests",
            "npc:sell them for pure profit",
            "npc:Come back if you'd like to go over this again",
        })
        stage("account_talked_to")
        door("leaveAdvisor", "newbie_door7", { 3130, 3124 }, { 3129, 3124 }, { 3130, 3124 })
        stage("exit_account_room")

        ---------------------------------------------------------------
        -- Brother Brace
        ---------------------------------------------------------------
        t.exec("walk-chapel", t.player.walk_to, 3124, 3107, 40)
        talk("talkToBrace", "brother_noob", {
            "player:Good day brother",
            "npc:I'm Brother Brace",
        })
        tab_given("prayerTab", "prayerbook", 5)
        stage("opened_prayer_tab")
        talk("talkToBrace2", "brother_noob", {
            "npc:This is your prayer list",
            "npc:recharge by finding an altar",
            "npc:Burying bones",
            "npc:community officer",
        })
        stage("after_prayer_tab")
        talk("talkToBrace3", "brother_noob", {
            "npc:Let's have a look at your friends list",
        })
        tab_given("friendsTab", "friends", 9)
        stage("after_friends_tab")
        talk("talkToBrace4", "brother_noob", {
            "npc:clicking the add button",
            "npc:ignore list",
            "npc:online status of your friends",
            "player:Are there rules on ingame behavior?",
            "npc:rules of conduct",
            "npc:real people somewhere",
            "player:I'll bear that in mind",
        })
        stage("brace_finish")
        door("leaveChapel", "newbie_door8", { 3122, 3102 }, { 3122, 3103 }, { 3122, 3102 })
        stage("exit_chapel")

        ---------------------------------------------------------------
        -- The Magic Instructor
        ---------------------------------------------------------------
        t.exec("walk-magic", t.player.walk_to, 3140, 3089, 60)
        talk("talkToMagic", "newbie_magic_instructor", {
            "player:Hello.",
            "npc:My name is Terrova",
        })
        tab_given("magicTab", "magic_spellbook", 6)
        stage("opened_magic_tab")
        talk("talkToMagic2", "newbie_magic_instructor", {
            "npc:you can only cast one offensive spell called Wind Strike",
            "*",
        })
        stage("cast_wind_strike")
        t.exec("talkToMagic2.runes", t.inv.await_all, { airrune = 5, mindrune = 5 }, 5)
        cast_chicken("castWindStrike", "unsuccessful_wind_strike")
        cast_chicken("castWindStrikeAgain", "successful_wind_strike")
        talk("talkToMagic3", "newbie_magic_instructor", {
            "npc:you're all finished here now",
            "npc:Home Teleport",
            "npc:Cast it whenever you're ready to leave",
        })
        stage("magic_finished")

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
        t.exec("graduation.welcome", t.msg.expect, "Welcome to Lumbridge!")
        t.finish(0)
    end,
}
