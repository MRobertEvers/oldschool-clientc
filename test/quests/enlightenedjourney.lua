-- Enlightened Journey: Auguste's hot air balloon. Driven with real clicks from the Port Sarim
-- monk to the Taverley landing (docs/quests/ladders/enlightenedjourney.notes.md).
-- Setup stages the brought-along kit only: qp/stat requirements, the papyrus, ball of wool, candle
-- and tinderbox the guide lists. Everything the guide has you gather is driven: sacks from Sarah,
-- redberries from Wydin, dyes from Aggie, silk from the silk trader, potatoes by Fill on a sack,
-- willow branches from a grown sapling plus secateurs, logs with Bob's axe
-- (routes in test/quests/wip/enlightenedjourney/relay.md).
-- Cutscenes (first/second launch, basket weaving) are spec-pending (CUTSCENES.tsv).

return {
    id = "enlightenedjourney",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = {
        "::clearinv",
        "::setlevel firemaking 20",
        "::setlevel farming 30",
        "::setlevel crafting 36",
        "::setvar varp101_qp 20",
        "::give papyrus 3",
        "::give ball_of_wool 1",
        "::give unlit_candle 1",
        "::give tinderbox 1",
        "::give coins 500",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb2866_zep_quest",
            constants = {
                not_started = 0, talk_one = 5, talk_two = 6, talk_three = 10, prototype = 20,
                second_trial = 40, after_mob = 60, gathering = 70, basket_build = 80,
                fly_ready = 90, landed = 100, complete = 200,
            },
            row = "quest_enlightenedjourney",
            display = "Enlightened Journey",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- TRIP 1 KIT (wiki Enlightened Journey oldid 15292357 "items"; QH getItemRequirements)
        t.exec("goto-sarah", t.player.goto_tile, 3036, 3290, 0)
        t.exec("sarah-trade", t.shop.open, "farming_shopkeeper_1", 3, "farming_shop_1")
        t.exec("buySacks", t.shop.buy, "sack_empty", 9)
        local cr0, cd0 = t.shop.close()
        t.check("sarah-close", cr0 == "ok", tostring(cr0) .. ": " .. tostring(cd0))
        t.exec("goto-wydin", t.player.goto_tile, 3014, 3206, 0)
        for i = 1, 3 do
            t.exec("wydin-trade-" .. i, t.shop.open, "wydin", 3, "wydinstore")
            t.exec("buyRedberries-" .. i, t.shop.buy, "redberries", 1)
            local wr, wd = t.shop.close()
            t.check("wydin-close-" .. i, wr == "ok", tostring(wr) .. ": " .. tostring(wd))
            if i < 3 then t.ticks(110) end
        end
        -- the onion patch is the fenced yard south of Fred's house (x 3186-3191 z 3265-3269,
        -- maps/m49_51.jl2): in and out by its west gate qip_sheep_shearer_fencegate_l 3186,3268
        -- (a west-edge leaf; reach.py 3015,3206 -> 3185,3268 REACH closed-doors)
        t.exec("goto-onions", t.player.goto_tile, 3185, 3268, 0)
        t.exec("onions.gateIn", t.player.pass_door, { closed = "qip_sheep_shearer_fencegate_l",
            open = "qip_sheep_shearer_openfencegate_l", at = { 3186, 3268, 0 }, near = { 3185, 3268 }, far = { 3187, 3268 },
            far_ok = function(tile) return tile.x >= 3186 end, far_desc = "in the onion yard, x >= 3186" })
        for i = 1, 2 do
            t.exec("pickOnion-" .. i, t.player.click_loc, "onion", 2)
            t.exec("pickOnion-" .. i .. ".count", t.inv.await, "onion", i, 10)
        end
        t.exec("onions.gateOut", t.player.pass_door, { closed = "qip_sheep_shearer_fencegate_l",
            open = "qip_sheep_shearer_openfencegate_l", at = { 3186, 3268, 0 }, near = { 3186, 3268 }, far = { 3184, 3268 },
            far_ok = function(tile) return tile.x <= 3185 end, far_desc = "west of the onion yard, x <= 3185" })
        -- Aggie's house (poordoor 3088,3258, an east-wall leaf: x <= 3088 is inside; prince.lua)
        t.exec("goto-aggie", t.player.goto_tile, 3089, 3258, 0)
        t.exec("aggie.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3088, 3258, 0 }, near = { 3089, 3258 }, far = { 3087, 3258 },
            far_ok = function(tile) return tile.x <= 3088 end, far_desc = "in Aggie's house, x <= 3088" })
        local aggie = t.player.by_symbol("npc", "aggie")
        t.exec("makeRedDye", t.player.use_on, "redberries", aggie)
        t.exec("makeRedDye-dialog", t.chat.play, { "player:Okay, make me some red dye please.", "mesbox:You hand the berries" })
        t.exec("reddye.has", t.inv.await, "reddye", 1, 5)
        t.exec("makeYellowDye", t.player.use_on, "onion", aggie)
        t.exec("makeYellowDye-dialog", t.chat.play, { "*" })
        t.exec("yellowdye.has", t.inv.await, "yellowdye", 1, 5)
        t.exec("aggie.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3088, 3258, 0 }, near = { 3088, 3258 }, far = { 3090, 3258 },
            far_ok = function(tile) return tile.x >= 3089 end, far_desc = "outside Aggie's house, x >= 3089" })
        -- the potato field is fenced; its one gate is fencegate_l 3145,3291 on its north edge
        -- (maps/m49_51.jl2; reach.py 3089,3258 -> 3145,3292 REACH closed-doors)
        t.exec("goto-potatoes", t.player.goto_tile, 3145, 3292, 0)
        t.exec("potatoes.gateIn", t.player.pass_door, { closed = "fencegate_l", open = "openfencegate_l",
            at = { 3145, 3291, 0 }, near = { 3145, 3292 }, far = { 3145, 3290 },
            far_ok = function(tile) return tile.z <= 3291 end, far_desc = "in the potato field, z <= 3291" })
        for i = 1, 10 do
            t.exec("pickPotato-" .. i, t.player.click_loc, "potato", 2)
            t.exec("pickPotato-" .. i .. ".count", t.inv.await, "potato", i, 10)
        end
        local fr, fd = t.player.inv_op("sack_empty", 1)
        t.ticks(1)
        t.check("fillPotatoSack", select(2, t.inv.count("sack_potato_10")) == 1,
            "Fill on sack_empty -> " .. tostring(fr) .. " " .. tostring(fd) .. "; sack_potato_10 " .. tostring(select(2, t.inv.count("sack_potato_10"))))
        t.exec("potatoes.gateOut", t.player.pass_door, { closed = "fencegate_l", open = "openfencegate_l",
            at = { 3145, 3291, 0 }, near = { 3145, 3291 }, far = { 3145, 3293 },
            far_ok = function(tile) return tile.z >= 3292 end, far_desc = "north of the potato field, z >= 3292" })
        t.exec("goto-silk", t.player.goto_tile, 3298, 3206, 0)
        for i = 1, 10 do
            t.exec("buySilk-" .. i, t.player.talk_to, "silk_trader", 1)
            t.exec("buySilk-" .. i .. "-dialog", t.chat.play, {
                "npc:Do you want to buy any fine silks?", "choose:How much are they?", "player:How much are they?",
                "npc:3 gp.", "choose:Okay, that sounds good.", "player:Okay, that sounds good.", "mesbox:You buy some silk for 3 gp.",
            })
            t.exec("buySilk-" .. i .. ".count", t.inv.await, "silk", i, 5)
        end
        t.exec("kit.trip1", t.inv.await_all, { sack_empty = 8, sack_potato_10 = 1, reddye = 1, yellowdye = 1, silk = 10, papyrus = 3 }, 3)

        -- travelToEntrana
        t.exec("goto-travelToEntrana", t.player.goto_tile, 3047, 3236, 0)
        t.exec("travelToEntrana", t.player.talk_to, "shipmonk1_c", 1)
        t.exec("travelToEntrana-dialog", t.chat.play, {
            "npc:Do you seek passage",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes",
            "npc:Very well",
            "mesbox:The monk quickly searches you.",
        })
        t.ticks(8)
        local _, dk = t.world.tile()
        t.check("travelToEntrana-deck", dk.x >= 2830 and dk.x <= 2838 and dk.z >= 3328 and dk.z <= 3334,
            "tile " .. dk.x .. "," .. dk.z .. " level " .. tostring(t.world.level()))
        t.exec("useGangPlank", t.player.click_loc, "ship_from_entrana_off", 1)
        t.ticks(8)
        local _, pk = t.world.tile()
        t.check("useGangPlank-pier", pk.z >= 3334, "tile " .. pk.x .. "," .. pk.z)

        -- talkToAuguste (three yeses over three talks)
        t.exec("goto-talkToAuguste", t.player.goto_tile, 2808, 3355, 0)
        t.exec("talkToAuguste", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAuguste-dialog-1", t.chat.play, {
            "npc:Ah, hello", "npc:I've grown", "player:A hot air balloon", "npc:Indeed",
            "choose:Yes! Sign me up.", "player:Yes! Sign me up", "npc:Splendid",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_one", t.quest.expect_stage("talk_one"))
        t.exec("talkToAuguste-2", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAuguste-dialog-2", t.chat.play, {
            "npc:Ah, you came back", "choose:Umm, yes. What's your point?",
            "player:Umm, yes", "npc:My point is",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_two", t.quest.expect_stage("talk_two"))
        t.exec("talkToAuguste-3", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAuguste-dialog-3", t.chat.play, {
            "npc:One last matter", "choose:Yes.", "player:Yes", "npc:Wonderful",
        })
        t.ticks(2)
        t.expect("quest.stage.prototype", t.quest.expect_stage("prototype"))

        -- usePapyrusOnWool, useCandleOnBalloon
        local frame_r, frame_d = t.player.use_item_on_item("papyrus", "ball_of_wool")
        t.check("usePapyrusOnWool", frame_r == "ok" and select(2, t.inv.count("zep_test_balloon_struc")) == 1,
            "use papyrus on wool -> " .. tostring(frame_r) .. " " .. tostring(frame_d))
        local ball_r, ball_d = t.player.use_item_on_item("unlit_candle", "zep_test_balloon_struc")
        t.ticks(1)
        t.check("useCandleOnBalloon", ball_r == "ok" and select(2, t.inv.count("zep_test_balloon")) == 1,
            "use candle on frame -> " .. tostring(ball_r) .. " " .. tostring(ball_d))

        -- talkToAugusteAgain
        t.exec("talkToAugusteAgain", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteAgain-dialog", t.chat.play, {
            "player:Yes, I have them here", "npc:Wonderful", "npc:Now, that was only",
        })
        t.ticks(2)
        t.expect("quest.stage.second_trial", t.quest.expect_stage("second_trial"))

        -- talkToAugusteWithPapyrus (2 papyrus + sack of potatoes)
        t.exec("talkToAugusteWithPapyrus", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteWithPapyrus-dialog", t.chat.play, {
            "player:Yes, I have them here", "npc:Perfect", "npc:Great Guthix",
        })
        t.ticks(2)
        t.expect("quest.stage.after_mob", t.quest.expect_stage("after_mob"))

        -- talkToAugusteAfterMob
        t.exec("talkToAugusteAfterMob", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteAfterMob-dialog", t.chat.play, {
            "player:What in Guthix", "npc:I have a theory", "player:Right", "npc:In any case", "npc:for the burner", "end",
        })
        t.ticks(2)
        t.expect("quest.stage.gathering", t.quest.expect_stage("gathering"))

        -- fillSacks: use an empty sack on the sandpit, eight times
        -- the sand pit (2816,3341) covers 2816-2817,3341-3342: stand east of it
        t.exec("goto-fillSacks", t.player.goto_tile, 2818, 3341, 0)
        local pit = t.player.by_symbol("loc", "sandpit")
        for i = 1, 8 do
            local r, d = t.player.use_on("sack_empty", pit)
            t.inv.await("zep_sandbag", i, 8)
            t.check("fillSacks-" .. i, r == "ok" and select(2, t.inv.count("zep_sandbag")) == i,
                "sandbags " .. tostring(select(2, t.inv.count("zep_sandbag"))) .. " (" .. tostring(r) .. " " .. tostring(d) .. ")")
        end

        -- giving Auguste the materials; the bowl is in his house, up the ladder
        -- Auguste's house (x 2816-2822 z 3351-3356, maps/m44_52.jl2): in and out by its east
        -- poshdoor 2822,3354 (an east-wall leaf: x <= 2822 is inside)
        t.exec("goto-bowl", t.player.goto_tile, 2823, 3354, 0)
        t.exec("bowl.doorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2822, 3354, 0 }, near = { 2823, 3354 }, far = { 2821, 3354 },
            far_ok = function(tile) return tile.x <= 2822 end, far_desc = "in Auguste's house, x <= 2822" })
        t.exec("climbLadder", t.player.climb, { loc = "ladder", at = { 2816, 3352, 0 }, src = { 2817, 3352 },
            dest = { 2817, 3352, 1 }, slack = 1 })
        t.exec("takeBowl", t.player.click_obj, "bowl_empty", 3)
        t.exec("bowl.has", t.inv.await, "bowl_empty", 1, 10)
        t.exec("climbDown", t.player.climb, { loc = "laddertop", at = { 2816, 3352, 1 }, dest = { 2817, 3352, 0 }, slack = 1 })
        t.exec("bowl.doorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2822, 3354, 0 }, near = { 2822, 3354 }, far = { 2824, 3354 },
            far_ok = function(tile) return tile.x >= 2823 end, far_desc = "outside Auguste's house, x >= 2823" })
        t.exec("goto-giveAuguste", t.player.goto_tile, 2808, 3355, 0)
        t.exec("giveDye", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveDye-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give dye.", "player:Dye", "npc:Ah, wonderful, red", "npc:Ah, wonderful, yellow",
        })
        t.exec("giveSandbags", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveSandbags-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give sandbags.", "player:Sandbags", "npc:Sandbags, thank you",
        })
        t.exec("giveSilk", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveSilk-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give silk.", "player:Silk", "npc:Silk for the balloon",
        })
        t.exec("giveBowl", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveBowl-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give bowl.", "player:Bowl", "npc:Ah, the bowl", "npc:That's everything", "*", "end",
        })
        t.ticks(2)
        t.expect("quest.stage.basket_build", t.quest.expect_stage("basket_build"))
        t.check("giveAuguste-sapling", select(2, t.inv.count("zep_plantpot_willow_sapling")) == 1,
            "sapling " .. tostring(select(2, t.inv.count("zep_plantpot_willow_sapling"))) .. ", apples " .. tostring(select(2, t.inv.count("basket_apple_5"))))

        -- TRIP 2: grow Auguste's sapling into a willow and cut its branches
        -- (wiki Enlightened Journey oldid 15292357 note; Willow branch oldid 15184331)
        t.exec("goto-leaveEntrana", t.player.goto_tile, 2832, 3336, 0)
        t.exec("leaveEntrana", t.player.talk_to, "shipmonk2", 1)
        t.exec("leaveEntrana-dialog", t.chat.play, {
            "npc:Do you wish to leave holy Entrana?", "choose:Yes, I'm ready to go.", "player:Yes, I'm ready to go.", "npc:Okay, let's board",
        })
        t.ticks(8)
        t.exec("leaveShip", t.player.click_loc, "ship_to_entrana_off", 1)
        t.ticks(6)
        t.exec("goto-sarah2", t.player.goto_tile, 3036, 3290, 0)
        t.exec("sarah2-trade", t.shop.open, "farming_shopkeeper_1", 3, "farming_shop_1")
        t.exec("buyRake", t.shop.buy, "rake", 1)
        t.exec("buySpade", t.shop.buy, "spade", 1)
        t.exec("buySecateurs", t.shop.buy, "secateurs", 1)
        local cr1, cd1 = t.shop.close()
        t.check("sarah2-close", cr1 == "ok", tostring(cr1) .. ": " .. tostring(cd1))
        t.exec("goto-patch", t.player.goto_tile, 3002, 3376, 0)
        local _, ps0 = t.var.server("varb701_varbit_701")
        if ps0 ~= 3 then
            t.exec("rakePatch", t.player.click_loc, "farming_tree_patch_2", 1)
            t.exec("rakePatch.weeded", t.var.await_server, "varb701_varbit_701", 3, 80)
        end
        local patch = t.player.by_symbol("loc", "farming_tree_patch_2")
        t.exec("plantSapling", t.player.use_on, "zep_plantpot_willow_sapling", patch)
        t.exec("plantSapling.state", t.var.await_server, "varb701_varbit_701", 15, 10)
        for stage = 1, 6 do
            t.exec("growWillow" .. stage .. "-skip", t.clock.skip, 40)
            t.exec("growWillow" .. stage, t.var.await_server, "varb701_varbit_701", 15 + stage, 560)
        end
        t.exec("checkWillowHealth", t.player.click_loc, "farming_tree_patch_2", 1)
        t.exec("checkWillowHealth.state", t.var.await_server, "varb701_varbit_701", 22, 10)
        for round = 1, 2 do
            t.exec("willowBranches" .. round .. "-skip", t.clock.skip, 30)
            t.exec("willowBranches" .. round, t.var.await_server, "varb701_varbit_701", 197, 560)
            t.exec("cutBranches" .. round, t.player.use_on, "secateurs", patch)
            t.exec("cutBranches" .. round .. ".count", t.inv.await, "willow_branch", 6 * round, 40)
        end
        -- the pack is 23 full: the farming leftovers go (a player would bank them)
        -- (t.player.drop reads the ground count, which a second weeds on the same
        -- tile does not raise: drop them directly and read the backpack instead)
        for _ = 1, 3 do t.player.drop("weeds") t.ticks(1) end
        t.check("dropJunk-weeds", select(2, t.inv.count("weeds")) == 0, "weeds held " .. tostring(select(2, t.inv.count("weeds"))))
        for _, junk in ipairs({ "plantpot_empty", "rake", "spade" }) do
            t.exec("dropJunk-" .. junk, t.player.drop, junk)
        end
        -- ten logs; the axe stays on the mainland (wiki: axes cannot be taken to Entrana)
        t.exec("goto-bob", t.player.goto_tile, 3230, 3203, 0)
        t.exec("bob-trade", t.player.talk_to, "bob", 3)
        t.exec("bob-trade-dialog", t.chat.play, { "player:Have you anything to sell?", "npc:Yes! I buy and sell axes!" })
        t.exec("bob-attach", t.shop.attach, "axeshop")
        t.exec("buyAxe", t.shop.buy, "bronze_axe", 1)
        local cr2, cd2 = t.shop.close()
        t.check("bob-close", cr2 == "ok", tostring(cr2) .. ": " .. tostring(cd2))
        t.exec("goto-trees", t.player.goto_tile, 3165, 3225, 0)
        -- the trees west of Lumbridge castle, by tile: "nearest tree" walked the
        -- player over the river into the goblins once (killed, run b of this copy)
        local trees = { {3168,3233}, {3171,3236}, {3178,3238}, {3181,3237}, {3180,3224},
            {3173,3216}, {3170,3213}, {3179,3212}, {3177,3208}, {3161,3208}, {3158,3209},
            {3156,3214}, {3154,3217}, {3154,3224}, {3146,3226}, {3145,3222} }
        for i = 1, 32 do
            local _, have = t.inv.count("logs")
            if have >= 10 then break end
            local at = trees[((i - 1) % #trees) + 1]
            t.exec("chopTree-" .. i, t.player.click_loc, "tree", 1, { at = at })
            t.inv.await("logs", have + 1, 30)
            t.note("chopTree-" .. i .. ": logs " .. tostring(have) .. " -> " .. tostring(select(2, t.inv.count("logs"))))
        end
        t.exec("logs.has", t.inv.expect_has, "logs", 10)
        t.exec("dropAxe", t.player.drop, "bronze_axe")
        t.exec("goto-travelToEntrana2", t.player.goto_tile, 3047, 3236, 0)
        t.exec("travelToEntrana2", t.player.talk_to, "shipmonk1_c", 1)
        t.exec("travelToEntrana2-dialog", t.chat.play, {
            "npc:Do you seek passage", "choose:Yes, okay, I'm ready to go.", "player:Yes", "npc:Very well", "mesbox:The monk quickly searches you.",
        })
        t.ticks(8)
        t.exec("useGangPlank2", t.player.click_loc, "ship_from_entrana_off", 1)
        t.ticks(8)
        t.exec("goto-basket", t.player.goto_tile, 2808, 3355, 0)

        -- talkToAugusteWithBranches: twelve willow branches on the basket frame
        local basket = t.player.by_symbol("loc", "zep_multi_basket_entrana")
        local wr, wd = t.player.use_on("willow_branch", basket)
        t.ticks(2)
        t.exec("weave-dismiss", t.chat.continue_, true)
        t.check("talkToAugusteWithBranches", wr == "ok", "use branches on basket -> " .. tostring(wr) .. " " .. tostring(wd))
        t.expect("quest.stage.fly_ready", t.quest.expect_stage("fly_ready"))

        -- talkToAugusteWithLogsAndTinderbox: ten logs + tinderbox, then fly
        t.exec("talkToAugusteFly", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteFly-dialog", t.chat.play, {
            "npc:Excellent", "npc:We must avoid", "npc:Dropping a sandbag", "choose:Okay.", "player:Okay",
        })
        t.ticks(3)
        local _, w_sand = t.ui.widget("zep_interface_side:zep_btn_sandbags")
        local _, w_log = t.ui.widget("zep_interface_side:zep_btn_logs")
        local _, w_relax = t.ui.widget("zep_interface_side:zep_btn_relax")
        local _, w_tug = t.ui.widget("zep_interface_side:zep_btn_tug")
        local _, w_emerg = t.ui.widget("zep_interface_side:zep_btn_tug_emerg")
        local route = {
            "S", "L", "R","R","R","R","R","R","R","R","R", "E", "R","R", "T", "R","R","R","R","R",
            "R", "L", "R", "L", "R","R","R","R","R","R","R","R","R","R", "L", "R","R","R","R","R",
            "R","R","R","R","R","R","R","R", "E", "T", "R","R","R", "L", "R","R","R","R", "T", "R",
        }
        local ctl = { S = w_sand, L = w_log, R = w_relax, T = w_tug, E = w_emerg }
        for i, key in ipairs(route) do
            t.ui.invoke(ctl[key], 1)
            t.ticks(1)
        end
        t.ticks(3)
        t.check("flight-landed", select(2, t.quest.stage()) == 100, "stage " .. tostring(select(2, t.quest.stage())))
        t.exec("flight-dismiss", t.chat.continue_, true)
        t.expect("quest.stage.landed", t.quest.expect_stage("landed"))

        -- talkToAugusteToFinish (Taverley)
        local _, snap = t.skill.snapshot()
        t.exec("talkToAugusteToFinish", t.player.talk_to, "zep_multi_piccard", 1)
        t.exec("talkToAugusteToFinish-dialog", t.chat.play, {
            "npc:We have travelled", "npc:I'm considering",
        })
        t.ticks(3)
        t.quest.expect_complete()
        local g_crafting, g_crafting_d = t.skill.expect_gain("crafting", 2000, snap)
        t.check("reward.crafting", g_crafting == "ok", "crafting +2000 -> " .. tostring(g_crafting) .. " " .. tostring(g_crafting_d))
        local g_farming, g_farming_d = t.skill.expect_gain("farming", 3000, snap)
        t.check("reward.farming", g_farming == "ok", "farming +3000 -> " .. tostring(g_farming) .. " " .. tostring(g_farming_d))
        local g_woodcutting, g_woodcutting_d = t.skill.expect_gain("woodcutting", 1500, snap)
        t.check("reward.woodcutting", g_woodcutting == "ok", "woodcutting +1500 -> " .. tostring(g_woodcutting) .. " " .. tostring(g_woodcutting_d))
        local g_firemaking, g_firemaking_d = t.skill.expect_gain("firemaking", 4000, snap)
        t.check("reward.firemaking", g_firemaking == "ok", "firemaking +4000 -> " .. tostring(g_firemaking) .. " " .. tostring(g_firemaking_d))
        t.check("reward.jacket", t.inv.expect_has("zep_bomber_jacket", 1) == "ok", "bomber jacket held")
        t.check("reward.cap", t.inv.expect_has("zep_bomber_cap", 1) == "ok", "bomber cap held")
        t.finish(0)
    end,
}
