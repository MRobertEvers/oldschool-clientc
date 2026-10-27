-- Fight Arena, the ending with General Khazard left alive (no recording; the OSRS
-- Wiki's Transcript:Fight_Arena, "Talking to Lady Servil after Khazard attacks").
-- Staged at freed_servils (12) in the compound's yard, as the Servils' scene leaves
-- a player who logs out in the General's fight: the arena door's "Return to the
-- arena?" puts them in front of him, the escape door's Quick-escape takes them out
-- without a question, and Lady Servil ends the quest at complete (14).
-- test/quests/arena.lua plays the recorded route and the other ending.

return {
    id = "arena_escape",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::arenastage 12",
        "::tele 0_40_49_46_15",
    },

    run = function(t)
        t.ticklog.start()
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp17_arenaquest",
            constants = {
                freed_servils = 12,
                complete = 14,
                complete_defeated_genkhazard = 15,
                not_started = 0,
            },
            row = "quest_fightarena",
            display = "Fight Arena",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.freed_servils", t.quest.expect_stage("freed_servils"))

        local function tile_text(tile)
            if type(tile) ~= "table" then
                return tostring(tile)
            end
            return tile.x .. "," .. tile.z .. "," .. tile.level
        end

        -- Back in (4396 [8..16]): the menu, the black, the General's copy.
        t.exec("returnToArena", t.player.click_loc, "fightarena_door2", 1)
        t.exec("returnToArena-menu", t.chat.play, { "choose:Yes." })
        t.ticks(8)
        local in_result, in_tile = t.world.tile()
        t.check("returnToArena.copy", in_result == "ok" and in_tile.x >= 6400,
            "after Yes at " .. tile_text(in_tile) .. " (want a copy of the arena: x >= 6400)")

        -- Out by the escape door's Quick-escape, the General still standing.
        t.exec("quickEscape", t.player.click_loc, "fightarena_door2_escape", 2)
        t.ticks(3)
        local out_result, out_tile = t.world.tile()
        t.check("quickEscape.yard", out_result == "ok" and out_tile.x == 2606 and out_tile.z == 3151
            and out_tile.level == 0, "after Quick-escape at " .. tile_text(out_tile) .. " (want 2606,3151,0)")
        t.expect("quest.stage.still_freed_servils", t.quest.expect_stage("freed_servils"))

        t.exec("goto-westDoorOut", t.player.goto_tile, 2585, 3141, 0)
        t.exec("westDoorOut", t.player.cross_gate, { loc = "fightarena_door1", at = { 2585, 3141, 0 },
            near = { 2585, 3141 }, far_ok = function(tile) return tile.x <= 2584 end,
            far_desc = "outside the compound, x <= 2584" })
        t.exec("goto-endQuest", t.player.goto_tile, 2566, 3198, 0)
        local _, snapshot = t.skill.snapshot()
        t.exec("endQuest", t.player.talk_to, "lady_servil", 1)
        t.exec("endQuest-dialog", t.chat.play, {
            "player:Lady Servil.",
            "npc:You're alive! I feared General Khazard would have killed you!",
            "player:He tried, but I escaped.",
            "npc:Thank goodness.",
            "npc:Now, you'll be pleased to know",
            "player:It's not a problem.",
            "npc:Please, take this payment.",
        })
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.quest.expect_complete()
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 12175, snapshot))
        t.expect("reward.thieving_xp", t.skill.expect_gain("thieving", 2175, snapshot))
        t.finish(0)
    end,
}
