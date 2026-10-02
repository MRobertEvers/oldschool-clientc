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

        t.exec("goto-useRopeOnHole", t.player.goto_tile, 3149, 3345, 0)
        local hole = t.player.by_symbol("loc", "porcine_hole")
        t.exec("useRopeOnHole", t.player.use_on, "rope", hole)
        t.ticks(3)
        t.expect("quest.stage.cave", t.quest.expect_stage("cave"))
        t.exec("rope.consumed", t.inv.expect_absent, "rope")

        t.exec("enterHole", t.player.click_loc, "porcine_hole", 1)
        t.ticks(4)
        local tr, tl = t.world.tile()
        t.check("enterHole.underground", tr == "ok" and tl.z > 9600, "tile " .. tostring(tl and (tl.x .. "," .. tl.z)))

        t.exec("goto-blockage", t.player.goto_tile, 3157, 9707, 0)
        t.exec("blockage.south", t.player.click_loc, "porcine_cave_blockage", 1)
        t.ticks(4)
        tr, tl = t.world.tile()
        t.check("blockage.crossed", tr == "ok" and tl.z < 9704, "tile " .. tostring(tl and (tl.x .. "," .. tl.z)))

        t.exec("goto-skeleton", t.player.goto_tile, 3163, 9678, 0)
        t.exec("investigateSkeleton", t.player.click_loc, "porcine_skeleton", 1)
        t.exec("investigateSkeleton-dialog", t.chat.play, {
            "player:There's also a scrawled note.",
            "player:Well, that's not exactly reassuring.",
            "player:Uhh... Nice piggy?",
            "player:Argh! My eyes!",
        })
        t.ticks(4)
        t.expect("quest.stage.spria", t.quest.expect_stage("spria"))

        t.exec("goto-talkToSpria", t.player.goto_tile, 3092, 3265, 0)
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

        t.exec("goto-enterHoleAgain", t.player.goto_tile, 3149, 3345, 0)
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
        t.exec("killSourhog.dead", t.npc.await_dead_engaged, 300, 6, { eat = { item = "lobster", below = 35 } })
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

        t.exec("goto-returnToSpria", t.player.goto_tile, 3092, 3265, 0)
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
