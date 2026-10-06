-- Demon Slayer, driven end to end from Quest Helper's demonslayer/DemonSlayer.java
-- (steps Aris -> Prysin -> Rovin -> bucket/sink/drain -> manhole -> sewer key ->
-- Traiborn's bones -> Silverlight -> Delrith and the incantation).
-- Brought along (getItemRequirements): coins, 25 bones, combat stats. The bucket,
-- the water and every key are obtained by playing the quest.
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md standing rules, owner 2026-10-03): no goto lands in or
-- leaves a closed space. Every room below is entered and left on foot through its door, every
-- floor change is a click on its staircase or ladder (t.player.climb; the sewer, a level-0 map
-- frame at z+6400, through t.player.cross_trap since climb refuses a same-level landing), and a
-- goto only joins two open, outdoor tiles. The map (maps/m50_54.jl2, m48_49.jl2, m50_154.jl2):
--   * Aris' tent, Varrock Square: its doorway fai_varrock_tentdoor 3206,3424 is blockwalk=0 (an
--     open flap, no op): the goto lands outside it on the square and talk_to walks in.
--   * Varrock Palace: the courtyard 3212,3460 is street; the doorway 3212-3213,3470 has no door.
--     Sir Prysin's room (x 3201-3206 z 3469-3475) is shut by fai_varrock_castle_door 3207,3472.
--     The west corridor (x 3205) leads, door-free (reach.py), to the north-west tower's
--     fai_varrock_castle_door 3203,3493 and to the library corridor (z 3488) and the kitchen
--     door fai_varrock_castle_door 3217,3492.
--   * The tower: varrock_spiralstairs_taller 3202,3497 (maplink 0_50_54_3_40 -> 3204,3497,1),
--     varrock_spiralstairs_middle_taller op2 (no up row: [proc,climb] +1 plane on the tile),
--     Captain Rovin's floor (level 2); down: varrock_spiralstairstop (2_50_54_4_41 ->
--     3203,3496,1) and the middle op3 (1_50_54_4_41 -> 3203,3496,0).
--   * The kitchen stairs varrock_spiralstairs 3218,3496 (0_50_54_18_39 -> 3220,3496,1); the
--     bucket room (x 3221-3223 z 3495-3497, level 1) is shut by fai_varrock_castle_door
--     3221,3496,1; down: varrock_spiralstairstop (1_50_54_20_40 -> 3219,3495,0).
--   * The manhole 3237,3458 (manholes.rs2: Open, then Climb-down p_telejump +6400); the sewer
--     ladder fai_varrock_manhole_ladder 3237,9858 (blockwalk=0; maplink 0_50_154_37_2 ->
--     3236,3458).
--   * The Wizards' Tower: north door fai_wiztower_poor_door 3109,3167, the stair room's
--     diagonal door 3107,3162, fai_wiztower_spiralstairs 3103,3159 (0_48_49_33_24 ->
--     3104,3161,1), Traiborn's room (x 3110-3114 z 3160-3165, level 1) shut by
--     fai_wiztower_poor_door 3109,3162,1; down: the middle op3 (1_48_49_31_25 -> 3104,3161,0).
--   * Draynor bank: bankdoor_*_inactive 3092-3093,3246 is an open doorway.
-- Food: the pack is full until Traiborn takes the 25 bones (25 bones + key 2 + bucket + key 3 =
-- 28), so the fight's food is staged in the bank and drawn at Draynor on the way back.

