-- Scrambled!. Guide: Quest Helper scrambled (Scrambled.java); content:
-- quest_scrambled/scripts/scrambled.rs2 (lines from Transcript:Scrambled! oldid 15356404),
-- the Dragon Nest and the log balance in areas/area_tlati/scripts/tlati.rs2, the npcs and the
-- item spawns in quest_scrambled/configs/scrambled.spawn.
-- Prerequisites staged by setup: Children of the Sun (::complete) and the three skill gates at
-- exactly the quest's requirement (Construction 38, Cooking 36, Smithing 35;
-- scrambled.constant ^sc_req_*; sc_qualify_fail_reason). No dialogue or script branches on
-- combat level.
-- Margins (no quest script reads them): Attack/Strength/Defence/Hitpoints 70, a rune scimitar,
-- rune armour and sharks for the three guardians (Quest Helper combatGear; the red dragon is level
-- 106, max hit 10, wiki Red_dragon_(Scrambled!) oldid 15247383), and the anti-dragon shield
-- (Quest Helper antifireShield, recommended). Agility 40 for the guide's useDragonShortcut (the
-- log balance's own level gate, tlati.rs2; Quest Helper shows the step only with 40 Agility);
-- no Scrambled! script reads Agility.
--
-- Travel is the real way: Regulus Cento outside Varrock's east gate flies to Civitas illa Fortis
-- (twilightspromise.rs2:161 and :178, p_telejump ^tp_fortis_arrive = 1697,3140), then on foot west to Tal Teok Temple
-- (reach.py: REACH closed-doors len=787). The Renu quetzal to Tal Teklan needs Twilight's Promise
-- (tools/data/shortest_path/transports/quetzals.tsv), which this quest does not require.
--
-- The jigsaw (puzzleSolver) is solved through the client's own interface 922: each loose piece is
-- read off the screen (which model the client draws, its angle, where it sits), turned with its own
-- Rotate op until its angle is 0 and dragged with the mouse onto the spot Quest Helper's
-- EggSolver.java names for that model. Nothing is read from a varp to choose a move.

