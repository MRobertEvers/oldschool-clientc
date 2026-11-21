-- In Aid of the Myreque, the recorded flow (RSProx index 4634, rev 242.1;
-- docs/rsprox-analysis/quests/in_aid_of_the_myreque.md). Leg 1 follows the
-- recording from Veliaf's offer to the first pile of cellar rubble scooped:
-- [146..235] Veliaf, [324..605] Florin and the food chest, [637] the gate,
-- [1135] the wall, [1328..1341] the trapdoor, [1567..1594] a pile mined and
-- scooped. Leg 2: the rest of the cellar [1567..2163] and Aurel's store
-- [2484..3345]; leg 3: the bank, Cornelius, the furnace and Castle Drakan,
-- the blood tithe and the fight [3401..4585]; leg 4: the Hollows, the trek
-- (route 1, won) and Paterdomus [4908..6476]; leg 5: the tomb, the rod and
-- the end [7065..8378]. Items come by cheat at each step (the backpack holds
-- one step's kit at a time).

-- Click through whatever boxes are up.
local function drain(t, name)
    local r, d = t.chat.drain()
    t.note(name .. ": " .. tostring(r) .. " " .. tostring(d))
end

local function tele(t, where)
    t.cheat("::tele " .. where)
    t.ticks(2)
end

return {
    id = "inaidofthemyreque",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_restlessghost",
        "::complete quest_priestinperil", -- Drezel in the mausoleum, the well
        "::complete quest_insearchofthemyreque",
        "::setlevel crafting 25",
        "::setlevel mining 41",
        "::setlevel magic 7",
        "::setlevel agility 20",
        "::give shark 2",
        "::give adamant_pickaxe 1",
        "::give spade 1",
        "::give bucket_empty 3",
    },
    bind = {
        varp = "varb1990_myreque_2_quest",
        row = "quest_inaidofthemyreque",
        constants = { not_started = 0, started = 20, met_florin = 30, food_given = 40, told_inn = 60,
            climbed_wall = 70, rubble_mined = 80, trapdoor_open = 90, in_basement = 100, basement_clear = 110,
            shop_asked = 140, roof_fixed = 150, wall_fixed = 160, crate_given = 165, store_stocked = 170,
            booth_fixed = 180, bankwall_fixed = 190, teller_recruited = 200, furnace_hinted = 205, chimney_fixed = 210,
            furnace_lit = 220, tithe_arrived = 230, tithe_fight = 240, veliaf_returned = 280, veliaf_briefed = 290,
            polmafi_told = 300, trek_started = 310, at_temple = 315, key_given = 350, library_entered = 360,
            book_read = 375, tomb_entered = 380, mould_made = 390, rod_made = 400, rod_enchanted = 410,
            rod_blessed = 420, complete = 430 },
        display = "In Aid of the Myreque",
        points = 2,
    },

    legs = {
        { name = "veliaf_to_cellar", run = function(t)
            do local _, d = t.ticklog.start(); t.note("ticklog: " .. tostring(d)) end
            t.ticklog.mark("leg.veliaf_to_cellar")
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- [146..204] the offer
            t.cheat("::tele 0_54_153_50_44")
            t.ticks(2)
            t.exec("veliaf", t.player.talk_to, "route_veliaf_hurtz_parent", 1)
            t.exec("veliaf-dialog", t.chat.play, {
                "npc:You're back. Any luck in Canifis?",
                "player:I went back to the place where I first met Vanstrom.",
                "npc:I feared as much.",
                "npc:Anyway, now that you're back,",
                "choose:Yes.",
                "player:What do you need?",
                "npc:Now that our base of operations is no longer safe,",
                "player:Seems like a wise move. What part do I play?",
                "npc:To the south of Mort'ton",
                "player:And you want me to scout the place out?",
                "npc:Exactly. Head down to Burgh de Rott",
                "player:I'll get it done.",
                "npc:Be sure to take a weapon of silver with you.",
            })
            t.expect("quest.stage.started", t.quest.expect_stage("started"))

            -- [324..534] Florin over the gate
            t.cheat("::tele 0_54_50_31_47")
            t.ticks(2)
            t.exec("florin", t.player.talk_to, "burgh_vilager_8", 1)
            t.exec("florin-dialog", t.chat.play, {
                "npc:Get out of it! You ain't comming in here",
                "player:What on earth makes you think",
                "npc:Hey! Don't try to sweet talk me.",
                "npc:What blood we've got,",
                "npc:And as for food,",
                "choose:Okay, goodbye.",
                "player:Okay, goodbye.",
            })
            t.expect("quest.stage.met_florin", t.quest.expect_stage("met_florin"))

            -- [549..561] the food in the chest
            local chest = t.player.by_symbol("loc", "burgh_quest_food_chest_open")
            t.exec("food-chest", t.player.use_on, "shark", chest)
            t.exec("food-chest-dialog", t.chat.play, {
                "objbox:You gingerly place the food into the chest.",
                "npc:Blimey, did you see that?",
            })
            t.expect("quest.stage.food_given", t.quest.expect_stage("food_given"))

            -- [565..622] the inn
            t.exec("goto-gate", t.player.goto_tile, 3486, 3245, 0)
            t.exec("florin2", t.player.talk_to, "burgh_vilager_8", 1)
            t.exec("florin2-dialog", t.chat.play, {
                "npc:Hey, what did you put in that chest?",
                "choose:Are there any 'out of the way' places here?",
                "player:Are there any 'out of the way' places here?",
                "npc:How do you mean, 'out of the way'?",
                "player:Somewhere where myself",
                "npc:Ah, right, I know what you mean.",
                "npc:Well, there's the inn.",
                "npc:The cellar itself is probably filled",
                "player:That could work. Thanks.",
                "npc:Listen, if you do decide to join us here,",
                "choose:I'd best be off.",
                "player:I'd best be off.",
            })
            t.expect("quest.stage.told_inn", t.quest.expect_stage("told_inn"))

            -- [637..641] through the gate
            t.exec("gate", t.player.click_loc, "burgh_fencegate_l", 1, { at = { 3485, 3244, 0 } })
            t.ticks(3)
            t.check("gate.inside", select(2, t.world.tile()).z <= 3243, "tile after the gate")

            -- [1127..1135] over the inn's broken wall
            t.exec("goto-wall", t.player.goto_tile, 3491, 3229, 0)
            t.exec("wall", t.player.click_loc, "burgh_inn_climb_over", 1, { at = { 3491, 3230, 0 } })
            t.ticks(4)
            t.expect("quest.stage.climbed_wall", t.quest.expect_stage("climbed_wall"))

            -- [1328..1334] the rubble on the trapdoor
            t.exec("trapdoor-rubble", t.player.click_loc, "burgh_inn_trapdoor_closed", 1, { at = { 3490, 3232, 0 } })
            t.exec("trapdoor-rubble-dialog", t.chat.play, {
                "mesbox:The trapdoor is blocked by some rubble.",
                "choose:Yes.",
            })
            t.ticks(6)
            t.expect("quest.stage.rubble_mined", t.quest.expect_stage("rubble_mined"))

            -- [1338..1341] open and down
            t.exec("trapdoor-open", t.player.click_loc, "burgh_inn_trapdoor_closed", 1, { at = { 3490, 3232, 0 } })
            t.ticks(2)
            t.expect("quest.stage.trapdoor_open", t.quest.expect_stage("trapdoor_open"))
            t.exec("trapdoor-down", t.player.click_loc, "burgh_inn_trapdoor_open", 1, { at = { 3490, 3232, 0 } })
            t.ticks(3)
            t.expect("quest.stage.in_basement", t.quest.expect_stage("in_basement"))
            t.note("cellar tile: " .. tostring(select(2, t.world.tile()).x) .. "," .. tostring(select(2, t.world.tile()).z))

            -- [1567..1594] one pile mined to pieces and scooped
            t.exec("rubble-mine", t.player.click_loc, "burgh_rubble_a_1", 1)
            t.ticks(14)
            t.exec("rubble-box", t.chat.play, { "mesbox:You manage to mine the rubble to pieces." })
            t.exec("rubble-scoop", t.player.click_loc, "burgh_rubble_a_4", 1)
            t.ticks(6)
            local _, pile = t.var.varbit("varb1972_burgh_inn_rubble_pile")
            t.check("rubble.scooped", pile == 1, "burgh_inn_rubble_pile = " .. tostring(pile))
        end },

        { name = "cellar_to_store", run = function(t)
            t.ticklog.mark("leg.cellar_to_store")
            t.cheat("::clearinv")
            t.cheat("::give adamant_pickaxe 1")
            t.cheat("::give spade 1")
            t.cheat("::give bucket_empty 5")
            -- [1594..2151] the other fourteen piles
            for n = 1, 30 do
                local _, pile = t.var.varbit("varb1972_burgh_inn_rubble_pile")
                if pile >= 15 then break end
                local r4 = t.world.loc_near("burgh_rubble_a_4", 12)
                if r4 == "ok" then
                    t.exec("scoop-" .. n, t.player.click_loc, "burgh_rubble_a_4", 1)
                    t.ticks(6)
                else
                    t.exec("mine-" .. n, t.player.click_loc, "burgh_rubble_a_1", 1)
                    t.ticks(14)
                end
                drain(t, "boxes-" .. n)
            end
            local _, pile = t.var.varbit("varb1972_burgh_inn_rubble_pile")
            t.check("rubble.all", pile == 15, "burgh_inn_rubble_pile = " .. tostring(pile))
            t.expect("quest.stage.basement_clear", t.quest.expect_stage("basement_clear"))
            t.exec("plaque", t.player.click_loc, "burgh_inn_wall_plaque", 1)
            t.exec("plaque-box", t.chat.play, { "mesbox:You read the wall plaque:" })
            t.exec("ladder-up", t.player.click_loc, "burgh_inn_basement_ladderup", 1)
            t.ticks(3)
            -- [2279..2286] a bucket into the outside pile
            t.exec("wall-out", t.player.click_loc, "burgh_inn_climb_over", 1, { at = { 3491, 3230, 0 } })
            t.ticks(4)
            local pile_loc = t.player.by_symbol("loc", "burgh_outside_rubble_pile")
            t.exec("dump", t.player.use_on, "burgh_rubble_bucket_3", pile_loc)
            t.ticks(3)

            -- [2484..2497] Aurel
            t.cheat("::clearinv")
            tele(t, "0_54_50_60_40")
            t.exec("aurel", t.player.talk_to, "burgh_general_store_owner", 1)
            t.exec("aurel-dialog", t.chat.play, {
                "player:Hey there, I'm finished with my project at the inn.",
                "npc:Oh, that's good to know.",
                "choose:I'd like to help fix up the town.",
                "player:I'd like to help fix up the town.",
                "npc:Oh, that's very kind of you!",
                "npc:Perhaps you would consider helping",
                "player:What needs fixing?",
                "npc:Please can you fix the roof, then the walls.",
                "player:I'll take a look.",
                "choose:I'd best be off.",
                "player:I'd best be off.",
            })
            t.expect("quest.stage.shop_asked", t.quest.expect_stage("shop_asked"))

            -- [2761..2793] the roof and the wall
            t.cheat("::give hammer 1")
            t.cheat("::give woodplank 6")
            t.cheat("::give nails 24")
            t.exec("ladder-roof", t.player.click_loc, "burgh_ladder_generalstore_up", 1)
            t.ticks(4)
            t.exec("roof", t.player.click_loc, "burgh_store_roof_broken", 1)
            t.exec("roof-dialog", t.chat.play, {
                "mesbox:The roof looks like it's seen better days.",
                "choose:Yes.",
                "objbox:You use some wooden planks and nails to patch up the hole in the roof.",
            })
            t.expect("quest.stage.roof_fixed", t.quest.expect_stage("roof_fixed"))
            t.exec("ladder-down", t.player.click_loc, "burgh_ladder_generalstore_down", 1)
            t.ticks(4)
            t.exec("store-wall", t.player.click_loc, "burgh_boared_up_wall_clickzone", 1)
            t.exec("store-wall-dialog", t.chat.play, {
                "mesbox:The wall has a large hole in it",
                "choose:Yes.",
                "objbox:You use some wooden planks and nails to fix up the wall.",
            })
            t.expect("quest.stage.wall_fixed", t.quest.expect_stage("wall_fixed"))

            -- [2803..2859] the crate
            t.cheat("::clearinv")
            t.exec("aurel2", t.player.talk_to, "burgh_general_store_owner", 1)
            t.exec("aurel2-dialog", t.chat.play, {
                "npc:Hi there. You've done a good job on my store!",
                "player:Yeah, but I've only fixed the roof and the walls.",
                "choose:What should I do now?",
                "player:What should I do now?",
                "npc:Well you could stock the store for us.",
                "player:What stock do you need?",
                "npc:I need 3 tinderboxes,",
                "objbox:Aurel gives you a crate.",
                "npc:There you go. You can put them in that.",
                "choose:I'd best be off.",
                "player:I'd best be off.",
            })
            t.expect("quest.stage.crate_given", t.quest.expect_stage("crate_given"))
            local _, food = t.var.varbit("varb1976_burgh_food_type")
            local food_obj = (food == 1) and "mackerel" or "snail_corpse_cooked1"
            t.cheat("::give bronze_axe 10")
            t.cheat("::give tinderbox 3")
            t.cheat("::give " .. food_obj .. " 10")
            for _, it in ipairs({ "bronze_axe", food_obj, "tinderbox" }) do
                t.exec("crate-" .. it, t.player.use_item_on_item, it, "burgh_generalstore_crate")
                t.exec("crate-" .. it .. "-dialog", t.chat.play, { "choose:Yes, I'll fill the crate." })
                drain(t, "crate-" .. it .. "-boxes")
            end
            t.exec("crate-search", t.player.inv_op, "burgh_generalstore_crate", 1)
            t.exec("crate-search-box", t.chat.play, { "mesbox:The crate is packed with stock." })

            -- [3340..3345] the hand-in
            t.exec("aurel3", t.player.talk_to, "burgh_general_store_owner", 1)
            t.exec("aurel3-dialog", t.chat.play, {
                "objbox:You show the full crate to Aurel",
                "npc:Wow, that's fantastic.",
                "mesbox:Aurel quickly puts the stock out around the shop.",
                "npc:Hey, you've done such a great job for me.",
                "player:I can probably look into it.",
                "choose:I'd best be off.",
                "player:I'd best be off.",
            })
            t.expect("quest.stage.store_stocked", t.quest.expect_stage("store_stocked"))
        end },

        { name = "bank_to_tithe", run = function(t)
            t.ticklog.mark("leg.bank_to_tithe")
            t.cheat("::clearinv")
            t.cheat("::give hammer 1")
            t.cheat("::give swamppaste 1")
            t.cheat("::give woodplank 5")
            t.cheat("::give nails 20")
            tele(t, "0_54_50_35_13")
            -- [3529..3538] the booth
            t.exec("booth", t.player.click_loc, "burgh_bankbooth_damaged", 1)
            t.exec("booth-dialog", t.chat.play, {
                "mesbox:This bank booth is in an awful condition.",
                "choose:Yes.",
                "objbox:You use some swamp paste to fix the bits of glass",
            })
            t.expect("quest.stage.booth_fixed", t.quest.expect_stage("booth_fixed"))
            -- [3590..3597] the bank's wall
            t.exec("bank-wall", t.player.click_loc, "burgh_boared_up_wall_clickzone", 1)
            t.exec("bank-wall-dialog", t.chat.play, {
                "mesbox:The wall has a large hole in it",
                "choose:Yes.",
                "objbox:You use some wooden planks and nails to fix up the wall.",
            })
            t.expect("quest.stage.bankwall_fixed", t.quest.expect_stage("bankwall_fixed"))
            -- [3611..3629] Cornelius
            t.exec("cornelius", t.player.talk_to, "burgh_potential_bank_teller_multinpc", 1)
            t.exec("cornelius-dialog", t.chat.play, {
                "npc:Hey it's you!",
                "player:I'm happy to help!",
                "choose:What should I do now?",
                "player:What should I do now?",
                "npc:Well, the bank is all fixed now,",
                "player:Do you fancy the job?",
                "npc:Me? Why, I guess I could...",
                "npc:I'd best go and get changed now...",
            })
            t.ticks(4)
            drain(t, "cornelius-boxes")
            t.expect("quest.stage.teller_recruited", t.quest.expect_stage("teller_recruited"))
            -- [3640..3645] the furnace hint
            -- [3640..3645] Cornelius, now behind his booth, from the recorder's tile.
            t.exec("goto-booth", t.player.goto_tile, 3495, 3211, 0)
            t.exec("cornelius2", t.player.talk_to, "burgh_actual_bank_teller_multinpc", 1)
            t.exec("cornelius2-dialog", t.chat.play, {
                "npc:Hello there. Thank you so much",
                "player:Thanks.",
                "choose:What should I do now?",
                "player:What should I do now?",
                "npc:You've already done quite a lot",
                "choose:I'd best be off.",
                "player:I'd best be off.",
            })
            t.expect("quest.stage.furnace_hinted", t.quest.expect_stage("furnace_hinted"))

            -- [3809..3978] the furnace
            t.cheat("::clearinv")
            t.cheat("::give hammer 1")
            t.cheat("::give steel_bar 2")
            t.cheat("::give coal 1")
            t.cheat("::give tinderbox 1")
            tele(t, "0_55_50_6_8")
            t.exec("chimney", t.player.click_loc, "burgh_furnace_broken", 1)
            t.exec("chimney-dialog", t.chat.play, {
                "mesbox:There's a large hole in the chimney",
                "choose:Yes.",
                "objbox:You use some steel bars to fix the hole in the furnace chimney.",
            })
            t.expect("quest.stage.chimney_fixed", t.quest.expect_stage("chimney_fixed"))
            t.exec("coal", t.player.click_loc, "burgh_furnace_repaired", 1)
            t.exec("coal-dialog", t.chat.play, {
                "mesbox:The furnace has been repaired now.",
                "choose:Yes.",
                "objbox:You place some coal into the newly fixed furnace.",
            })
            -- [3976..4038] Castle Drakan
            local mark = t.cutscene.mark()
            t.exec("light", t.player.click_loc, "burgh_furnace_coal_loaded", 1)
            t.exec("light-dialog", t.chat.play, {
                "mesbox:The furnace has been repaired and refueled.",
                "choose:Yes.",
            })
            t.ticks(12)
            t.exec("drakan-dialog", t.chat.play, {
                "npc:Gadderanks.",
                "npc:My lord... We've finished",
                "npc:I saw your wife today, Gadderanks.",
                "npc:I hope she stays that way.",
                "npc:My lord Vanstrom, there are precious few",
                "npc:Mort'ton is just a dead town",
                "npc:Canifis is delivering more",
                "npc:Please, where should I get the remainder from?",
                "npc:I couldn't care less for your problems",
                "npc:Wait... Do you smell that?",
                "npc:But Burgh de Rott is abandoned.",
                "npc:No. Burgh de Rott was abandoned.",
                "npc:It seems you have a fresh appointment,",
            })
            t.ticks(4)
            t.exec("gabriela", t.chat.play, { "npc:It's Gadderanks! He's found us!" })
            t.exec("cutscene:castle_drakan", t.cutscene.await, "castle_drakan", { since = mark, timeout = 10,
                expect = { { op = "moveto", height = 1900 }, { op = "lookat", height = 450 }, { op = "reset" } } })
            t.expect("quest.stage.tithe_arrived", t.quest.expect_stage("tithe_arrived"))

            -- [4070..4152] the three in the store, then the fight
            t.cheat("::clearinv")
            t.cheat("::setlevel attack 75")
            t.cheat("::setlevel strength 75")
            t.cheat("::setlevel defence 75")
            t.cheat("::setlevel hitpoints 80")
            t.cheat("::give rune_scimitar 1")
            t.cheat("::wield rune_scimitar")
            t.cheat("::give shark 10")
            tele(t, "0_54_50_58_37")
            t.exec("juve", t.player.talk_to, "burgh_juve1_multinpc", 1)
            t.exec("juve-dialog", t.chat.play, { "player:What's going on here?", "npc:We're getting some delicious blood tithes" })
            t.exec("wiskit", t.player.talk_to, "burgh_villager_tithe_multinpc", 1)
            t.exec("wiskit-dialog", t.chat.play, { "player:What's going on here?", "npc:They're draining my blood" })
            t.exec("gadderanks", t.player.talk_to, "burgh_gadderanks_multinpc", 1)
            t.exec("gadderanks-dialog", t.chat.play, {
                "player:What's going on here?",
                "npc:Oh happy days!",
                "npc:Now I should be able to make my blood tithe quota",
                "npc:I bet you could pay twice as much",
                "player:I don't have any intentions of paying a blood tithe!",
                "npc:Ha! We'll see about that!",
                "npc:I've had enough of you poking your nose in here!",
            })
            t.ticks(6)
            t.exec("fight-page", t.chat.play, { "npc:I've had enough of you poking your nose in here!" })
            t.expect("quest.stage.tithe_fight", t.quest.expect_stage("tithe_fight"))
            for _, sym in ipairs({ "burgh_vampire_juve_1_attackable", "burgh_gadderanks_attackable", "burgh_vampire_juve_2_attackable" }) do
                local _, st = t.var.varbit("varb1990_myreque_2_quest")
                if st >= 260 then break end
                t.player.attack(sym, 2, 10)
                t.npc.await_dead_engaged(300, 4, { eat = { item = "shark", below = 40 } })
                t.ticks(3)
            end
            t.ticks(4)
            t.exec("gadderanks-words", t.chat.play, {
                "npc:*cough*",
                "npc:Huh, it looks like Gadderanks is still alive.",
                "player:You don't look too well, Gadderanks.",
                "npc:You... You do?",
                "player:Yes, I do.",
                "npc:But... you don't understand.",
                "player:You need to take a second",
                "npc:Then... will you do me a favour?",
                "player:You have my word.",
                "npc:Thank you. I have some information",
                "npc:They fear some things.",
                "player:Take it easy Gadderanks.",
                "npc:It's too late for me *cough*.",
            })
            t.ticks(6)
            drain(t, "passes-away")
            t.expect("quest.stage.veliaf_returned", t.quest.expect_stage("veliaf_returned"))
            local _, hammer = t.inv.count("gadderanks_warhammer")
            t.check("warhammer", hammer == 1, "gadderanks_warhammer x" .. tostring(hammer))
            -- [4567..4585] Veliaf, where Wiskit stood
            t.exec("veliaf-burgh", t.player.talk_to, "burgh_villager_tithe_multinpc", 1)
            t.exec("veliaf-burgh-dialog", t.chat.play, {
                "player:Phew! Thanks for your help Veliaf.",
                "npc:Don't mention it.",
                "player:I have. The cellar of the inn",
                "npc:Excellent! Now, I must return to the Hollows.",
            })
            t.ticks(5)
            drain(t, "veliaf-leaves")
            local _, vis = t.var.varbit("varb1981_blood_tithe_visible")
            t.check("veliaf.gone", vis == 3, "blood_tithe_visible = " .. tostring(vis))
        end },

        { name = "hollows_to_library", run = function(t)
            t.ticklog.mark("leg.hollows_to_library")
            -- [4914..4996] Veliaf's briefing
            tele(t, "0_54_153_50_44")
            t.exec("veliaf2", t.player.talk_to, "route_veliaf_hurtz_parent", 1)
            t.exec("veliaf2-dialog", t.chat.play, {
                "npc:Well met, friend.",
                "player:Likewise. So, what happens now?",
                "npc:Do you know of Drezel?",
                "player:The priest at Paterdomus?",
                "npc:Ah, excellent.",
                "npc:Drezel has agreed to take him in",
                "npc:Myself, Polmafi and Radigad",
                "player:I'm sure I can manage that.",
                "npc:Thank you. There's something else",
                "npc:I'm sure you've noticed by now",
                "npc:If you've met Drezel,",
                "player:He has indeed.",
                "npc:Well I have heard rumour",
                "player:I see. And you'd like me to look into this?",
                "npc:If you could, my friend,",
                "player:I'll see what I can dig up.",
                "npc:Before you go,",
                "player:Sure.",
                "choose:I'd best be off.",
                "player:I'd best be off.",
            })
            t.expect("quest.stage.veliaf_briefed", t.quest.expect_stage("veliaf_briefed"))
            t.exec("polmafi", t.player.talk_to, "route_polmafi_ferdygris_parent", 1)
            t.exec("polmafi-dialog", t.chat.play, { "player:Hey. Veliaf says it's time", "npc:Okay. Thanks for letting me know." })
            t.expect("quest.stage.polmafi_told", t.quest.expect_stage("polmafi_told"))
            -- [5841..5854] Ivan and the map, route 1
            t.cheat("::give shark 10")
            t.exec("ivan", t.player.talk_to, "route_ivan_strom_parent", 1)
            t.exec("ivan-dialog", t.chat.play, {
                "npc:Hey there. I hear you're going to be taking me to the temple.",
                "player:That's right. We should get going soon.",
                "npc:Very well. I'm ready, lead the way.",
            })
            t.expect("quest.stage.trek_started", t.quest.expect_stage("trek_started"))
            -- [5083..5207]: route 2 first, the juvinates left on Ivan until he flees.
            t.exec("map-a", t.ui.await_open, "burgh_map", 10)
            local l2r, l2w = t.ui.widget("burgh_map:route2_layer")
            t.check("map.pick2", l2w ~= nil and t.ui.invoke(l2w, 1) == "ok", "burgh_map:route2_layer " .. tostring(l2r))
            t.ticks(2)
            local m2r, m2w = t.ui.widget("burgh_map:text_travel_layer2")
            t.check("map.route2", m2w ~= nil and t.ui.invoke(m2w, 1) == "ok", "burgh_map:text_travel_layer2 " .. tostring(m2r))
            t.ticks(8)
            t.exec("ambush2-page", t.chat.play, { "npc:There's that tasty morsel Ivan!" })
            local fled = false
            for _ = 1, 40 do
                t.ticks(5)
                local k = t.chat.kind()
                if k == "npc" then fled = true break end
            end
            t.check("ivan.flees", fled, "a page from Ivan while the juvinates beat him")
            t.exec("ivan-flees", t.chat.play, { "npc:I'm getting badly beaten up here," })
            t.exec("exit-path", t.player.click_loc, "burgh_swamp_exit_path", 1)
            t.ticks(4)
            t.expect("quest.stage.trek_started.after_flight", t.quest.expect_stage("trek_started"))
            tele(t, "0_54_153_50_44")
            t.exec("ivan-again", t.player.talk_to, "route_ivan_strom_parent", 1)
            t.exec("ivan-again-dialog", t.chat.play, {
                "npc:Hey there. I hear you're going to be taking me to the temple.",
                "player:That's right. We should get going soon.",
                "npc:Very well. I'm ready, lead the way.",
            })
            t.exec("map", t.ui.await_open, "burgh_map", 10)
            -- [5465..5470]: the route first (its highlight shows the travel text), then travel.
            local lr, lw = t.ui.widget("burgh_map:route1_layer")
            t.check("map.pick1", lw ~= nil and t.ui.invoke(lw, 1) == "ok", "burgh_map:route1_layer " .. tostring(lr))
            t.ticks(2)
            local mr, mw = t.ui.widget("burgh_map:text_travel_layer1")
            t.check("map.route1", mw ~= nil and t.ui.invoke(mw, 1) == "ok", "burgh_map:text_travel_layer1 " .. tostring(mr))
            t.ticks(8)
            t.exec("ambush-page", t.chat.play, { "npc:There's that tasty morsel Ivan!" })
            for i = 1, 2 do
                local _, st = t.var.varbit("varb1990_myreque_2_quest")
                if st >= 315 then break end
                t.player.attack("burgh_ivan_temple_vampire_juve_1", 2, 10)
                t.npc.await_dead_engaged(300, 4, { eat = { item = "shark", below = 40 } })
                t.ticks(2)
            end
            t.ticks(4)
            t.exec("won", t.chat.play, {
                "mesbox:You manage to slay the last of the vampyre juvinates",
                "npc:Many thanks for bringing me safely to the temple.",
                "player:Not a problem.",
            })
            t.expect("quest.stage.at_temple", t.quest.expect_stage("at_temple"))

            -- [6128..6218] Drezel
            tele(t, "0_53_154_48_42")
            t.exec("drezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
            t.exec("drezel-dialog", t.chat.play, {
                "npc:Well done for bringing Ivan to the temple,",
                "player:Yes, it seems to be getting more dangerous",
                "npc:Indeed it does my friend.",
                "choose:Veliaf told me about Ivandis.",
                "player:Veliaf told me about Ivandis.",
                "npc:Hmmm, how can I say this diplomatically.",
                "player:But you don't even know what he said",
                "npc:I assume he's made suggestions",
                "choose:So, you're sure that Ivandis is buried here then?",
                "player:So, you're sure", "npc:If you don't mind me saying,", "player:So where did this rumour", "npc:Rumours come from",
                "choose:What proof do you have that Ivandis is buried here?",
                "player:What proof", "npc:Servants of Saradomin", "player:So you don't actually know", "npc:Please, do not challenge me",
                "choose:What do you know about the history of this temple?",
                "player:What do you know about the history", "npc:I know that it is built", "player:Have you checked that then?", "npc:Why would I need to?",
                "choose:Is there somewhere that I might get more information on Ivandis?",
                "player:Is there somewhere", "npc:What little information there is", "player:So you have no further information", "npc:I have told you all I know.",
                "choose:The lives of those pitiful few left in Morytania could rest on this!",
                "player:The lives of those pitiful few", "npc:And you know I would do anything", "player:I know. So please,",
                "npc:Very well, but there is only so much I can do.", "npc:I know not if it will help,",
                "objbox:Drezel gives you a key.",
                "player:Thank you, Drezel.",
            })
            t.expect("quest.stage.key_given", t.quest.expect_stage("key_given"))
            -- [6244..6265] the keyhole, the trapdoor, the bookcase
            t.exec("keyhole", t.player.click_loc, "burgh_library_keyhole", 1)
            t.exec("keyhole-dialog", t.chat.play, {
                "mesbox:You inspect the panel.",
                "choose:Yes.",
                "mesbox:You use the library key in the keyhole",
            })
            t.exec("lib-down", t.player.click_loc, "burgh_temple_library_trapdoor_open", 1)
            t.ticks(4)
            t.expect("quest.stage.library_entered", t.quest.expect_stage("library_entered"))
            t.exec("bookcase", t.player.click_loc, "burgh_library_bookcase_ivandis", 1)
            t.exec("bookcase-box", t.chat.play, { "objbox:You find a book called The Sleeping Seven." })
            t.exec("read", t.player.inv_op, "burgh_book_sevenwarriors", 1)
            t.ticks(3)
            t.ui.close_modal()
            t.expect("quest.stage.book_read", t.quest.expect_stage("book_read"))
        end },

        { name = "rod_to_end", run = function(t)
            t.ticklog.mark("leg.rod_to_end")
            t.cheat("::clearinv")
            t.cheat("::give hammer 1")
            t.cheat("::give softclay 1")
            -- [7065..7076] the boards and the tomb
            tele(t, "0_54_153_29_40")
            t.exec("boards", t.player.click_loc, "burgh_ivandis_tomb_entrance", 1)
            t.exec("boards-dialog", t.chat.play, {
                "mesbox:There's some boards blocking the cave entrance.",
                "choose:Yes.",
                "objbox:You manage to break up the boards using a hammer.",
            })
            t.exec("tomb", t.player.click_loc, "burgh_ivandis_tomb_entrance", 1)
            t.ticks(6)
            t.expect("quest.stage.tomb_entered", t.quest.expect_stage("tomb_entered"))
            local coffin = t.player.by_symbol("loc", "burgh_ancient_coffin")
            t.exec("mould", t.player.use_on, "softclay", coffin)
            t.exec("mould-box", t.chat.play, { "objbox:You use some clay to create a mould of the rod." })
            t.expect("quest.stage.mould_made", t.quest.expect_stage("mould_made"))
            -- [7386..7502] the rod at Burgh's furnace
            t.cheat("::give silver_bar 1")
            t.cheat("::give mithril_bar 1")
            t.cheat("::give sapphire 1")
            tele(t, "0_55_50_6_8")
            local furnace = t.player.by_symbol("loc", "burgh_furnace_fired")
            t.exec("mould-furnace", t.player.use_on, "burgh_rod_clay", furnace)
            t.exec("silver", t.ui.await_open, "silver_crafting", 10)
            local sr, sw = t.ui.widget("silver_crafting:ivandis")
            t.check("silver.ivandis", sw ~= nil and t.ui.invoke(sw, 1) == "ok", "silver_crafting:ivandis " .. tostring(sr))
            t.ticks(4)
            t.expect("quest.stage.rod_made", t.quest.expect_stage("rod_made"))
            -- [7964..7975] Lvl-1 Enchant
            t.cheat("::give cosmicrune 1")
            t.cheat("::give waterrune 1")
            -- [7964..7966]: the jewellery list first, then Lvl-1 Enchant on the rod.
            t.ui.tab("magic")
            local er, ew = t.ui.widget("magic_spellbook:enchant_jewellery")
            t.check("enchant.list", ew ~= nil and t.ui.invoke(ew, 1) == "ok", "magic_spellbook:enchant_jewellery " .. tostring(er))
            t.ticks(2)
            t.exec("enchant", t.player.cast, "enchant_1", { kind = "held", id = "burgh_rod_command1" }, 15)
            t.ticks(10)
            drain(t, "enchant-box")
            t.expect("quest.stage.rod_enchanted", t.quest.expect_stage("rod_enchanted"))
            -- [8158..8281] the well, with a rope
            t.cheat("::give rope 1")
            tele(t, "0_53_154_32_35")
            local well = t.player.by_symbol("loc", "priestperil_well")
            t.exec("well", t.player.use_on, "burgh_rod_command2", well)
            t.ticks(6)
            t.exec("well-box", t.chat.play, { "objbox:You lower the rod into the Salve" })
            t.expect("quest.stage.rod_blessed", t.quest.expect_stage("rod_blessed"))
            -- [8371..8378] Veliaf in the new hideout
            tele(t, "0_54_150_34_31") -- the trapdoor's landing ([7638])
            t.exec("veliaf-end", t.player.talk_to, "myq5_veliaf_burgh_hideout", 1)
            t.exec("veliaf-end-dialog", t.chat.play, {
                "npc:Well met. How's it going?",
                "player:I have brought you the Rod of Ivandis!",
                "npc:Very impressive. Good work.",
                "objbox:You give the Rod of Ivandis to Veliaf.",
                "npc:I... I don't believe it!",
                "npc:Well",
            })
            t.ticks(3)
            t.expect("quest.complete", t.quest.expect_complete())
        end },
    },
}