return {
    id = "demon",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give coins 1",
        "::give bones 25",
        "::bankgive lobster 4",
        "::setlevel attack 45",
        "::setlevel strength 45",
        "::setlevel defence 45",
        "::setlevel hitpoints 60",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb2561_demonslayer_main",
            constants = {
                not_started = 0,
                talked_aris = 1,
                key_hunt = 2,
                complete = 3,
            },
            display = "Demon Slayer",
            points = 3,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local CASTLE_DOOR, CASTLE_DOOR_OPEN = "fai_varrock_castle_door", "fai_varrock_castle_door_open"
        local TOWER_DOOR, TOWER_DOOR_OPEN = "fai_wiztower_poor_door", "fai_wiztower_poor_door_open"

        -- Prysin's room, in and out through its door (west wall of 3207,3472).
        local function prysin_in(pfx)
            t.exec(pfx .. ".walkToPrysinDoor", t.player.walk_route, { { 3212, 3468 }, { 3208, 3472 }, { 3207, 3472 } })
            t.exec(pfx .. ".prysinDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3207, 3472, 0 }, near = { 3207, 3472 }, far = { 3205, 3472 } })
        end
        local function prysin_out(pfx)
            t.exec(pfx .. ".prysinDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3207, 3472, 0 }, near = { 3206, 3472 }, far = { 3208, 3472 } })
        end

        -- talkToAris: Aris in her tent, Varrock Square (costs a coin). The goto lands on the
        -- square outside the tent's open doorway.
        t.exec("goto-talkToAris", t.player.goto_tile, 3208, 3424, 0)
        t.exec("talkToAris", t.player.talk_to, "aris", 1)
        t.exec("talkToAris-dialog", t.chat.play, {
            "npc:Hello young one. Cross my palm",
            "choose:Ok, here you go.",
            "player:Ok, here you go.",
            "npc:Come closer",
            "npc:I can see images forming",
            "npc:very impressive looking sword",
            "npc:big dark shadow",
            "npc:Aaargh",
            "choose:Aaargh?",
            "player:Aaargh?",
            "npc:It's Delrith",
            "choose:Who's Delrith?",
            "player:Who's Delrith?",
            "npc:Delrith...",
            "npc:powerful demon",
            "npc:really hope he didn't see me",
            "npc:tried to destroy this city",
            "npc:Using his magic sword",
            "npc:Ye gods",
            "choose:Okay, where is he? I'll kill him for you!",
            "player:Okay, where is he",
            "npc:can't just go and fight",
            "npc:Wally managed",
            "npc:By reciting the correct magical incantation",
            "npc:Delrith will come forth",
            "npc:evil sorcerer",
            "choose:What is the magical incantation?",
            "player:What is the magical incantation?",
            "npc:let me think a second",
        })
        t.expect("quest.stage.talked_aris", t.quest.expect_stage("talked_aris"))
        -- the incantation is rolled per player: read its five words off the page
        local ok, txt = t.chat.text()
        local chant_words = {}
        if txt then
            local tail = txt:match("goes%.+%s*(.-)%.%s*Have") or ""
            for w in tail:gmatch("%a+") do chant_words[#chant_words + 1] = w end
        end
        t.check("aris.chant_words", #chant_words == 5, table.concat(chant_words, ","))
        t.exec("talkToAris-dialog2", t.chat.play, {
            "npc:Alright, I think I've got it",
            "player:I think so, yes.",
            "choose:Okay, thanks. I'll do my best to stop the demon.",
            "player:Okay, thanks",
            "npc:Good luck",
        })
        t.chat.close()
        t.expect("coin.paid", t.inv.expect_absent("coins"))

        -- talkToPrysin: south west corner of Varrock Castle, walked from the tent across the
        -- square and in through the palace's open doorway.
        t.exec("talkToPrysin.walkToPalace", t.player.walk_route, { { 3208, 3432 }, { 3210, 3438 }, { 3212, 3444 },
            { 3212, 3452 }, { 3212, 3460 } })
        prysin_in("talkToPrysin")
        t.exec("talkToPrysin", t.player.talk_to, "sir_prysin", 1)
        t.exec("talkToPrysin-dialog", t.chat.play, {
            "npc:Hello, who are you?",
            "choose:Gypsy Aris said I should come and talk to you.",
            "player:Gypsy Aris said",
            "npc:Gypsy Aris? Is she still alive?",
            "choose:I need to find Silverlight.",
            "player:I need to find Silverlight.",
            "npc:What do you need to find that for?",
            "player:I need it to fight Delrith.",
            "npc:Delrith? I thought the world",
            "choose:He's back and unfortunately I've got to deal with him.",
            "player:He's back",
            "npc:You don't look up to much",
            "npc:The problem is getting Silverlight.",
            "player:You mean you don't have it?",
            "npc:Oh I do have it",
            "choose:So give me the keys!",
            "player:So give me the keys!",
            "npc:Um, well it's not so easy.",
            "npc:I kept one of the keys",
            "npc:One I gave to Rovin",
            "npc:I gave the other to the wizard Traiborn",
            "choose:Where can I find Captain Rovin?",
            "player:Where can I find Captain Rovin?",
            "npc:Captain Rovin lives at the top",
            "choose:Well I'd better go key hunting.",
            "player:Well I'd better go key hunting.",
            "npc:Ok, goodbye.",
        })
        t.chat.close()
        t.expect("quest.stage.key_hunt", t.quest.expect_stage("key_hunt"))

        -- talkToRovin: out of Prysin's room, up the west corridor to the north-west tower's
        -- door, and up its two flights (goUpToRovin, goUpToRovin2) to his floor.
        prysin_out("talkToRovin")
        t.exec("talkToRovin.walkToTower", t.player.walk_route, { { 3207, 3476 }, { 3205, 3478 }, { 3205, 3486 },
            { 3203, 3491 }, { 3203, 3493 } })
        t.exec("talkToRovin.towerDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3203, 3493, 0 }, near = { 3203, 3493 }, far = { 3203, 3496 } })
        t.exec("goUpToRovin", t.player.climb, { loc = "varrock_spiralstairs_taller", op = 1, op_name = "Climb-up",
            at = { 3202, 3497, 0 }, src = { 3203, 3496 }, dest = { 3204, 3497, 1 } })
        t.exec("goUpToRovin2", t.player.climb, { loc = "varrock_spiralstairs_middle_taller", op = 2,
            op_name = "Climb-up", at = { 3202, 3497, 1 }, src = { 3204, 3497 }, dest = { 3204, 3497, 2 } })
        t.exec("talkToRovin", t.player.talk_to, "captain_rovin", 1)
        t.exec("talkToRovin-dialog", t.chat.play, {
            "npc:What are you doing up here?",
            "choose:Yes I know, but this is important.",
            "player:Yes, I know, but this is important.",
            "npc:Ok, I'm listening",
            "choose:There's a demon who wants to invade this city.",
            "player:There's a demon who wants to invade",
            "npc:Is it a powerful demon?",
            "player:Yes, very.",
            "npc:As good as the palace guards",
            "player:It's not them who are going to fight",
            "npc:What, all by yourself?",
            "player:I'm going to use the powerful sword Silverlight",
            "npc:Yes you are right. Here you go.",
        })
        t.exec("talkToRovin-key", t.inv.await, "silverlight_key_2", 1, 8)
        t.chat.close()

        -- Down the tower (goDownstairsFromRovin, goDownstairsFromRovin2), out of its door, along
        -- the library corridor and through the kitchen door.
        t.exec("goDownstairsFromRovin", t.player.climb, { loc = "varrock_spiralstairstop", op = 1,
            op_name = "Climb-down", at = { 3202, 3497, 2 }, src = { 3204, 3497 }, dest = { 3203, 3496, 1 } })
        t.exec("goDownstairsFromRovin2", t.player.climb, { loc = "varrock_spiralstairs_middle_taller", op = 3,
            op_name = "Climb-down", at = { 3202, 3497, 1 }, src = { 3204, 3497 }, dest = { 3203, 3496, 0 } })
        t.exec("talkToRovin.towerDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3203, 3493, 0 }, near = { 3203, 3494 }, far = { 3203, 3492 } })
        t.exec("goUpToBucket.walkToKitchen", t.player.walk_route, { { 3206, 3490 }, { 3207, 3488 }, { 3214, 3488 },
            { 3215, 3492 }, { 3217, 3492 } })
        t.exec("goUpToBucket.kitchenDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3217, 3492, 0 }, near = { 3217, 3492 }, far = { 3218, 3492 } })

        -- pickupBucket: the bucket above the kitchen, up the kitchen stairs (goUpToBucket) and
        -- through the bucket room's door.
        t.player.walk_to(3218, 3495, 10)
        t.exec("goUpToBucket", t.player.climb, { loc = "varrock_spiralstairs", op = 1, op_name = "Climb-up",
            at = { 3218, 3496, 0 }, src = { 3218, 3495 }, dest = { 3220, 3496, 1 } })
        t.exec("pickupBucket.roomDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3221, 3496, 1 }, near = { 3220, 3496 }, far = { 3222, 3496 } })
        t.exec("pickupBucket", t.player.click_obj, "bucket_empty", 3)
        t.exec("pickupBucket-inv", t.inv.await, "bucket_empty", 1, 8)
        t.exec("pickupBucket.roomDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3221, 3496, 1 }, near = { 3222, 3496 }, far = { 3220, 3496 } })

        -- fillBucket: the sink in the kitchen (goDownFromBucket is the stairs)
        t.exec("goDownFromBucket", t.player.climb, { loc = "varrock_spiralstairstop", op = 1, op_name = "Climb-down",
            at = { 3218, 3496, 1 }, src = { 3220, 3496 }, dest = { 3219, 3495, 0 } })
        t.exec("fillBucket", t.player.use_on, "bucket_empty", t.player.by_symbol("loc", "fai_varrock_posh_sink"))
        t.exec("fillBucket-inv", t.inv.await, "bucket_water", 1, 8)
        t.expect("fillBucket.emptyGone", t.inv.expect_absent("bucket_empty"))

        -- useFilledBucketOnDrain: pour the bucket of water down the drain (demon_slayer.rs2:
        -- the water leaves, an empty bucket comes back, the key is washed into the sewer).
        local dr = t.player.by_symbol("loc", "questdrain")
        t.exec("useFilledBucketOnDrain", t.player.use_on, "bucket_water", dr)
        t.exec("useFilledBucketOnDrain.var", t.var.await_server, "varb2568_delrith_drain_key", 1, 8)
        t.expect("useFilledBucketOnDrain.waterGone", t.inv.expect_absent("bucket_water"))
        t.exec("useFilledBucketOnDrain.bucketBack", t.inv.await, "bucket_empty", 1, 4)

        -- goDownManhole: out of the kitchen door, out of the palace by its doorway, round to
        -- the manhole south east of the palace (Quest Helper WorldPoint 3237,3458).
        t.exec("goDownManhole.kitchenDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3217, 3492, 0 }, near = { 3218, 3492 }, far = { 3216, 3492 } })
        t.exec("goDownManhole.walkOutOfPalace", t.player.walk_route, { { 3212, 3488 }, { 3206, 3486 },
            { 3206, 3478 }, { 3211, 3475 }, { 3212, 3468 }, { 3212, 3460 } })
        t.exec("goDownManhole.walkToManhole", t.player.walk_route, { { 3215, 3465 }, { 3223, 3465 },
            { 3231, 3465 }, { 3236, 3462 }, { 3236, 3458 } })
        t.exec("goDownManhole.open", t.player.click_loc, "manholeclosed", 1, { at = { 3237, 3458, 0 } })
        t.ticks(2)
        local mr, md = t.world.loc_near("manholeopen", 3, { at = { 3237, 3458, 0 } })
        t.check("goDownManhole.opened", mr == "ok",
            "manholeopen at 3237,3458,0 after Open -> " .. tostring(mr)
                .. (mr == "ok" and (" at " .. md.tile_x .. "," .. md.tile_z .. "," .. md.level) or (" " .. tostring(md))))
        -- manholeopen is forceapproach=23 (all.loc): the player climbs from its west side, and
        -- p_telejump(movecoord(coord, 0, 0, 6400)) lands him straight below that tile.
        t.exec("goDownManhole", t.player.cross_trap, { loc = "manholeopen", op_name = "Climb-down",
            at = { 3237, 3458, 0 }, src = { 3236, 3458 }, dest = { 3236, 9858 } })

        -- pickupSecondKey: north through the sewer to the key at the drain pipe.
        t.exec("pickupSecondKey.walk", t.player.walk_route, { { 3236, 9862 }, { 3231, 9865 }, { 3229, 9871 },
            { 3230, 9878 }, { 3231, 9885 }, { 3231, 9893 }, { 3228, 9898 }, { 3226, 9897 } })
        t.exec("pickupSecondKey", t.player.click_loc, "qip_ds_sewer_key", 1)
        t.exec("pickupSecondKey.inv", t.inv.await, "silverlight_key_3", 1, 8)

        -- goUpManhole: back to the ladder and up it.
        t.exec("goUpManhole.walk", t.player.walk_route, { { 3231, 9896 }, { 3231, 9888 }, { 3231, 9880 },
            { 3229, 9874 }, { 3232, 9869 }, { 3237, 9866 }, { 3237, 9858 } })
        t.exec("goUpManhole", t.player.cross_trap, { loc = "fai_varrock_manhole_ladder", op_name = "Climb-up",
            at = { 3237, 9858, 0 }, src = { 3237, 9858 }, dest = { 3236, 3458 } })

        -- talkToTraiborn: overland to the Wizards' Tower (open ground), in through its north
        -- door and the stair room's door, up the stairs (goUpstairsWizard), into his room.
        t.exec("goto-traiborn", t.player.goto_tile, 3109, 3169, 0)
        t.exec("talkToTraiborn.towerIn", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
            at = { 3109, 3167, 0 }, near = { 3109, 3168 }, far = { 3109, 3164 } })
        t.exec("talkToTraiborn.stairRoomIn", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
            at = { 3107, 3162, 0 }, near = { 3108, 3163 }, far = { 3105, 3162 } })
        t.exec("goUpstairsWizard", t.player.climb, { loc = "fai_wiztower_spiralstairs", op = 1, op_name = "Climb-up",
            at = { 3103, 3159, 0 }, src = { 3105, 3160 }, dest = { 3104, 3161, 1 } })
        t.exec("talkToTraiborn.roomDoorIn", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
            at = { 3109, 3162, 1 }, near = { 3109, 3162 }, far = { 3111, 3162 } })
        t.exec("talkToTraiborn", t.player.talk_to, "traiborn", 1)
        t.exec("traiborn.dialog", t.chat.play, {
            "npc:Ello young thingummywut.",
            "choose:I need to get a key given to you by Sir Prysin.",
            "player:I need to get a key",
            "npc:Sir Prysin? Who's that?",
            "choose:Well, have you got any keys knocking around?",
            "player:Well, have you got any keys knocking around?",
            "npc:Now you come to mention it",
            "npc:I sealed it using one of my magic rituals",
            "player:So do you know what ritual to use?",
            "npc:Let me think a second.",
            "npc:Yes a simple drazier",
            "choose:I'll get the bones for you.",
            "player:I'll help get the bones",
            "npc:Ooh that would be very good",
            "player:Okay, I'll speak to you",
        })
        t.chat.close()
        t.exec("traiborn.talk2", t.player.talk_to, "traiborn", 1)
        t.exec("traiborn.dialog2", t.chat.play, {
            "npc:How are you doing finding bones?",
            "player:I have some bones.",
            "npc:Give 'em here then.",
        })
        t.exec("traiborn.bones", t.var.await_server, "varp7143_demon_bones_given", 25, 60)
        t.exec("traiborn.ritual", t.chat.play, {
            "npc:Hurrah! That's all 25 sets of bones.",
            "mesbox:Traiborn places the bones in a circle",
            "mesbox:Traiborn waves his arms about",
            "npc:Wings of dark and colour too",
            "mesbox:The wizard waves his arms some more",
        })
        t.exec("traiborn.key", t.inv.await, "silverlight_key_1", 1, 12)
        t.ticks(4)
        t.exec("traiborn.thanks", t.chat.play, {
            "player:Thank you very much.",
            "npc:Not a problem for a friend",
        })
        t.chat.close()
        t.expect("bones.consumed", t.inv.expect_absent("bones"))

        -- Out of his room, down the stairs, out of the stair room and the tower on foot.
        t.exec("traiborn.roomDoorOut", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
            at = { 3109, 3162, 1 }, near = { 3110, 3162 }, far = { 3108, 3162 } })
        t.exec("traiborn.stairsDown", t.player.climb, { loc = "fai_wiztower_spiralstairs_middle", op = 3,
            op_name = "Climb-down", at = { 3103, 3159, 1 }, src = { 3103, 3161 }, dest = { 3104, 3161, 0 } })
        t.exec("traiborn.stairRoomOut", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
            at = { 3107, 3162, 0 }, near = { 3105, 3162 }, far = { 3108, 3163 } })
        t.exec("traiborn.towerOut", t.player.pass_door, { closed = TOWER_DOOR, open = TOWER_DOOR_OPEN,
            at = { 3109, 3167, 0 }, near = { 3109, 3165 }, far = { 3109, 3169 } })

        -- Food for Delrith, drawn at Draynor bank (open doorway) now the bones have left the pack.
        t.exec("goto-draynorBank", t.player.goto_tile, 3097, 3246, 0)
        t.exec("food.bankOpen", t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
        t.exec("food.withdraw.lobster", t.bank.withdraw, "lobster", 4)
        t.check("food.bankClose", t.bank.close())
        t.exec("food.walkOut", t.player.walk_route, { { 3094, 3246 }, { 3097, 3246 } })

        -- returnToPrysin: back to the palace courtyard (open street), in through his door.
        t.exec("goto-prysin2", t.player.goto_tile, 3212, 3460, 0)
        prysin_in("returnToPrysin")
        t.exec("returnToPrysin", t.player.talk_to, "sir_prysin", 1)
        t.exec("prysin2.dialog", t.chat.play, {
            "npc:So how are you doing with getting the keys?",
            "player:I've got all three keys!",
            "npc:Excellent! Now I can give you Silverlight.",
        })
        t.exec("silverlight.got", t.inv.await, "silverlight", 1, 12)
        t.expect("keys.gone", t.inv.expect_absent("silverlight_key_1"))
        t.expect("keys.gone2", t.inv.expect_absent("silverlight_key_2"))
        t.expect("keys.gone3", t.inv.expect_absent("silverlight_key_3"))
        t.exec("case.var", t.var.await_server, "varb2567_delrith_silverlight_case", 1, 5)
        t.chat.close()

        -- ================= BOSS =================
        t.exec("equip.silverlight", t.player.equip, "silverlight")
        prysin_out("killDelrith")
        t.exec("killDelrith.walkOutOfPalace", t.player.walk_route, { { 3212, 3468 }, { 3212, 3460 } })
        t.exec("goto-circle-edge", t.player.goto_tile, 3221, 3366, 0)
        t.player.walk_to(3226, 3366)
        t.ticks(4)
        t.exec("summon.mesbox", t.chat.play, { "mesbox:The dark wizards complete their ritual" })
        t.chat.close()

        -- Each round of the fight is sampled every tick: the lowest hitpoints seen, and a
        -- lobster eaten below EAT_BELOW. The margin row reads both.
        local EAT_BELOW = 30
        local hp_low, hp_base, eaten = nil, nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                hp_base = hp.base_level
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW and t.player.inv_op("lobster", 1) == "ok" then
                    eaten = eaten + 1
                    t.ticks(1)
                end
            end
        end
        local function fight_until_weakened(name)
            hp_low = nil
            vitals()
            t.exec(name .. ".attack", t.player.attack, "delrith", 2, 15)
            for _ = 1, 60 do
                vitals()
                if t.npc.nearest("delrith_weakened", 20) == "ok" then
                    break
                end
                t.ticks(1)
            end
            t.exec(name .. ".weakened", t.npc.await_present, "delrith_weakened", 20, 5)
            vitals()
            local fr, food = t.inv.count("lobster")
            t.check(name .. ".margin", hp_low ~= nil and hp_base ~= nil and hp_low * 4 >= hp_base
                    and fr == "ok" and food >= 1,
                "Delrith (level 27) with Silverlight: lowest hp " .. tostring(hp_low) .. "/" .. tostring(hp_base)
                    .. " (sampled every tick), lobsters eaten so far " .. eaten .. ", left " .. tostring(food)
                    .. " (" .. tostring(fr) .. ") of 4 drawn (margin: lowest hp >= a quarter of max AND food left)")
        end

        t.exec("delrith.present", t.npc.await_present, "delrith", 12, 12)
        fight_until_weakened("killDelrithStep")
        t.ticks(2)
        t.exec("banish.press", t.player.press, "delrith_weakened", 1, 8)
        -- wrong incantation first: fixed alphabetical order, unless it happens to be the real one
        local wrong = { "Aber", "Camerinthum", "Carlem", "Gabindo", "Purchai" }
        local same = true
        for i = 1, 5 do if wrong[i] ~= chant_words[i] then same = false end end
        if same then wrong = { "Purchai", "Gabindo", "Carlem", "Camerinthum", "Aber" } end
        local pages = { "player:Now what was that incantation again?" }
        for i = 1, 5 do pages[#pages + 1] = "choose:" .. wrong[i] end
        pages[#pages + 1] = "player:" .. table.concat(wrong, " ")
        pages[#pages + 1] = "mesbox:As you chant, Delrith is sucked"
        pages[#pages + 1] = "mesbox:The vortex collapses"
        t.exec("chant.wrong", t.chat.play, pages)
        t.chat.close()
        t.expect("wrong.stage_unchanged", t.quest.expect_stage("key_hunt"))
        t.exec("delrith.restored", t.npc.await_present, "delrith", 12, 12)
        fight_until_weakened("delrith2")
        t.ticks(2)
        t.exec("banish2.press", t.player.press, "delrith_weakened", 1, 8)
        local rpages = { "player:Now what was that incantation again?" }
        for i = 1, 5 do rpages[#rpages + 1] = "choose:" .. chant_words[i] end
        rpages[#rpages + 1] = "player:" .. table.concat(chant_words, " ")
        rpages[#rpages + 1] = "mesbox:Delrith is sucked into the vortex"
        rpages[#rpages + 1] = "mesbox:back into the dark dimension"
        t.exec("chant.right", t.chat.play, rpages)
        t.chat.close()
        t.ticks(6)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