return {
    id = "scrambled",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        -- No ::scrambled: that debugproc p_teleports into Varlamore (scrambled.rs2), an area
        -- with no on-foot route from Lumbridge. The fresh fixture's quest state is already 0.
        "::setlevel construction 38",
        "::setlevel cooking 36",
        "::setlevel smithing 35",
        "::complete quest_childrenofthesun",
        "::setlevel agility 40", -- useDragonShortcut (tlati.rs2 level gate); no quest script reads it
        "::setlevel attack 70", -- combatGear margin: three guardians up to level 106
        "::setlevel strength 70",
        "::setlevel defence 70",
        "::setlevel hitpoints 70",
        "::give rune_scimitar 1", -- combatGear
        "::give rune_full_helm 1",
        "::give rune_chainbody 1", -- the platebody needs Dragon Slayer
        "::give rune_platelegs 1",
        "::give antidragonbreathshield 1", -- antifireShield (recommended)
        "::give shark 12", -- combatGear: food
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb16758_scrambled",
            constants = {
                not_started = 0, started = 2, start = 4, inspect = 6, king = 8, gather = 14,
                men = 16, eggs = 18, judge = 20, panic = 22, fix = 24, finish = 26, complete = 30,
            },
            row = "quest_scrambled",
            display = "Scrambled!",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("equip.helm", t.player.equip, "rune_full_helm")
        t.exec("equip.body", t.player.equip, "rune_chainbody")
        t.exec("equip.legs", t.player.equip, "rune_platelegs")
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")
        t.exec("equip.shield", t.player.equip, "antidragonbreathshield")

        -- A fight's margin (brief: lowest hp at least a quarter of the maximum AND food left).
        local function fight_margin(name, detail, food_before)
            local lowest = tonumber(tostring(detail):match("lowest hp (%d+)/"))
            local _, hitpoints = t.skill.read("hitpoints")
            local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
            local food_result, food_left = t.inv.count("shark")
            t.check(name, lowest ~= nil and max_hp ~= nil and food_result == "ok"
                and lowest * 4 >= max_hp and food_left >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", sharks "
                .. tostring(food_before) .. " -> " .. tostring(food_left)
                .. " (margin: lowest hp >= a quarter of max AND at least one shark left)")
        end
        local EAT = { eat = { item = "shark", below = 40 } }

        -- 0. Travel to Varlamore: Regulus Cento (3281,3413) flies to Civitas illa Fortis
        -- (twilightspromise.rs2:161 and :178 -> p_telejump 1697,3140), as atfirstlight.lua does.
        t.exec("goto-talkToRegulus", t.player.goto_tile, 3281, 3413, 0)
        t.exec("talkToRegulus", t.player.talk_to, "vmq2_quetzal_keeper_varrock", 1)
        t.exec("talkToRegulus-dialog", t.chat.play, {
            "npc:Nilsal, adventurer. Do you wis",
            "choose:Let's do it!",
            "player:Let's do it!",
            "npc:Then hold on tight. Varlamore ",
        })
        local fly_result = t.await({
            level = function()
                local _, here = t.world.tile()
                return here ~= nil and here.x < 2000
            end,
            note = "landed in Civitas illa Fortis",
        }, 10)
        local _, arrive = t.world.tile()
        t.check("talkToRegulus-arrived", arrive ~= nil and arrive.x == 1697 and arrive.z == 3140,
            "flew to Civitas illa Fortis (tp_fortis_arrive 1697,3140), tile "
            .. tostring(arrive and (arrive.x .. "," .. arrive.z)) .. " await " .. tostring(fly_result))
        t.ticks(3)

        -- 1.1 startQuest: Alan in Tal Teok Temple (scrambled.spawn 1247,3167). Overland from
        -- Civitas: reach.py 1697,3140 -> 1247,3166 REACH closed-doors.
        t.exec("goto-startQuest", t.player.goto_tile, 1247, 3165, 0)
        t.exec("startQuest", t.player.talk_to, "scrambled_alan", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "player:Hello there. What are you doing out here?",
            "npc:You found me! What a day, huh, Humpy Plumpy?",
            "npc:...",
            "player:Right... Is everything okay?",
            "npc:Yes, yes... In fact, I've been getting rather used to life out here.",
            "player:I see... So, who are you, and how did you end up here?",
            "npc:Oh, I'm Alan. I was just visiting the area as a tourist",
            "player:Do you need a hand getting back to civilization?",
            "npc:Oh, no thank you. Like I say, I've actually come to quite like it here.",
            "npc:...",
            "npc:Still, it is nice for us to finally have a guest.",
            "choose:Yes.",
            "player:Well, I suppose I can't say no to that.",
            "npc:Wonderful! Would you mind staying here while I go out and gather the food?",
            "choose:Of course!",
            "player:Of course!",
            "npc:Right, time for me to get going.",
            "player:Hello there, egg.",
            "player:Bollocks.",
        })
        t.expect("quest.stage.inspect", t.quest.expect_stage("inspect"))

        -- 1.2 inspectEggAfterItFell: Humphrey Dumphrey fallen in front of the wall (1247,3170).
        t.exec("inspectEggAfterItFell", t.player.talk_to, "scrambled_egg_dead", 1)
        t.exec("inspectEggAfterItFell-dialog", t.chat.play, {
            "mesbox:The egg is well and truly broken.",
            "player:This isn't good. I'd better head into town and try to find some help.",
        })
        t.expect("quest.stage.king", t.quest.expect_stage("king"))

        -- 1.3 talkToKing: King behind the King's Inn bar (1224,3119); the floor in front of the bar
        -- is 1225-1227,3119 ([apnpc1,scrambled_king] talks across it).
        t.exec("goto-talkToKing", t.player.goto_tile, 1226, 3118, 0)
        t.exec("talkToKing", t.player.talk_to, "scrambled_king", 1)
        t.exec("talkToKing-dialog", t.chat.play, {
            "npc:Welcome, iknami. What can I get you?",
            "choose:Scrumpty Numpty fell off a wall!",
            "player:Scrumpty Numpty fell off a wall!",
            "npc:Bumpty Numpty?",
            "player:Yes, exactly!",
            "npc:Who's that?",
            "player:He's an egg. I need to get him fixed up.",
            "npc:An egg fell off a wall?",
            "player:It was a disaster! He's everywhere! Can you help?",
            "npc:I think you came to the right person.",
            "npc:Still putting a broken egg back together again...",
            "player:Yes?",
            "npc:I have a plan, but it's going to take all my horses and all my men.",
            "player:What's a horse?",
            "npc:I have no idea.",
            "player:Oh... Well, what about these men.",
            "npc:They'll be up to the task, but you'll need to rally them up.",
            "player:Okay, where can I find your men?",
            "npc:Nezketi should be in the temple just south of here",
            "player:Thanks! I'll have them gathered in no time.",
        })
        t.expect("quest.stage.gather", t.quest.expect_stage("gather"))

        -- 1.5 talkToAcatzin: in the inn (1228,3117).
        t.exec("talkToAcatzin", t.player.talk_to, "scrambled_kings_man_3", 1)
        t.exec("talkToAcatzin-dialog", t.chat.play, {
            "player:Excuse me... Are you one of King's men?",
            "npc:Why do you ask?",
            "player:King said that you might be able to help me.",
            "npc:Gumty Fumty?",
            "player:Yes, Pumpty Lumpty fell off a wall! It's a disaster.",
            "npc:A man fell off a wall? How tall is this wall?",
            "player:Well... not exactly a man. He's an egg.",
            "npc:I see... Well, I can probably help once I can get out of here.",
            "player:When will that be?",
            "npc:As soon as I get my axe back.",
            "choose:I can talk to the blacksmith.",
            "player:I can talk to the blacksmith.",
            "npc:Perfect! You'll find him in the western part of town, not far from here.",
            "player:Okay, I'll be back soon.",
        })
        t.expect("talkToAcatzin-agreed", t.var.expect("varb16765_scrambled_kings_man_3", 2))

        -- 1.4 getEmptyBowl: the bowl on the inn's table (wiki Bowl spawn 1230,3120).
        t.exec("getEmptyBowl", t.player.click_obj, "bowl_empty")
        t.expect("getEmptyBowl-held", t.inv.expect_has("bowl_empty", 1))

        -- 1.6 acatzinTalkToBlacksmith: west of the inn (1209,3109).
        t.exec("goto-acatzinTalkToBlacksmith", t.player.goto_tile, 1210, 3110, 0)
        t.exec("acatzinTalkToBlacksmith", t.player.talk_to, "scrambled_blacksmith", 1)
        t.exec("acatzinTalkToBlacksmith-dialog", t.chat.play, {
            "player:Hello there! Are you...",
            "npc:Blacksmith! Yes, I'm the blacksmith!",
            "player:Great! I'm here about...",
            "npc:I'm very busy, so you'll need to be quick! What do you need?",
            "player:Well, I'm here about an axe...",
            "npc:Sorry, I don't sell those! Move along!",
            "player:I don't want to buy one!",
            "npc:Okay, yes, I can repair an axe for you.",
            "player:The axe is already here. I'm just here to pick it up.",
            "npc:Well, you can't. No repairs are getting done until the whetstone is fixed!",
            "player:Well, what's wrong with the whetstone?",
            "npc:Yes, the whetstone!",
            "player:Okay... I guess I can try and fix the whetstone.",
        })

        -- 1.7 acatzinGetNails: the workbench (1210,3112), 20 iron nails.
        t.exec("acatzinGetNails", t.player.click_loc, "scrambled_workbench", 1)
        t.exec("acatzinGetNails-dialog", t.chat.play, {
            "choose:Take the nails.",
            "*",
        })
        t.expect("acatzinGetNails-held", t.inv.expect_has("nails_iron", 20))

        -- acatzinGetHammer: the hammer on the smithy's table (wiki Hammer spawn 1207,3108).
        t.exec("acatzinGetHammer", t.player.click_obj, "hammer")
        t.expect("acatzinGetHammer-held", t.inv.expect_has("hammer", 1))

        -- 1.8 acatzinFixWhetstone: Repair the broken whetstone (1211,3108) with the hammer.
        t.exec("acatzinFixWhetstone", t.player.click_loc, "scrambled_whetstone_broken_op", 1)
        t.exec("acatzinFixWhetstone-dialog", t.chat.play, {
            "choose:Yes.",
            "mesbox:You successfully repair the whetstone.",
        })
        t.expect("acatzinFixWhetstone-fixed", t.var.expect("varb16765_scrambled_kings_man_3", 4))
        t.expect("acatzinFixWhetstone-hammer-kept", t.inv.expect_has("hammer", 1))

        -- 1.9 acatzinTalkToBlacksmithAgain: the damaged axe.
        t.exec("acatzinTalkToBlacksmithAgain", t.player.talk_to, "scrambled_blacksmith", 1)
        t.exec("acatzinTalkToBlacksmithAgain-dialog", t.chat.play, {
            "npc:Is that whetstone fixed?",
            "player:It's done. Now, about that axe...",
            "npc:Axe? I don't have your axe. Are you sure you gave it to me?",
            "player:It's not my axe, it's...",
            "npc:Actually, here, have this one.",
            "*",
        })
        t.expect("acatzinTalkToBlacksmithAgain-held", t.inv.expect_has("scrambled_axe_damaged", 1))

        -- 1.10 acatzinRepairAxe: Use the repaired whetstone.
        t.exec("acatzinRepairAxe", t.player.click_loc, "scrambled_whetstone_fixed_op", 1)
        t.exec("acatzinRepairAxe-dialog", t.chat.play, {
            "choose:Yes.",
            "*",
        })
        t.expect("acatzinRepairAxe-held", t.inv.expect_has("scrambled_axe_repaired", 1))

        -- 1.11 acatzinGetSaw: the saw in Arcuani's Archery Supplies (wiki Saw spawn 1212,3093).
        t.exec("goto-acatzinGetSaw", t.player.goto_tile, 1211, 3095, 0)
        t.exec("acatzinGetSaw", t.player.click_obj, "poh_saw")
        t.expect("acatzinGetSaw-held", t.inv.expect_has("poh_saw", 1))

        -- 2.12 acatzinReturnRepairedAxe: back in the inn.
        t.exec("goto-acatzinReturnRepairedAxe", t.player.goto_tile, 1227, 3116, 0)
        t.exec("acatzinReturnRepairedAxe", t.player.talk_to, "scrambled_kings_man_3", 1)
        t.exec("acatzinReturnRepairedAxe-dialog", t.chat.play, {
            "npc:How are you getting on with my axe?",
            "player:Turned out there was a bit of a backlog of work...",
            "npc:So, have you got it?",
            "player:I have it right here!",
            "npc:Perfect! Hand it over, and then we'll see about Lumpty Mumpty.",
            "player:Rumpty Bumpty.",
            "npc:Yeah, that's it.",
            "player:Okay, here's the axe.",
            "*",
        })
        t.expect("acatzinReturnRepairedAxe-left", t.var.expect("varb16765_scrambled_kings_man_3", 7))

        -- 2.18 talkToNezketi: in the Tal Teklan temple (1224,3105): the empty cup.
        t.exec("goto-talkToNezketi", t.player.goto_tile, 1225, 3106, 0)
        t.exec("talkToNezketi", t.player.talk_to, "scrambled_kings_man_1", 1)
        t.exec("talkToNezketi-dialog", t.chat.play, {
            "player:Excuse me... Are you one of King's men?",
            "npc:I might be.",
            "player:I need your help. It's Bumpty Numpty.",
            "npc:Humphrey Dumphrey?",
            "player:Yes. He's a broken egg.",
            "npc:This isn't good!",
            "player:So, can you help?",
            "npc:I think I can, but I'm going to need to be on top of my game",
            "choose:I can get you some tea.",
            "player:I can get you some tea.",
            "npc:Kuani! There are plenty of damiana shrubs around here.",
            "*",
            "npc:Now, I trust you know how to make tea?",
            "choose:Could you remind me?",
            "player:Could you remind me?",
            "npc:You'll need to take the damiana leaves and add them to a bowl of water.",
            "player:Alright, I'll be back soon with some tea.",
            "npc:Thank you, iknami.",
        })
        t.expect("talkToNezketi-cup", t.inv.expect_has("cup_empty", 1))

        -- 2.19 fillEmptyBowlWithWater: the water pump near the eastern entrance (1242,3097).
        t.exec("goto-fillEmptyBowlWithWater", t.player.goto_tile, 1242, 3099, 0)
        t.exec("fillEmptyBowlWithWater", t.player.use_on, "bowl_empty", t.player.by_symbol("loc", "fortis_water_pump"))
        t.expect("fillEmptyBowlWithWater-held", t.inv.expect_has("bowl_water", 1))

        -- 2.15 talkToKauayotl: outside the eastern entrance (1251,3104).
        t.exec("goto-talkToKauayotl", t.player.goto_tile, 1251, 3102, 0)
        t.exec("talkToKauayotl", t.player.talk_to, "scrambled_kings_man_2", 1)
        t.exec("talkToKauayotl-dialog", t.chat.play, {
            "player:Excuse me... Are you one of King's men?",
            "npc:I sure am. What do you need?",
            "player:I've got a very big problem. It's Bumpty Numpty.",
            "npc:Gumty Fumty?",
            "player:Yes. He was sat on a wall, but he had a great fall!",
            "npc:Tell me you're joking?!",
            "player:I'm afraid not. Do you think you can help?",
            "npc:My friend, this is a matter that's very personal to me.",
            "player:Racing cart?",
            "npc:That's right! We race them over in the Colosseum.",
            "choose:I see. Well, maybe I can help out with that?",
            "player:I see. Well, maybe I can help out with that?",
            "npc:That would be appreciated.",
        })
        t.expect("talkToKauayotl-agreed", t.var.expect("varb16764_scrambled_kings_man_2", 2))

        -- 2.13 getDamianaLeaves: the damiana by the eastern entrance (1250,3110).
        t.exec("getDamianaLeaves", t.player.click_loc, "damiana_shrub", 1)
        t.expect("getDamianaLeaves-held", t.inv.expect_has("damiana_leaves", 1))

        -- 2.20 mixWaterAndDamianaLeaves.
        t.exec("mixWaterAndDamianaLeaves", t.player.use_item_on_item, "damiana_leaves", "bowl_water")
        t.expect("mixWaterAndDamianaLeaves-held", t.inv.expect_has("bowl_damiana_water", 1))

        -- 2.14 getPlanks: the two planks beside the lake south of town (wiki Plank spawns
        -- 1235,3074 and 1238,3076).
        t.exec("goto-getPlanks", t.player.goto_tile, 1237, 3075, 0)
        t.exec("getPlanks", t.player.click_obj, "woodplank")
        t.exec("getPlanks-2", t.player.click_obj, "woodplank")
        t.expect("getPlanks-held", t.inv.expect_has("woodplank", 2))

        -- 2.21 boilDamianaWater: the oven in the general store (stove_clay01_talkasti01_noop,
        -- 1239,3107).
        t.exec("goto-boilDamianaWater", t.player.goto_tile, 1240, 3108, 0)
        t.exec("boilDamianaWater", t.player.use_on, "bowl_damiana_water", t.player.by_symbol("loc", "stove_clay01_talkasti01_noop"))
        t.expect("boilDamianaWater-held", t.inv.expect_has("bowl_damiana_tea", 1))

        -- 2.22 pourTeaIntoCup.
        t.exec("pourTeaIntoCup", t.player.use_item_on_item, "bowl_damiana_tea", "cup_empty")
        t.expect("pourTeaIntoCup-held", t.inv.expect_has("cup_damiana_tea", 1))

        -- 2.16 kauayotlRepairCart: the cart beside Kauayotl (scrambled_cart 1249,3106).
        t.exec("goto-kauayotlRepairCart", t.player.goto_tile, 1251, 3106, 0)
        t.exec("kauayotlRepairCart", t.player.click_loc, "scrambled_cart_broken_op", 1)
        t.exec("kauayotlRepairCart-dialog", t.chat.play, {
            "choose:Yes.",
            "mesbox:You manage to repair the cart.",
        })
        t.expect("kauayotlRepairCart-fixed", t.var.expect("varb16764_scrambled_kings_man_2", 3))

        -- 2.17 talkToKauayotlAgain.
        t.exec("talkToKauayotlAgain", t.player.talk_to, "scrambled_kings_man_2", 1)
        t.exec("talkToKauayotlAgain-dialog", t.chat.play, {
            "player:Right, that's the cart all fixed.",
            "npc:Kuani! It's looking great!",
            "player:Over in the big temple just north of town.",
            "npc:I will go there at once.",
            "mesbox:Kauayotl departs for the temple.",
        })
        t.expect("talkToKauayotlAgain-left", t.var.expect("varb16764_scrambled_kings_man_2", 7))

        -- 2.23 giveTeaToNezketi.
        t.exec("goto-giveTeaToNezketi", t.player.goto_tile, 1225, 3106, 0)
        t.exec("giveTeaToNezketi", t.player.talk_to, "scrambled_kings_man_1", 1)
        t.exec("giveTeaToNezketi-dialog", t.chat.play, {
            "player:I've got your tea!",
            "npc:Wonderful. I'll just have that",
            "player:You mean Gumty Fumty?",
            "npc:That's what I said. Where is he?",
            "player:In the big temple just north of town.",
            "npc:Okay, I'll meet you over there.",
            "*",
        })
        t.expect("quest.stage.men", t.quest.expect_stage("men"))

        -- 3.24 talkToGatheredMen: any of the three at Tal Teok (1247,3166).
        t.exec("goto-talkToGatheredMen", t.player.goto_tile, 1247, 3164, 0)
        t.exec("talkToGatheredMen", t.player.talk_to, "scrambled_kings_man_3", 1)
        t.exec("talkToGatheredMen-dialog", t.chat.play, {
            "player:Okay, how are we looking?",
            "npc:This isn't good, iknami.",
            "player:What are we going to do?",
            "npc:The way I see it, we only have one option.",
            "player:Replace Pumpty Lumpty? That's madness!",
            "npc:It's more than madness.",
            "player:But where would we even get a replacement egg?",
            "npc:We're going to need to obtain a chicken egg.",
            "npc:Tetamo! That's preposterous!",
            "npc:Not if it's from a rather large chicken.",
            "npc:A dragon egg is what we need.",
            "npc:A dragon egg would be suicide.",
            "npc:But jaguars don't lay eggs...",
            "player:How about we gather a few potential eggs and pick the best one?",
            "npc:Kuani! What a brilliant idea.",
            "player:Okay, I just need to know where I can find these eggs.",
            "npc:There are some dragons in a cave just south east of here",
            "npc:There's a chicken farm on the outskirts of Tal Teklan.",
            "npc:And I've heard talk of some eggs in an old camp site",
            "player:Right then. I'll be back once I have some eggs.",
        })
        t.expect("quest.stage.eggs", t.quest.expect_stage("eggs"))

        -- 3.27 / 3.26 / 3.25 the large egg: the chicken farm south of the temple (eggs 1238,3137).
        t.exec("goto-collectLargeEggSpawnChicken", t.player.goto_tile, 1238, 3138, 0)
        t.exec("collectLargeEggSpawnChicken", t.player.click_loc, "scrambled_chicken_eggs_op", 1)
        t.expect("collectLargeEggSpawnChicken-msg",
            t.msg.expect("A nearby chicken doesn't take kindly to you trying to take one of its eggs."))
        local _, chicken_food = t.inv.count("shark")
        -- The guardian is npc_add-ed by the Search (scrambled.rs2); the client sees it a tick later.
        t.exec("guardian.chicken.present", t.npc.await_present, "scrambled_chicken", 10, 5)
        t.exec("fightLargeChicken", t.player.attack, "scrambled_chicken", 2, 20)
        local _, chicken_detail = t.exec("fightLargeChicken.dead", t.npc.await_dead_engaged, 120, 20, EAT)
        fight_margin("fightLargeChicken.margin", chicken_detail, chicken_food)
        t.exec("collectLargeEgg", t.player.click_loc, "scrambled_chicken_eggs_op", 1)
        t.exec("collectLargeEgg-dialog", t.chat.play, {
            "*",
        })
        t.expect("collectLargeEgg-held", t.inv.expect_has("scrambled_chicken_egg", 1))

        -- 3.32 useDragonShortcut: the log balance across the river (1283,3147 -> 1283,3138). The
        -- pieces are put down by tlati.rs2 on level 0; the client holds them on raw level 1 of the
        -- bridged river column, so the copy pressed is named by loc_level.
        t.exec("goto-useDragonShortcut", t.player.goto_tile, 1283, 3147, 0)
        t.exec("useDragonShortcut", t.player.cross_trap, { loc = "tlati_north_river_log_balance_1",
            op_name = "Walk-across", at = { 1283, 3146, 0 }, loc_level = 1, src = { 1283, 3147 }, dest = { 1283, 3138 },
            attempts = 1 })

        -- 3.28 enterDragonCave: the Dragon Nest's mouth (1288,3133) -> 1245,9527.
        t.exec("enterDragonCave", t.player.climb, { loc = "tlati_dragon_nest_cave_entry", op = 1,
            op_name = "Enter", at = { 1288, 3133, 0 }, dest = { 1245, 9527, 0 } })

        -- 3.31 / 3.30 / 3.29 the dragon egg: the eggs in the south-east of the nest (1259,9482).
        -- Quest Helper's safespot is south-east of the eggs (1260,9481).
        t.exec("walk-spawnDragonFromEgg", t.player.walk_to, 1260, 9481)
        t.exec("spawnDragonFromEgg", t.player.click_loc, "scrambled_dragon_eggs_op", 1)
        t.expect("spawnDragonFromEgg-msg",
            t.msg.expect("A nearby dragon doesn't take kindly to you trying to take one of its eggs."))
        local _, dragon_food = t.inv.count("shark")
        -- The guardian is npc_add-ed by the Search (scrambled.rs2); the client sees it a tick later.
        t.exec("guardian.dragon.present", t.npc.await_present, "scrambled_dragon", 10, 5)
        t.exec("fightRedDragon", t.player.attack, "scrambled_dragon", 2, 20)
        local _, dragon_detail = t.exec("fightRedDragon.dead", t.npc.await_dead_engaged, 400, 60, EAT)
        fight_margin("fightRedDragon.margin", dragon_detail, dragon_food)
        t.exec("collectDragonEgg", t.player.click_loc, "scrambled_dragon_eggs_op", 1)
        t.exec("collectDragonEgg-dialog", t.chat.play, {
            "*",
        })
        t.expect("collectDragonEgg-held", t.inv.expect_has("scrambled_dragon_egg", 1))

        -- 4.34 exitDragonCave: 1244,9528 -> 1289,3136.
        t.exec("walk-exitDragonCave", t.player.walk_to, 1245, 9526)
        t.exec("exitDragonCave", t.player.climb, { loc = "tlati_dragon_nest_cave_exit", op = 1,
            op_name = "Enter", at = { 1244, 9528, 0 }, dest = { 1289, 3136, 0 } })

        -- 4.36 / 4.35 / 4.33 the jaguar egg: the camp east of the nest (eggs 1332,3122; reach.py
        -- 1289,3136 -> 1331,3120 REACH closed-doors len=68).
        t.exec("goto-spawnJaguarFromEgg", t.player.goto_tile, 1331, 3123, 0)
        t.exec("spawnJaguarFromEgg", t.player.click_loc, "scrambled_jaguar_eggs_op", 1)
        t.expect("spawnJaguarFromEgg-msg",
            t.msg.expect("A nearby jaguar doesn't take kindly to you trying to take one of its eggs."))
        local _, jaguar_food = t.inv.count("shark")
        -- The guardian is npc_add-ed by the Search (scrambled.rs2); the client sees it a tick later.
        t.exec("guardian.jaguar.present", t.npc.await_present, "scrambled_jaguar", 10, 5)
        t.exec("fightJaguar", t.player.attack, "scrambled_jaguar", 2, 20)
        local _, jaguar_detail = t.exec("fightJaguar.dead", t.npc.await_dead_engaged, 400, 60, EAT)
        fight_margin("fightJaguar.margin", jaguar_detail, jaguar_food)
        t.exec("collectJaguarEgg", t.player.click_loc, "scrambled_jaguar_eggs_op", 1)
        t.exec("collectJaguarEgg-dialog", t.chat.play, {
            "*",
        })
        t.expect("collectJaguarEgg-held", t.inv.expect_has("scrambled_jaguar_egg", 1))

        -- 4.37 / 4.38 returnToTempleWithEggs: each man takes his own egg.
        t.exec("goto-returnToTempleWithEggs", t.player.goto_tile, 1247, 3164, 0)
        t.exec("returnToTempleWithDragonEgg", t.player.talk_to, "scrambled_kings_man_2", 1)
        t.exec("returnToTempleWithDragonEgg-dialog", t.chat.play, {
            "player:I managed to find a dragon egg!",
            "npc:Kuani! Excellent work! I'll get it ready to go.",
            "*",
        })
        t.exec("returnToTempleWithJaguarEgg", t.player.talk_to, "scrambled_kings_man_1", 1)
        t.exec("returnToTempleWithJaguarEgg-dialog", t.chat.play, {
            "player:I have that jaguar egg!",
            "npc:Kuani! I'll get it set up and ready to be judged.",
            "*",
        })
        t.exec("returnToTempleWithEggs", t.player.talk_to, "scrambled_kings_man_3", 1)
        t.exec("returnToTempleWithEggs-dialog", t.chat.play, {
            "player:I managed to find a large chicken egg!",
            "npc:Good work! I'll get it set up so its ready to be judged.",
            "*",
            "npc:That's the last one. Let's get these eggs judged.",
        })
        t.expect("quest.stage.judge", t.quest.expect_stage("judge"))

        -- 4.39 judgeEggs.
        t.exec("judgeEggs", t.player.talk_to, "scrambled_kings_man_1", 1)
        t.exec("judgeEggs-dialog", t.chat.play, {
            "player:Right, we have the eggs.",
            "npc:We've prettied them up as best we can.",
            "npc:First up, the chicken egg.",
            "player:Hmm... It's not bad",
            "npc:Well, what about the dragon egg?",
            "player:It's a good size, but the face...",
            "npc:The jaguar egg then?",
            "npc:I still don't think it's actually a jaguar egg...",
            "player:Well, regardless of what it is, I do think it might be the one.",
            "npc:Kuani! Good work everyone. I think we've done it!",
        })
        t.expect("quest.stage.panic", t.quest.expect_stage("panic"))

        -- 4.40 panicWithKingsMen.
        t.exec("panicWithKingsMen", t.player.talk_to, "scrambled_kings_man_2", 1)
        t.exec("panicWithKingsMen-dialog", t.chat.play, {
            "player:What are we going to do now?",
            "npc:We gave it our best, but I fear we have failed.",
            "npc:We can't give up!",
            "npc:There's only one thing for it.",
            "npc:But how? He was broken beyond repair!",
            "npc:We don't have any other options. We have to try.",
            "npc:But it's not possible!",
            "player:No, it's necessary...",
        })
        t.expect("quest.stage.fix", t.quest.expect_stage("fix"))

        -- 4.41 putEggBackTogether: Fix Humphrey Dumphrey (1244,3168); the jigsaw opens
        -- (Quest Helper isPuzzleOpen = InterfaceID.Jigsaw.BACKGROUND).
        t.exec("putEggBackTogether", t.player.talk_to, "scrambled_egg_fix", 1)
        t.expect("putEggBackTogether-puzzle-open", t.ui.await_open("jigsaw", 10))

        -- 4.42 puzzleSolver (Quest Helper EggSolver.java): "Rotate the piece until you're prompted
        -- to move it to the correct spot" -- a piece is right at rotation 0 (REQUIRED_ROTATION) with
        -- its corner on the spot addEggPair names for its model. EGG_SPOT is EggSolver's
        -- addEggPair(model, x, y) table, verbatim. Each loose piece is identified by the model the
        -- client draws for it; its angle and place are read back from the client.
        local EGG_SPOT = {
            [57106] = { 194, 121 }, [57109] = { 217, 27 }, [57111] = { 260, 22 },
            [57107] = { 250, 41 }, [57123] = { 175, 58 }, [57125] = { 198, 60 },
            [57118] = { 288, 71 }, [57104] = { 231, 71 }, [57127] = { 231, 99 },
            [57115] = { 257, 113 }, [57122] = { 273, 115 }, [57116] = { 306, 119 },
            [57103] = { 243, 149 }, [57120] = { 309, 160 }, [57114] = { 269, 177 },
            [57102] = { 297, 217 }, [57124] = { 252, 213 }, [57126] = { 244, 255 },
            [57112] = { 199, 248 }, [57121] = { 222, 199 }, [57119] = { 187, 210 },
            [57105] = { 149, 179 }, [57108] = { 209, 160 }, [57101] = { 165, 141 },
            [57110] = { 157, 103 }, [57117] = { 196, 105 },
        }
        local QUARTER = 256 -- the Rotate op's step (torirs_jigsaw_piece_rot.cs2: +256, 2048 wraps to 0)
        local placed = 0
        local loose = 0
        -- The loose pile is jigsaw:pieces' children; the jigsaw holds at most 31
        -- (torirs_jigsaw_validate.cs2), so its sub-ids are 0..30.
        for sub = 0, 30 do
            local pose_result, _, pose = t.ui.model_pose("jigsaw:pieces", sub)
            if pose_result == "ok" then
                loose = loose + 1
                local spot = EGG_SPOT[pose.model]
                local name = "puzzleSolver-piece-" .. tostring(pose.model)
                if spot == nil then
                    t.check(name, false, "the client draws model " .. tostring(pose.model)
                        .. " for piece " .. sub .. ", which EggSolver does not name")
                else
                    local _, widget = t.ui.widget("jigsaw:pieces", sub)
                    local turns = ((2048 - pose.zan) % 2048) // QUARTER
                    local angle = pose.zan
                    for _ = 1, turns do
                        t.ui.invoke(widget, 1)
                        angle = (angle + QUARTER) % 2048
                        t.ui.await_model_pose("jigsaw:pieces", { zan = angle }, 5, sub)
                    end
                    local turned_result, turned_detail = t.ui.model_pose("jigsaw:pieces", sub)
                    t.step(name .. "-turned", turned_result == "ok" and angle == 0 and "PASS" or "FAIL",
                        string.format("%d Rotate op(s) from %d -> %s", turns, pose.zan,
                            tostring(turned_detail)))
                    t.exec(name, t.ui.drag, "jigsaw:pieces", sub,
                        { sym = "jigsaw:pieces", x = spot[1], y = spot[2] })
                    -- A right placement is locked onto the board (torirs_jigsaw_piece_place.cs2
                    -- moves the piece from jigsaw:pieces to jigsaw:pieces_locked); the last one
                    -- completes the egg and the jigsaw closes instead.
                    local locked, closed = false, false
                    for _ = 1, 6 do
                        if t.ui.widget("jigsaw:pieces_locked", sub) == "ok" then
                            locked = true
                            break
                        end
                        if t.ui.await_close("jigsaw", 1) == "ok" then
                            closed = true
                            break
                        end
                    end
                    if locked or closed then
                        placed = placed + 1
                    end
                    t.check(name .. "-locked", locked or closed, string.format(
                        "piece %d (model %d) dropped at %d,%d: %s", sub, pose.model, spot[1],
                        spot[2], locked and "locked onto jigsaw:pieces_locked"
                            or (closed and "the last piece -- the jigsaw closed on the whole egg"
                            or "NOT locked, the jigsaw still open")))
                end
            end
        end
        t.check("puzzleSolver", loose > 0 and placed == loose, string.format(
            "%d of the %d loose pieces locked in place", placed, loose))
        local close_result = t.ui.await_close("jigsaw", 10)
        t.check("puzzleSolver-closed", close_result == "ok",
            "the jigsaw (interface 922) after the last piece -> " .. tostring(close_result))
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))

        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))

        -- 4.43 finishQuest: any of King's men; Alan comes back.
        t.exec("finishQuest", t.player.talk_to, "scrambled_kings_man_3", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "npc:It's beautiful!",
            "npc:Indeed it is. I think this means our work here is done.",
            "player:Thank you. I don't think I could have done it without you three.",
            "npc:Don't mention it. We're happy to have helped.",
            "npc:I'm back! How was Plumper?",
            "player:Er... No. No problems at all.",
            "npc:Wonderful! I'm so glad to hear it.",
            "npc:You know, friend, it wasn't until now",
            "npc:But now... now I think I've finally found myself.",
            "player:Bollocks.",
        })

        t.quest.expect_complete()

        t.check("reward.construction", t.skill.expect_gain("construction", 5000, reward_before))
        t.check("reward.cooking", t.skill.expect_gain("cooking", 5000, reward_before))
        t.check("reward.smithing", t.skill.expect_gain("smithing", 5000, reward_before))

        t.finish(0)
    end,
}
