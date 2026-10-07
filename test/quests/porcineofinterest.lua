-- A Porcine of Interest: driven from the guide ladder (porcineofinterest.notes.md) and the b52 parity script.
-- Goggles come from Spria (stage 25); rope + knife + a slash weapon are brought along.
return {
    id = "porcineofinterest",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give rope 1",
        "::give knife 1",
        "::give bronze_scimitar 1",
        "::give lobster 10",
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel hitpoints 80",
    },

    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb10582_porcine",
            constants = { not_started = 0, sarah = 5, rope = 10, cave = 15, spria = 20, kill = 25, foot = 30, finish = 35, complete = 40 },
            display = "A Porcine of Interest",
            points = 1,
        })
        t.step("quest.bind", br == "ok" and "PASS" or "FAIL", bd)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-readNotice", t.player.goto_tile, 3086, 3251, 0)
        t.exec("readNotice", t.player.click_loc, "porcine_noticeboard", 1)
        t.exec("readNotice-dialog", t.chat.play, {
            "player:'Damaged roof tiles for sale'",
            "player:Wait a minute",
            "player:This could be worth looking into.",
            "choose:Yes.",
            "player:Right, let's go and see what Sarah has in mind.",
        })
        t.ticks(2)
        t.expect("quest.stage.sarah", t.quest.expect_stage("sarah"))

        t.exec("goto-talkToSarah", t.player.goto_tile, 3033, 3293, 0)
        t.exec("talkToSarah", t.player.talk_to, "farming_shopkeeper_1", 1)
        t.exec("talkToSarah-dialog", t.chat.play, {
            "npc:Hello. How can I help you?",
            "choose:Talk about the bounty.",
            "player:I've come about the bounty.",
            "npc:Oh thank Saradomin!",
            "player:Can you explain what happened?",
            "npc:Of course.",
            "npc:Suddenly, out of the trees",
            "player:Sounds like it must have given you quite the scare!",
            "npc:It certainly did.",
            "npc:A good place to start",
            "player:I think that'll be all for now.",
            "npc:Excellent!",
        })
        t.ticks(2)
        t.expect("quest.stage.rope", t.quest.expect_stage("rope"))

        -- The tracking trail (wiki walkthrough oldid 15272507: "investigate the cart there. Then, follow
        -- the trail of farming produce and broken dead trees north-east until you reach a strange hole";
        -- transcript oldid 15107528 'Investigating the crossroads'). Placements from maps/m48_51.jl2 and
        -- m49_52.jl2: the damaged cart 3120,3300, produce and broken trees north-east to the hole 3150,3347.
        t.exec("goto-crossroads", t.player.goto_tile, 3121, 3303, 0)
        t.exec("trackCart", t.player.click_loc, "porcine_tracking_cart", 1)
        t.exec("trackCart-dialog", t.chat.play, {
            "mesbox:It seems as though the monster was trying to run off with some of Sarah's produce.",
        })
        t.exec("trackCabbage", t.player.click_loc, "porcine_tracking_cabbage", 1, { at = { 3124, 3302 } })
        t.exec("trackCabbage-dialog", t.chat.play, { "mesbox:The cabbage is damaged and has begun to yellow." })
        t.exec("trackPotatoes", t.player.click_loc, "porcine_tracking_potatoes", 1, { at = { 3123, 3301 } })
        t.exec("trackPotatoes-dialog", t.chat.play, {
            "mesbox:These potatoes seem to have been largely untouched.",
            "player:Would you look at that... Apparently our monster is picky.",
        })
        t.exec("trackCarrot1", t.player.click_loc, "porcine_tracking_carrot", 1, { at = { 3124, 3305 } })
        t.exec("trackCarrot1-dialog", t.chat.play, {
            "mesbox:The carrot has been slightly gnawed on.",
            "player:Weird... What sort of monster charges at someone",
        })
        t.exec("trackTree1", t.player.click_loc, "porcine_tracking_tree", 1, { at = { 3133, 3317 } })
        t.exec("trackTree1-dialog", t.chat.play, { "player:Something pretty big must have come running through here" })
        t.exec("trackCarrot2", t.player.click_loc, "porcine_tracking_carrot", 1, { at = { 3135, 3323 } })
        t.exec("trackCarrot2-dialog", t.chat.play, { "mesbox:The carrot has been slightly gnawed on.", "player:Weird..." })
        t.exec("trackTree2", t.player.click_loc, "porcine_tracking_tree", 1, { at = { 3139, 3331 } })
        t.exec("trackTree2-dialog", t.chat.play, { "player:Something pretty big must have come running through here" })
        t.exec("walk-trackTree3", t.player.walk_to, 3141, 3338, 20)
        t.exec("trackTree3", t.player.click_loc, "porcine_tracking_tree", 1, { at = { 3142, 3340 } })
        t.exec("trackTree3-dialog", t.chat.play, { "player:Something pretty big must have come running through here" })
        t.exec("walk-trackTree4", t.player.walk_to, 3146, 3342, 20)
        t.exec("trackTree4", t.player.click_loc, "porcine_tracking_tree", 1, { at = { 3148, 3344 } })
        t.exec("trackTree4-dialog", t.chat.play, { "player:Something pretty big must have come running through here" })
        -- The hole before the rope: transcript 'Strange hole', the no-rope reading is the Investigate op's
        -- (porcineofinterest_locs.rs2 [oploc1,porcine_hole_norope]).
        local hole = t.player.by_symbol("loc", "porcine_hole")
        t.exec("useRopeOnHole", t.player.use_on, "rope", hole)
        t.ticks(3)
        t.expect("quest.stage.cave", t.quest.expect_stage("cave"))
        t.exec("rope.consumed", t.inv.expect_absent, "rope")

        t.exec("enterHole", t.player.click_loc, "porcine_hole", 1)
        t.ticks(4)
        local tr, tl = t.world.tile()
        t.check("enterHole.underground", tr == "ok" and tl.z > 9600, "tile " .. tostring(tl and (tl.x .. "," .. tl.z)))
        -- Transcript 'Investigating pile of rope' (porcine_fallen_rope, 3159,9713, beside the climb-down).
        t.exec("investigateRopePile", t.player.click_loc, "porcine_fallen_rope", 1)
        t.exec("investigateRopePile-dialog", t.chat.play, { "player:Huh... there's a rope already down here." })

        t.exec("goto-blockage", t.player.goto_tile, 3157, 9707, 0)
        t.exec("blockage.south", t.player.click_loc, "porcine_cave_blockage", 1)
        t.ticks(4)
        tr, tl = t.world.tile()
        t.check("blockage.crossed", tr == "ok" and tl.z < 9704, "tile " .. tostring(tl and (tl.x .. "," .. tl.z)))

        t.exec("goto-skeleton", t.player.goto_tile, 3164, 9677, 0)
        t.exec("investigateSkeleton", t.player.click_loc, "porcine_skeleton", 1)
        -- Transcript 'Investigating the skeleton' (oldid 15107528), verbatim; the Pig Thing cutscene
        -- (porcineofinterest_locs.rs2 [proc,poi_pig_thing_cutscene]) starts when the last page closes.
        local pig_mark = t.cutscene.mark()
        t.exec("investigateSkeleton-dialog", t.chat.play, {
            "mesbox:The skeleton seems fresh.",
            "player:There's also a scrawled note.",
            "mesbox:'This is what I get for exploring uncharted caves.",
            "player:Well, that's not exactly reassuring.",
        })
        t.ticks(4)
        local pig_cam = t.world.camera()
        t.check("pigThing.shot1", pig_cam ~= nil and pig_cam.server_driven == true,
            "camera as the Pig Thing appears: eye " .. tostring(pig_cam and pig_cam.x) .. "," .. tostring(pig_cam and pig_cam.z)
                .. " last_op " .. tostring(pig_cam and pig_cam.last_op))
        local pr, pd = t.npc.await_present("porcine_sourhog_cutscene", 10)
        t.check("pigThing.appears", pr == "ok", tostring(pr) .. " " .. tostring(pd))
        t.exec("pigThing-dialog1", t.chat.play, { "player:Uhh... Nice piggy?" })
        t.ticks(8)
        t.exec("pigThing-dialog2", t.chat.play, { "mesbox:Some time passes..." })
        t.ticks(10)
        t.exec("pigThing-dialog3", t.chat.play, { "player:..." })
        t.ticks(2)
        local sr, sd = t.npc.await_present("porcine_spria_cutscene", 10)
        t.check("pigThing.spriaEnters", sr == "ok", tostring(sr) .. " " .. tostring(sd))
        t.exec("pigThing-dialog4", t.chat.play, { "npc:Well then, what do we have here?" })
        t.ticks(3)
        t.exec("pigThing-dialog5", t.chat.play, {
            "npc:Still breathing, eh...?",
            "npc:Seems I came at just the right time.",
            "npc:We'd better get you out of here before that thing returns.",
        })
        t.ticks(6)
        t.exec("pigThing-wake", t.chat.play, { "player:W-what happened? Where am I?" })
        t.exec("pigThing.cutscene", t.cutscene.await, "pigThing", { since = pig_mark, expect = {
            { op = "moveto", coord = "0_49_151_31_10", height = 450 },  -- porcineofinterest_locs.rs2, copied verbatim
            { op = "lookat", coord = "0_49_151_28_14", height = 150 },
            { op = "moveto", coord = "0_49_151_30_11", height = 700 },  -- shot 2, after the blackout
            { op = "lookat", coord = "0_49_151_28_13", height = 0 },
            { op = "moveto", coord = "0_49_151_31_12", height = 400 },  -- shot 3, Spria enters
            { op = "lookat", coord = "0_49_151_29_15", height = 150 },
            { op = "moveto", coord = "0_49_151_31_13", height = 350 },  -- glide (2, 1)
            { op = "reset" },
        } })
        t.ticks(2)
        t.expect("quest.stage.spria", t.quest.expect_stage("spria"))
        -- "The player wakes up in Spria's house in Draynor Village": no walk or goto to her.
        tr, tl = t.world.tile()
        t.check("pigThing.wokeAtSpria", tr == "ok" and tl.x == 3092 and tl.z == 3266 and tl.level == 0,
            "woke at " .. tostring(tl and (tl.x .. "," .. tl.z .. "," .. tostring(tl.level))) .. " want 3092,3266,0")

        t.exec("talkToSpria", t.player.talk_to, "porcine_spria", 1)
        t.exec("talkToSpria-dialog", t.chat.play, {
            "npc:Oh, you're awake!", "player:Who are you?", "npc:My name's Spria", "player:A Sourhog?",
            "npc:Precisely.", "npc:Were you responding", "player:Yes...", "npc:I thought as much.",
            "npc:The beast's saliva", "player:So how am I supposed", "npc:Luckily,", "npc:This eyewear",
            "npc:Go now,", "npc:If you make it out alive",
        })
        t.ticks(2)
        t.expect("quest.stage.kill", t.quest.expect_stage("kill"))
        t.exec("goggles.have", t.inv.expect_has, "slayer_reinforced_goggles", 1)

        t.exec("goto-enterHoleAgain", t.player.goto_tile, 3149, 3346, 0)
        t.exec("equip.goggles", t.player.equip, "slayer_reinforced_goggles")
        t.exec("equip.scimitar", t.player.equip, "bronze_scimitar")
        t.exec("enterHoleAgain", t.player.click_loc, "porcine_hole", 1)
        t.ticks(4)
        t.exec("goto-blockage2", t.player.goto_tile, 3157, 9707, 0)
        t.exec("blockage.warn", t.player.click_loc, "porcine_cave_blockage", 1)
        t.exec("blockage.warn-dialog", t.chat.play, {
            "player:I don't think Spria will be rescuing me this time",
            "choose:Yes",
        })
        t.ticks(3)
        local nr, nd = t.npc.await_present("porcine_sourhog_second", 10)
        t.check("sourhog.present", nr == "ok", tostring(nr) .. " " .. tostring(nd))
        t.exec("killSourhog", t.player.attack, "porcine_sourhog_second", 2, 15)
        -- The quest Sourhog (level 37, crush melee; its acid spit is stopped by the worn goggles,
        -- porcineofinterest_locs.rs2 [ai_opplayer2,porcine_sourhog_second]). No dialogue in
        -- quest_porcineofinterest branches on combat level (grep stat/combat), so the setup's
        -- attack/strength 60 and hitpoints 80 are staging for a margin only.
        local _, lobsters_before = t.inv.count("lobster")
        local _, sourhog_detail = t.exec("killSourhog.dead", t.npc.await_dead_engaged, 300, 6, { eat = { item = "lobster", below = 35 } })
        local lowest = tonumber(tostring(sourhog_detail):match("lowest hp (%d+)/"))
        local _, hitpoints = t.skill.read("hitpoints")
        local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
        local lobster_result, lobsters_left = t.inv.count("lobster")
        t.check("killSourhog.margin", lowest ~= nil and max_hp ~= nil and lobster_result == "ok"
            and lowest * 4 >= max_hp and lobsters_left >= 1,
            "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", lobsters "
            .. tostring(lobsters_before) .. " -> " .. tostring(lobsters_left)
            .. " (margin: lowest hp >= a quarter of max AND at least one lobster left)")
        t.ticks(3)
        t.expect("quest.stage.foot", t.quest.expect_stage("foot"))

        t.exec("goto-corpse", t.player.goto_tile, 3157, 9700, 0)
        t.exec("cutOffFoot", t.player.click_loc, "porcine_dead_sourhog", 1)
        t.exec("cutOffFoot.have", t.inv.await, "porcine_sourhog_trophy", 1, 5)

        t.exec("goto-blockage3", t.player.goto_tile, 3157, 9703, 0)
        t.exec("blockage.north", t.player.click_loc, "porcine_cave_blockage", 1)
        t.ticks(4)
        t.exec("goto-exit", t.player.goto_tile, 3157, 9712, 0)
        t.exec("exit.rope", t.player.click_loc, "porcine_cave_exit_rope", 1)
        t.ticks(4)
        tr, tl = t.world.tile()
        t.check("exit.surface", tr == "ok" and tl.z < 9000, "tile " .. tostring(tl and (tl.x .. "," .. tl.z)))

        local _, coins_before = t.inv.count("coins")
        t.exec("goto-returnToSarah", t.player.goto_tile, 3033, 3293, 0)
        t.exec("returnToSarah", t.player.talk_to, "farming_shopkeeper_1", 1)
        t.exec("returnToSarah-dialog", t.chat.play, {
            "npc:Hello. How can I help you?", "choose:Talk about the bounty.",
            "player:That monster certainly", "npc:Oh that's fantastic", "npc:Wait, how do I know",
            "player:How about this as proof?", "npc:Eugh!", "player:Indeed", "npc:I'll probably just give it",
            "npc:Anyway, thank you", "player:Perhaps I should speak with Spria",
        })
        t.ticks(2)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", (coins_after or 0) - (coins_before or 0) == 5000, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " want +5000")

        t.exec("goto-returnToSpria", t.player.goto_tile, 3092, 3266, 0)
        local snap_result, snap = t.skill.snapshot()
        t.check("slayer.snapshot", snap_result, "skill.snapshot before hand-in -> " .. tostring(snap_result))
        t.exec("returnToSpria", t.player.talk_to, "porcine_spria", 1)
        t.exec("returnToSpria-dialog", t.chat.play, {
            "npc:'Ello, and what are you after then?", "player:I did it!", "npc:Very impressive",
            "npc:Although,", "npc:We may have to monitor",
        })
        t.quest.expect_complete()
        local xr, xd = t.skill.expect_gain("slayer", 1000, snap)
        t.check("reward.slayer_xp", xr, tostring(xd))
        t.finish(0)
    end,
}
