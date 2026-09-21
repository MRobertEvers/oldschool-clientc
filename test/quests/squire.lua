-- The Knight's Sword: Squire (Falador courtyard) accepts, sends the player
-- to Reldo (Varrock library) for the Imcando dwarf lead, then to Thurgo
-- (south of Port Sarim) with a redberry pie. Thurgo agrees to smith a
-- replacement sword but needs to see the design first -- a portrait kept
-- in Sir Vyvin's cupboard, upper floor of Falador Castle. Thurgo then
-- needs two iron bars (brought along) and a blurite ore, mined in the
-- eastern cavern under the ice-giant cliff. Handing the finished sword to
-- the Squire completes the quest.
--
-- Every gather step below is driven through real clicks against the
-- quest's own scripts (OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_squire/scripts/quest_squire.rs2, areas/falador/scripts/
-- squire.rs2, areas/varrock/scripts/reldo.rs2, areas/port_sarim/scripts/
-- thurgo.rs2) -- redberry pie, both iron bars and a pickaxe are the only
-- setup ::give's (Quest Helper's own getItemRequirements() -- a tool and
-- two brought-along items, never the quest's own deliverable).
return {
    id = "squire",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give redberry_pie 1",
        "::give iron_bar 2",
        "::give bronze_pickaxe 1",
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
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Squire (Falador courtyard): accept the quest.
        t.exec("goto.squire", t.player.goto_tile, 2977, 3342, 0)
        t.exec("squire.talk", t.player.talk_to, "squire", 1)

        local d1r, d1d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu1", d1r, d1d)
        t.exec("squire.choose1", t.chat.choose, "And how is life as a squire?")

        local d2r, d2d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu2", d2r, d2d)
        t.exec("squire.choose2", t.chat.choose, "I can make a new sword if you like...")

        local d3r, d3d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu3", d3r, d3d)
        t.exec("squire.choose3", t.chat.choose, "So would these dwarves make another one?")

        local d4r, d4d = t.chat.drain({ stop_at = "options" })
        t.expect("squire.menu4", d4r, d4d)
        t.exec("squire.accept", t.chat.choose, "Ok, I'll give it a go.")
        t.chat.close()

        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- Reldo (Varrock palace library): ask about the Imcando dwarves.
        t.exec("goto.reldo", t.player.goto_tile, 3211, 3494, 0)
        t.exec("reldo.talk", t.player.talk_to, "reldo_normal", 1)

        local rdr, rdd = t.chat.drain({ stop_at = "options" })
        t.expect("reldo.menu", rdr, rdd)
        t.exec("reldo.choose", t.chat.choose, "What do you know about the Imcando dwarves?")

        local rd2r, rd2d = t.chat.drain({ stop_at = "none" })
        t.expect("reldo.done", rd2r, rd2d)
        t.expect("quest.stage.spoken_reldo", t.quest.expect_stage("spoken_reldo"))

        -- Thurgo (south of Port Sarim): give him the redberry pie.
        t.exec("goto.thurgo1", t.player.goto_tile, 3001, 3144, 0)
        t.exec("thurgo.talk1", t.player.talk_to, "thurgo", 1)

        local tdr, tdd = t.chat.drain({ stop_at = "options" })
        t.expect("thurgo.menu1", tdr, tdd)
        t.exec("thurgo.pie", t.chat.choose, "Would you like a redberry pie?")

        local td2r, td2d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.pie.done", td2r, td2d)
        t.expect("quest.stage.given_pie", t.quest.expect_stage("given_pie"))

        -- Thurgo, second talk: he agrees to try, but wants a picture of
        -- the sword's design first.
        t.exec("thurgo.talk2", t.player.talk_to, "thurgo", 1)
        local td3r, td3d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.sword.done", td3r, td3d)
        t.expect("quest.stage.spoken_thurgo", t.quest.expect_stage("spoken_thurgo"))

        -- Squire, status check: he mentions Sir Vyvin's cupboard.
        t.exec("goto.squire2", t.player.goto_tile, 2977, 3342, 0)
        t.exec("squire.talk2", t.player.talk_to, "squire", 1)
        local sq2r, sq2d = t.chat.drain({ stop_at = "none" })
        t.expect("squire.status1.done", sq2r, sq2d)
        t.expect("quest.stage.looking_portrait", t.quest.expect_stage("looking_portrait"))

        -- Sir Vyvin's cupboard, upper floor of Falador Castle: open it,
        -- then search it for the portrait. It is a shut/open PAIR (trap
        -- 20), not a multiloc, so the open half needs its own symbol and
        -- click, and each mesbox is drained before the next click so it
        -- cannot settle for "close the box" instead of "do the thing".
        t.exec("goto.cupboard", t.player.goto_tile, 2985, 3336, 2)
        t.exec("cupboard.open", t.player.click_loc, "vyvincupboardshut", 1)
        local cor, cod = t.chat.drain({ stop_at = "none" })
        t.expect("cupboard.open.msg", cor, cod)
        t.ticks(3)  -- let the loc_change (shut -> open) reach the client's entity pool

        t.exec("cupboard.search", t.player.click_loc, "vyvincupboardopen", 1)
        local csr, csd = t.chat.drain({ stop_at = "none" })
        t.expect("cupboard.search.msg", csr, csd)
        t.ticks(2)  -- let the inv_add reach the client's own container read

        local port_r, port_d = t.inv.expect_has("knights_portrait", 1)
        t.step("inv.portrait", port_r == "ok" and "PASS" or "FAIL",
            "knights_portrait after the cupboard search -> " .. tostring(port_r) .. " " .. tostring(port_d))

        -- Thurgo, third talk: hand over the portrait; he asks for the
        -- smithing materials.
        t.exec("goto.thurgo2", t.player.goto_tile, 3001, 3144, 0)
        t.exec("thurgo.portrait", t.player.talk_to, "thurgo", 1)
        local td4r, td4d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.portrait.done", td4r, td4d)
        t.expect("quest.stage.looking_blurite", t.quest.expect_stage("looking_blurite"))
        t.ticks(2)  -- let the inv_del reach the client's own container read

        local port2_r, port2_d = t.inv.expect_absent("knights_portrait")
        t.step("inv.portrait.handed_over", port2_r == "ok" and "PASS" or "FAIL",
            "knights_portrait after handing it to Thurgo -> " .. tostring(port2_r) .. " " .. tostring(port2_d))

        -- Blurite mine, eastern cavern under the ice-giant cliff south of
        -- Port Sarim: mine one blurite ore for real (mining level 10 and
        -- a pickaxe are the setup cheat/give; the swing itself is not).
        t.exec("goto.mine", t.player.goto_tile, 3049, 9566, 0)
        local ore_before_r, ore_before = t.inv.count("blurite_ore")
        t.exec("mine.blurite", t.player.click_loc, "blurite_rock_1", 1)
        local ore_await_r = t.inv.await("blurite_ore", 1, 30)
        local ore_after_r, ore_after = t.inv.count("blurite_ore")
        t.check("mine.ore", ore_await_r == "ok",
            "blurite_ore " .. tostring(ore_before) .. " -> " .. tostring(ore_after)
                .. " (await=" .. tostring(ore_await_r) .. ")")

        -- Thurgo, fourth talk: hand over the blurite ore and the two iron
        -- bars; he forges the sword.
        t.exec("goto.thurgo3", t.player.goto_tile, 3001, 3144, 0)
        t.exec("thurgo.ore", t.player.talk_to, "thurgo", 1)
        local td5r, td5d = t.chat.drain({ stop_at = "none" })
        t.expect("thurgo.ore.done", td5r, td5d)
        t.ticks(2)  -- let the inv_add(faladian_sword) reach the client's own container read

        local sword_r, sword_d = t.inv.expect_has("faladian_sword", 1)
        t.step("inv.sword", sword_r == "ok" and "PASS" or "FAIL",
            "faladian_sword after Thurgo forges it -> " .. tostring(sword_r) .. " " .. tostring(sword_d))

        -- Squire, final hand-in: give him the finished sword.
        local snap_r, snap_before = t.skill.snapshot()
        t.step("reward.snapshot", snap_r == "ok" and "PASS" or "FAIL", "skill.snapshot -> " .. tostring(snap_r))

        t.exec("goto.squire3", t.player.goto_tile, 2977, 3342, 0)
        t.ticks(2)  -- let the entity pool settle after the teleport before hunting for a menu row
        t.exec("squire.final", t.player.talk_to, "squire", 1)
        local sq3r, sq3d = t.chat.drain({ stop_at = "none" })
        t.expect("squire.final.done", sq3r, sq3d)

        t.ticks(3)  -- completion is queued ([queue,squire_complete]), not immediate

        t.quest.expect_complete()
        t.check("reward.smithing", t.skill.expect_gain("smithing", 12725, snap_before))

        t.finish(0)
    end,
}
