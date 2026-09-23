-- Ernest the Chicken (quest_haunted). Drives Veronica's opening scene to
-- start the quest, walks into Draynor Manor and up to Professor
-- Oddenstein's top-floor room, and plays the "I'm looking for a guy called
-- Ernest" branch through to its end -- content's own oddenstein_looking
-- label (areas/draynor/scripts/professor_oddenstein.rs2) sets
-- %haunted = ^haunted_spoken_to_oddenstein there, with no lever puzzle
-- involved at all.
--
-- From spoken_to_oddenstein on, the three missing parts he asks for
-- (pressure_gauge, oil_can, rubber_tube) are each a REAL content
-- interaction (a poisoned-fountain trick, and two plain ground-item
-- pickups) gated behind a WALKING obstacle only -- the manor's compost/key/
-- closet_door detour for rubber_tube, and the basement's six-lever maze for
-- oil_can. Per docs/QUEST_AUTHORING.md S2/S6 ("click_loc it... or goto_tile
-- past it", "the same tile trick reaches an instanced area behind a
-- trapdoor too... with no click_loc on the trapdoor at all") and cog.lua's
-- own precedent ("the rat-cage side puzzle is navigation-only -- goto_tile
-- reaches it directly"), the walking obstacle is not the deliverable: this
-- file goto_tiles straight to rubber_tube (3111,3367,0,
-- areas/world/configs/m48_52.spawn) and oil_can (3092,9755,0,
-- areas/world/configs/m48_152.spawn -- the exact tile quest_haunted.rs2's
-- own [debugproc,hauntedbmp_oil] photographs) and drives the real
-- click_obj pickup at each, never touching closet_door or a single lever.
-- The six-lever bit math (quest_haunted.rs2 update_ernest_doors) was read
-- in full to confirm it gates nothing the hand-in itself checks --
-- professor_oddenstein.rs2's oddenstein_items/haunted_take_parts test only
-- inv_total(pressure_gauge/oil_can/rubber_tube), never a lever/door varp.
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
--
-- Hand-in (professor_oddenstein.rs2 oddenstein_items -> oddenstein_ernest_
-- thanks) is ONE continuous chain with zero player choices, from "Have you
-- found anything yet?" through Ernest's "Of course, of course." -- the
-- 2026-09-20 chat.play fix (8cd829faf) now awaits each page's own readiness
-- itself, so it is driven as a SINGLE chat.play list rather than the two
-- manually-ticked halves an earlier attempt raced (queue.py's last_failure
-- on this row).

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

        -- ------------------------------------------------------- the gauge
        -- poison/fish_food (professor_oddenstein.rs2 wants oddenstein_items
        -- to see all three parts; the gauge only comes out of the fountain
        -- once poisoned_fish_food has killed its piranhas).
        t.exec("goto-poison", t.player.goto_tile, 3097, 3366, 0)
        local poison_result, poison_detail = t.player.click_obj("poison", 3)
        local poison_have_result, poison_have_count = t.inv.count("poison")
        t.check("pickup.poison", poison_result == "ok" and poison_have_result == "ok" and poison_have_count >= 1,
            string.format("click_obj(poison) -> %s (%s); inv poison=%s",
                tostring(poison_result), tostring(poison_detail), tostring(poison_have_count)))

        t.exec("goto-fishfood", t.player.goto_tile, 3109, 3357, 1)
        local fish_result, fish_detail = t.player.click_obj("fish_food", 3)
        local fish_have_result, fish_have_count = t.inv.count("fish_food")
        t.check("pickup.fish_food", fish_result == "ok" and fish_have_result == "ok" and fish_have_count >= 1,
            string.format("click_obj(fish_food) -> %s (%s); inv fish_food=%s",
                tostring(fish_result), tostring(fish_detail), tostring(fish_have_count)))

        -- [opheldu,poison]/[opheldu,fish_food] both trigger @poison_fish_food
        -- -- either arming order reaches it, the detail names the diff.
        t.exec("poison-fishfood", t.player.use_item_on_item, "poison", "fish_food")
        local poisoned_have_result, poisoned_have_count = t.inv.count("poisoned_fish_food")
        t.check("have.poisoned_fish_food", poisoned_have_result == "ok" and poisoned_have_count >= 1,
            string.format("inv poisoned_fish_food=%s (%s)", tostring(poisoned_have_count), tostring(poisoned_have_result)))

        -- ------------------------------------------------------ the gauge
        t.exec("goto-fountain", t.player.goto_tile, 3088, 3335, 0)
        local fountain = t.player.by_symbol("loc", "hauntedfountain")
        t.exec("poison-fountain", t.player.use_on, "poisoned_fish_food", fountain)
        -- Measured (run 1): these mes() lines print straight to the chat
        -- LOG here, not a modal mesbox -- chat.drain right after use_on's
        -- own (immediate, backpack-diff) settle found kind=none and did
        -- nothing, three ticks before %haunted_manor_fountain_poisoned=1 is
        -- actually written (it is the LAST statement in the p_delay(1) +
        -- p_delay(2) chain: "...then die and float to the surface." then
        -- the assignment). Wait for that exact line instead of draining.
        local poison_wait_result, poison_wait_detail = t.msg.await("then die and float to the surface", 10)
        t.check("poison-fountain-wait", poison_wait_result == "ok",
            "msg.await('...then die and float to the surface', 10) -> " .. tostring(poison_wait_result) .. " " .. tostring(poison_wait_detail))

        -- A second plain click on the now-poisoned fountain grants the
        -- gauge (oploc1,hauntedfountain): chatplayer_anim's portrait DOES
        -- open a real modal page here (measured, run 2: "Click here to
        -- continue" under a chat_quiz portrait) -- unlike the bare mes()
        -- chain above, this one needs real continues, so drain it.
        t.exec("fountain-gauge", t.player.click_loc, "hauntedfountain", 1)
        t.exec("fountain-gauge-drain", t.chat.drain, { stop_at = "none" })
        -- trap 24: the container delta lands a tick behind the click/drain
        -- that caused it -- poll, never a bare t.inv.count right after.
        local gauge_await_result, gauge_await_detail = t.inv.await("pressure_gauge", 1, 10)
        t.check("pickup.pressure_gauge", gauge_await_result == "ok",
            string.format("inv.await(pressure_gauge,1,10) -> %s (%s) after the poisoned fountain's second click",
                tostring(gauge_await_result), tostring(gauge_await_detail)))

        -- ------------------------------------------------- the rubber tube
        -- rubber_tube is a plain ground item (areas/world/configs/
        -- m48_52.spawn) behind closet_door's key/lock walking obstacle, not
        -- behind any scripted state closet_door itself sets -- goto_tile
        -- reaches its tile directly (file header note above).
        t.exec("goto-rubbertube", t.player.goto_tile, 3111, 3367, 0)
        local tube_result, tube_detail = t.player.click_obj("rubber_tube", 3)
        local tube_have_result, tube_have_count = t.inv.count("rubber_tube")
        t.check("pickup.rubber_tube", tube_result == "ok" and tube_have_result == "ok" and tube_have_count >= 1,
            string.format("click_obj(rubber_tube) -> %s (%s); inv rubber_tube=%s",
                tostring(tube_result), tostring(tube_detail), tostring(tube_have_count)))

        -- ----------------------------------------------------- the oil can
        -- oil_can is a plain ground item in the basement maze (areas/world/
        -- configs/m48_152.spawn) -- the exact tile quest_haunted.rs2's own
        -- [debugproc,hauntedbmp_oil] photographs, behind the six-lever
        -- walking obstacle the file header explains bypassing.
        t.exec("goto-oilcan", t.player.goto_tile, 3092, 9755, 0)
        local oil_result, oil_detail = t.player.click_obj("oil_can", 3)
        local oil_have_result, oil_have_count = t.inv.count("oil_can")
        t.check("pickup.oil_can", oil_result == "ok" and oil_have_result == "ok" and oil_have_count >= 1,
            string.format("click_obj(oil_can) -> %s (%s); inv oil_can=%s",
                tostring(oil_result), tostring(oil_detail), tostring(oil_have_count)))

        -- ------------------------------------------------------- hand-in
        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        t.exec("goto-oddenstein-handin", t.player.goto_tile, 3110, 3367, 2)
        t.exec("talk-oddenstein-handin", t.player.talk_to, "professor_oddenstein", 1)
        -- oddenstein_items (all three parts present) -> haunted_take_parts
        -- -> oddenstein_ernest_thanks -> haunted_commit, ONE continuous
        -- chain, no choices at all (file header note on the 2026-09-20 fix).
        -- Measured (run 3): the "You give the rubber tube..."/"...and a can
        -- of oil..."/"Oddenstein starts up the machine."/"The machine hums
        -- and shakes." lines are all PLAIN mes() with no chatnpc/chatplayer
        -- wrapper -- they print to the chat log (like the fountain-poison
        -- chain above), never a mesbox page, so they are not chat.play
        -- entries; only the chatnpc_anim/chatplayer_anim/chatnpc_specific_
        -- anim lines open real pages.
        t.exec("oddenstein-handin", t.chat.play, {
            "npc:Have you found anything yet?",
            "player:I have everything!",
            "npc:Give 'em here then.",
            "npc:Let's get this fixed then.",
            "npc:It was dreadfully irritating being a chicken. How can I ever thank you?",
            "player:Well a cash reward is always nice...",
            "npc:Of course, of course.",
        })
        t.ticks(3) -- completion (haunted_commit) is queued behind its own dialogue, not synchronous

        t.quest.expect_complete()

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins", reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
            and reward_coins_after == reward_coins_before + 300,
            string.format("coins %s -> %s (want +300), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
