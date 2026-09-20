-- Ernest the Chicken (quest_haunted). Drives Veronica's opening scene to
-- start the quest, walks into Draynor Manor and up to Professor
-- Oddenstein's top-floor room, and plays the "I'm looking for a guy called
-- Ernest" branch through to its end -- content's own oddenstein_looking
-- label (areas/draynor/scripts/professor_oddenstein.rs2) sets
-- %haunted = ^haunted_spoken_to_oddenstein there, with no lever puzzle
-- involved at all; that is the SECOND reviewer's fix for the first
-- rejection (`last_failure` on this row 2026-09-19): the prior submission
-- blocked at the graveyard without ever walking to the manor.
--
-- From spoken_to_oddenstein on, the three missing parts he asks for
-- (pressure_gauge, oil_can, rubber_tube) are gated behind the manor
-- basement's six-lever combination puzzle (quest_haunted.rs2's
-- ernest_pull_lever/ernest_open_maze_door, unresolved PuzzleWrapperStep
-- entries such as 'pullUpLeverA' in the scaffold -- a maze door sequence
-- with no driver verb this suite has, and not one clickable symbol): that
-- is where THIS file blocks, after proving the quest was actually started
-- and the Oddenstein conversation actually played.
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- beside Hans -- nothing moves the player closer. Veronica spawns at
-- 3110,3330,0 (areas/world/configs/m48_52.spawn) just outside the manor's
-- south gate; Professor Oddenstein spawns at 3110,3367,2, the top floor.
--
-- Doors: the manor's own entrance (haunteddoorl/haunteddoorr, op 1) is not
-- an ordinary walkable door -- clicking it from the correct (south) side
-- runs quest_haunted.rs2's open_manor_entrance label, which mes()es "You go
-- through the doors." and p_teleports the player one tile north; approach
-- from the wrong side and it answers "The doors won't open." instead. Once
-- inside, goto_tile carries its own level argument the rest of the way (the
-- top floor is level 2), climbing the stairs -- docs/QUEST_AUTHORING.md S2.

return {
    id = "haunted",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so nothing crowds the backpack
        "::haunted", -- resets %haunted/%haunted_settle/%ernestlever/%ernestdoors to not_started; does not teleport
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "haunted",
            constants = {
                not_started = 0,
                started = 1,
                spoken_to_oddenstein = 2,
                complete = 3,
                settle_none = 0,
                settle_handover = 1,
                settle_paid = 2,
                questpoints = 4,
            },
            row = "quest_ernestthechicken",
            display = "Ernest the Chicken",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------- Veronica
        t.exec("goto-veronica", t.player.goto_tile, 3110, 3330, 0)
        t.exec("talk-veronica", t.player.talk_to, "veronica", 1)
        -- veronica.rs2 [opnpc1,veronica] @haunted_start: opens with the
        -- NPC's own line (chatnpc_anim), then a two-row choice menu, spelled
        -- verbatim from the .rs2.
        t.exec("veronica-accept", t.chat.play, {
            "npc:Can you please help me? I'm in",
            "choose:Aha, sounds like a quest. I'll help.",
            "player:Aha, sounds like a quest. I'll",
            "npc:Yes yes, I suppose it is a que",
            "npc:Seeing as we were a little los",
            "npc:That was an hour ago. That hou",
            "player:Ok, I'll see what I can do.",
            "npc:Thank you, thank you. I'm very",
        })
        t.ticks(2) -- the varp write (%haunted = ^haunted_started) settles a tick behind the closed dialogue
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------------- into the manor
        t.exec("goto-manor-gate", t.player.goto_tile, 3108, 3349, 0)
        t.exec("open-manor-door", t.player.click_loc, "haunteddoorl", 1)

        -- --------------------------------------------------- to Oddenstein
        t.exec("goto-oddenstein", t.player.goto_tile, 3110, 3367, 2)
        t.exec("talk-oddenstein", t.player.talk_to, "professor_oddenstein", 1)
        -- professor_oddenstein.rs2 [label,oddenstein_looking] (reached at
        -- %haunted == ^haunted_started) opens directly on a THREE-row choice
        -- menu -- no npc line before it -- spelled verbatim through to
        -- @oddenstein_change_back / @oddenstein_not_easy, which is the page
        -- that writes %haunted = ^haunted_spoken_to_oddenstein.
        t.exec("oddenstein-looking", t.chat.play, {
            "choose:I'm looking for a guy called Ernest.",
            "player:I'm looking for a guy called E",
            "npc:Ah Ernest, top notch bloke. He",
            "player:So you know where he is then?",
            "npc:He's that chicken over there.",
            "player:Ernest is a chicken...? Are yo",
            "npc:Oh, he isn't normally a chicke",
            "npc:It was originally going to be ",
            "choose:Change him back this instant!",
            "player:Change him back this instant!",
            "npc:Umm... It's not so easy...",
            "npc:My machine is broken, and the ",
            "player:Well I can look for them.",
            "npc:That would be a help. They'll ",
            "npc:I'm missing the pressure gauge",
        })
        t.ticks(2) -- same settle as above, before reading the varp back
        t.expect("quest.stage.spoken_to_oddenstein", t.quest.expect_stage("spoken_to_oddenstein"))

        -- From here, oddenstein_items wants pressure_gauge + oil_can +
        -- rubber_tube in the backpack (professor_oddenstein.rs2), and all
        -- three sit behind the manor basement's six-lever puzzle
        -- (quest_haunted.rs2's ernest_pull_lever/ernest_open_maze_door: a
        -- specific up/down sequence across six multiloc levers that opens a
        -- maze door -- there is no click_loc symbol for "solve the puzzle",
        -- only the individual lever locs the sequence itself decides).
        t.blocked("quest_haunted: pressure_gauge/oil_can/rubber_tube are gated behind the manor basement's six-lever combination puzzle (quest_haunted.rs2 ernest_pull_lever/ernest_open_maze_door) -- a maze-door sequence with no single driver verb, not automatable by a scripted click list")
        return
    end,
}
