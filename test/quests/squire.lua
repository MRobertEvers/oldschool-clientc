-- Knight's Sword quest test
return {
    id = "squire",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give redberry_pie 1",
        "::give iron_bar 2",
        "::setlevel mining 10",
    },

    run = function(t)
        t.quest.bind({
            varp = "squire",
            constants = {
                complete = 7,
                given_pie = 3,
                looking_blurite = 6,
                looking_portrait = 5,
                not_started = 0,
                questpoints = 1,
                spoken_reldo = 2,
                spoken_thurgo = 4,
                started = 1,
            },
            row = "quest_theknightssword",
            display = "The Knight's Sword",
            points = 1,
        })

        t.ticks(3)
        t.expect("reset", t.quest.expect_stage("not_started"))

        -- Squire: accept quest
        t.exec("sq.goto1", t.player.goto_tile, 2978, 3341, 0)
        t.exec("sq.talk1", t.player.talk_to, "squire", 1)

        local dr1, _ = t.chat.drain({ stop_at = "options" })
        t.expect("sq1.op1", dr1, "ok")
        t.exec("sq.ch1", t.chat.choose, "And how is life as a squire?")

        local dr2, _ = t.chat.drain({ stop_at = "options" })
        t.expect("sq1.op2", dr2, "ok")
        t.exec("sq.ch2", t.chat.choose, "I can make a new sword if you like...")

        local dr3, _ = t.chat.drain({ stop_at = "options" })
        t.expect("sq1.op3", dr3, "ok")
        t.exec("sq.ch3", t.chat.choose, "So would these dwarves make another one?")

        local dr4, _ = t.chat.drain({ stop_at = "options" })
        t.expect("sq1.op4", dr4, "ok")
        t.exec("sq.accept", t.chat.choose, "Ok, I'll give it a go.")

        local dr5, _ = t.chat.drain({ stop_at = "none" })
        t.expect("sq1.done", dr5, "ok")
        t.expect("sq.started", t.quest.expect_stage("started"))

        -- Reldo
        t.exec("reldo.goto", t.player.goto_tile, 3211, 3494, 0)
        t.exec("reldo.talk", t.player.talk_to, "reldo_normal", 1)
        local rdr1, _ = t.chat.drain({ stop_at = "options" })
        t.expect("reldo.op", rdr1, "ok")
        t.exec("reldo.ch", t.chat.choose, "What do you know about the Imcando dwarves?")
        local rdr2, _ = t.chat.drain({ stop_at = "none" })
        t.expect("reldo.done", rdr2, "ok")
        t.expect("sq.reldo", t.quest.expect_stage("spoken_reldo"))

        -- Thurgo: give pie (stage 2 -> 3)
        t.exec("thurgo.goto1", t.player.goto_tile, 3000, 3145, 0)
        t.exec("thurgo.talk1", t.player.talk_to, "thurgo", 1)
        local tdr1, _ = t.chat.drain({ stop_at = "options" })
        t.expect("thurgo1.op", tdr1, "ok")
        t.exec("thurgo.pie", t.chat.choose, "Would you like a redberry pie?")
        local tdr2, _ = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo1.done", tdr2, "ok")
        t.expect("sq.pie", t.quest.expect_stage("given_pie"))

        -- Thurgo: discuss sword (stage 3 -> 4, no options, auto-close)
        t.exec("thurgo.talk2", t.player.talk_to, "thurgo", 1)
        local tdr3, _ = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo2.done", tdr3, "ok")
        t.expect("sq.thurgo", t.quest.expect_stage("spoken_thurgo"))

        -- Now stage is 4 (spoken_thurgo), need to get portrait
        -- Squire status update
        t.exec("sq.goto2", t.player.goto_tile, 2978, 3341, 0)
        t.exec("sq.talk2", t.player.talk_to, "squire", 1)
        local sq2dr, _ = t.chat.drain({ stop_at = "none" })
        t.expect("sq2.done", sq2dr, "ok")
        t.expect("sq.portrait", t.quest.expect_stage("looking_portrait"))

        -- Get portrait (cheat to acquire it)
        t.cheat("::give knights_portrait 1")
        t.ticks(1)

        -- Thurgo: give portrait (stage 5 -> 6)
        t.exec("thurgo.goto3", t.player.goto_tile, 3000, 3145, 0)
        t.exec("thurgo.talk3", t.player.talk_to, "thurgo", 1)
        local tdr4, _ = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo3.done", tdr4, "ok")
        t.expect("sq.blurite", t.quest.expect_stage("looking_blurite"))

        -- Get blurite ore (we already have iron bars from setup)
        t.cheat("::give blurite_ore 1")
        t.ticks(1)

        -- Thurgo: give materials, get sword (stage 6 -> complete when we hand to squire)
        t.exec("thurgo.talk4", t.player.talk_to, "thurgo", 1)
        local tdr5, _ = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo4.done", tdr5, "ok")

        -- Squire: final handoff (complete -> quest complete callback)
        t.exec("sq.goto3", t.player.goto_tile, 2978, 3341, 0)
        t.exec("sq.final", t.player.talk_to, "squire", 1)

        local snap_r, snap_b = t.skill.snapshot()
        t.step("snap", snap_r == "ok" and "PASS" or "FAIL", "snapshot=" .. tostring(snap_r))

        local sq3dr, _ = t.chat.drain({ stop_at = "none" })
        t.expect("sq.final", sq3dr, "ok")

        -- Quest completion is asynchronous; wait for the callback
        t.ticks(3)

        t.quest.expect_complete()
        t.check("reward", t.skill.expect_gain("smithing", 12725, snap_b))

        t.finish(0)
    end,
}
