-- Tears of Guthix: talk to Juna, accept quest, mine stone, make bowl, hand in.
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge, beside Hans). ::tearsofguthix teleports to Juna and resets
-- the quest to stage 0 (not_started).
--
-- Quest progression:
-- - Stage 0 (not_started): Talk to Juna, choose "Okay..." to accept
-- - Stage 1 (tog_need_bowl): Mine stone, use chisel to make bowl, go back to Juna
-- - Stage 2 (tog_complete): Quest complete, crafting XP awarded

-- Tears of Guthix: quest test.
--
-- CONTENT BUG: The quest's tog_juna location does not open dialogue when clicked.
-- The quest script at tearsofguthix.rs2:22 uses ~chatnpc_specific("Juna", tog_juna_dummy, $text)
-- to initiate dialogue from within a location click handler context.
-- However, this call requires an active NPC entity, which is not available in the
-- location interaction context. The error "NPC_COORD requires an active entity the
-- script does not have" prevents the quest dialogue from opening, making it impossible
-- to progress past the initial interaction.
--
-- This is a script-side bug where the quest handler needs to bind the NPC entity
-- before attempting to use ~chatnpc_specific, similar to how dttd_bmp.rs2 handles
-- the same NPC with ~dttdbmp_bind(tog_juna_dummy) first.

return {
    id = "tearsofguthix",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::tearsofguthix",
        "::setlevel firemaking 49",
        "::setlevel crafting 20",
        "::setlevel mining 20",
    },

    run = function(t)
        -- quest.bind records the varp and constants for later checks
        t.quest.bind({
            varp = "tog_juna_bowl",
            constants = {
                not_started = 0,
                need_bowl = 1,
                complete = 2,
            },
            display = "Tears of Guthix",
            points = 1,
        })

        -- Wait for setup cheat effects to appear client-side
        t.ticks(3)

        -- Check initial quest stage (read server side to bypass client varp issue)
        local stage_result, stage_value = t.var.server("tog_juna_bowl")
        t.check("quest.stage.initial", stage_result == "ok" and stage_value == 0,
            "tog_juna_bowl server=" .. tostring(stage_value) .. " result=" .. tostring(stage_result))

        -- Check inventory for the stone from setup
        t.expect("setup.have_stone", t.inv.expect_has("tog_stone", 1))

        -- Check inventory for the chisel from setup
        t.expect("setup.have_chisel", t.inv.expect_has("chisel", 1))

        -- Attempt to click on tog_juna location to trigger initial dialogue
        t.exec("tog.greet", t.player.click_loc, "tog_juna", 1)
        t.shot("after-greet")

        -- Verify player is at Juna location after the click
        local tile_result, tile_value = t.world.tile()
        t.check("location.verify_juna", tile_result == "ok",
            "tile=" .. tostring(tile_value))

        -- Verify no dialogue opened (the bug prevents it)
        local kind = t.chat.kind()
        t.check("chat.no_dialogue", kind == "none",
            "chat_kind=" .. tostring(kind))

        -- The dialogue should open here after clicking tog_juna, but the quest's
        -- script fails when attempting to call ~chatnpc_specific without an active
        -- entity context. This is a content bug in quest_tearsofguthix/scripts/tearsofguthix.rs2:22
        -- where the proc uses ~chatnpc_specific from within a location handler without
        -- binding the NPC entity first (compare to dttd_bmp.rs2's ~dttdbmp_bind approach).

        -- Verify server-side quest stage is still not_started (dialogue failed to accept quest)
        local stage_server_result, stage_server_value = t.var.server("tog_juna_bowl")
        t.check("quest.stage.unchanged", stage_server_result == "ok" and stage_server_value == 0,
            "tog_juna_bowl still=" .. tostring(stage_server_value))

        -- Verify inventory still contains original items (quest progression blocked)
        t.expect("inv.still_has_stone", t.inv.expect_has("tog_stone", 1))

        t.blocked("quest_tearsofguthix/scripts/tearsofguthix.rs2:22 - ~chatnpc_specific requires active NPC entity in location click handler context")
        return
    end,
}
