-- Cook's Assistant, end to end through the real client -- docs/
-- QUEST_SUITE_KIT.md phase 3, worker 3d, closed after review.
--
-- HOW THIS FILE WAS MADE, precisely. `tools/quest_gate/new_quest.py
-- cooksassistant` emits a skeleton from the Quest Helper guide; that
-- skeleton was READ and then discarded for this one quest, and the D2-era
-- hand file was modernised onto the phase-2 verb kit instead (quest.bind,
-- t.exec, inv.await_all, skill.snapshot,
-- quest.expect_complete). Two reasons, both checked against content rather
-- than guessed:
--
--   * Giving all three ingredients (`::give egg/pot_flour/bucket_milk`)
--     BEFORE ever talking to the Cook -- which the generator used to do in
--     `setup`, and now emits as `-- CHECK gather` markers instead -- is
--     wrong for this quest. quest_cook.rs2's accept branch ([label,cooks_assistant_whats_wrong]
--     case 1) checks `inv_total` for all three and jumps straight to
--     `cooks_assistant_completion` in the SAME Talk-to when they are already
--     held -- so that setup collapses the whole test into one dialogue and
--     destroys the exact re-talk this file exists to exercise
--     (QUEST_SUITE_KIT phase 2d: player.talk_to's re-talk fix, proved by
--     this file staying green with no local `talk_to_and_settle` shim).
--     `quest_cook_test_ingredients.rs2`'s own banner says as much: its
--     `cookbmp_test_give_ingredients` debugproc exists PRECISELY so a test
--     can gather AFTER accepting and drive the hand-in through a real
--     SECOND player.talk_to click.
--   * Hans has no Quest Helper guide at all, so the "regenerated from the
--     scaffold" half of the phase-3 proof holds for neither quest. What IS
--     proved here is the other half: no local helper functions, the phase-2
--     verbs doing the work, and a re-talk that mounts a genuinely different
--     page.
--
-- `::cook` (quest_cook.rs2's own reset/cheat adapter) resets %cookquest to
-- 0, clears the three ingredients and teleports the player beside the cook:
-- the fixture only has to be A character, not one already standing in the
-- kitchen. It writes no completion state -- it resets.
--
-- WHICH ROWS SHOOT. t.exec and t.check shoot the row they write; plain
-- t.expect/t.step rows do not, and gate.py's minimum-shape rule is per row
-- (tools/quest_gate/gate.py `shooting_row_names`), so an action that changes
-- the screen goes through t.exec and a pure read stays a plain expect.
-- That is the pattern to copy: photograph the clicks, not the readings.

return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    setup = { "::cook" },

    run = function(t)
        -- quest.bind reads %qp now, before anything else touches it, so
        -- quest.expect_complete's points row has a true "before" reading.
        t.quest.bind({
            varp = "cookquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            row = "quest_cooksassistant",
            display = "Cook's Assistant",
            points = 1,
        })

        -- ::cook's own varp write and teleport are server-side; give the
        -- client a couple of ticks to see them before reading anything
        -- (the same class of race hans.lua's own constants read settles
        -- for -- a cheat's effect is not visible client-side the instant
        -- the cheat call returns).
        t.ticks(3)
        t.expect("cooksassistant.reset", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------------- greet
        t.exec("cooksassistant.greet", t.player.talk_to, "cook")
        t.expect("cooksassistant.expect_head", t.chat.expect_head("cook"))

        -- ---------------------------------------------- "What's wrong?"
        -- chat.drain's own return is the stop_at kind it reached, which is
        -- an "ok" result either way -- t.expect grades PASS on "ok", and
        -- the stop_at page itself is not shot by drain (it returns before
        -- shooting the page it stops on), so the named shot after is a
        -- genuinely new capture, never a re-shoot of a page drain already
        -- photographed.
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("cooksassistant.drain_to_opener", drain1_result, drain1_detail)
        t.shot("cook-menu")

        t.exec("cooksassistant.choose_whats_wrong", t.chat.choose, "What's wrong?")

        -- ------------------------------------------- offer to help / accept
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("cooksassistant.drain_to_offer", drain2_result, drain2_detail)
        t.shot("cook-offer-menu")

        t.exec("cooksassistant.choose_help", t.chat.choose, "Yes, I'll help you.")

        -- Accepting just plays case 1's own two lines ("Yes, I'll help
        -- you." / "Oh thank you, thank you. I need milk, an egg and
        -- flour...") and closes -- [label,cooks_assistant_whats_wrong]'s
        -- case 1 has no trailing `@cooks_assistant_inprogress;` jump, so
        -- RuneScript's labels do NOT fall through into one another. The
        -- "I'm afraid I don't have any yet" / "I'll get right on it."
        -- reminder only exists on a LATER, separate talk while cookquest is
        -- already 1.
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("cooksassistant.drain_close", drain3_result, drain3_detail)
        t.shot("cook-dialogue-closed")

        t.expect("cooksassistant.started", t.quest.expect_stage("started"))

        -- ------------------------------------------- gather the ingredients
        -- The test-only debugproc, not `::give` -- see the file banner. A
        -- plain t.step rather than t.expect so the row's detail names the
        -- cheat and its verdict instead of being empty: api.drive.cheat
        -- answers (ok, nil), and an `ok` with nothing behind it is a row
        -- that proves nothing to whoever reads the ledger later.
        local give_result, give_detail = t.cheat("::cookbmp_test_give_ingredients")
        t.step("cooksassistant.give_ingredients",
            give_result == "ok" and "PASS" or "FAIL",
            "::cookbmp_test_give_ingredients -> " .. tostring(give_result)
                .. " " .. tostring(give_detail))

        -- inv.await_all is the WAIT; the counts read back after it are what
        -- goes in the ledger, because await_all's own success answer is
        -- (ok, nil) and would otherwise leave this row blank.
        local have_result, have_detail = t.inv.await_all(
            { egg = 1, bucket_milk = 1, pot_flour = 1 }, 10)
        local egg_read, egg_count = t.inv.count("egg")
        local milk_read, milk_count = t.inv.count("bucket_milk")
        local flour_read, flour_count = t.inv.count("pot_flour")
        t.check("cooksassistant.has_ingredients",
            have_result == "ok" and egg_count == 1 and milk_count == 1 and flour_count == 1,
            "await_all=" .. tostring(have_result) .. " " .. tostring(have_detail)
                .. " egg=" .. tostring(egg_count) .. "(" .. tostring(egg_read) .. ")"
                .. " bucket_milk=" .. tostring(milk_count) .. "(" .. tostring(milk_read) .. ")"
                .. " pot_flour=" .. tostring(flour_count) .. "(" .. tostring(flour_read) .. ")")

        -- ----------------------------------------------------- hand in
        -- The second, separate player.talk_to click this file exists to
        -- exercise (see the banner) -- the same npc, but the chat page now
        -- mounts a DIFFERENT kind/text than the greet above
        -- ("~chatnpc_anim ... How are you getting on ...", not "What am I
        -- to do?"), which is exactly the case `_settle_after_click`'s fix
        -- treats as a fresh mount rather than the stale greet page.
        t.exec("cooksassistant.handin_talk", t.player.talk_to, "cook")

        -- Drain the "how's it going" / thank-you exchange, and stop right
        -- at the completion mesbox rather than past it, so the exact
        -- completion message can be read off the live page instead of
        -- guessed at. `shots` stays at drain's own default (true) --
        -- turning it off here removed the frame-pump drain's per-page shot
        -- costs, and without that pump the very first continue_ inside
        -- drain raced UITree_SetPausePending's clear from the handin_talk
        -- click immediately before it (measured: "chat.continue_: a resume
        -- is already outstanding" on drain's first iteration, gone once
        -- shots reverted to the default true).
        local drain5_result, drain5_detail = t.chat.drain({ stop_at = "mesbox" })
        t.expect("cooksassistant.handin_drain", drain5_result, drain5_detail)

        -- The completion mesbox itself, read and photographed in one row --
        -- t.exec's own shot replaces the hand-taken one that used to
        -- sit here, which was a second capture of the page drain had just
        -- stopped on (measured in review: 0.034% of bytes apart, all of it
        -- a per-tick counter).
        local COMPLETION_MESSAGE = "You give some milk, an egg and some flour to the cook."
        t.exec("cooksassistant.complete_message", t.chat.expect_text, COMPLETION_MESSAGE)

        -- Read cooking xp now, one call before the commit that awards it --
        -- state.lua's own skill.expect_gain banner names this exact row as
        -- the caller it was written to replace.
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.step("cooksassistant.xp_before_read", xp_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))

        -- Dismissing the mesbox is what runs the one atomic transaction
        -- (quest_cook.rs2's ~cooks_assistant_commit): state, xp and qp all
        -- move here, then the reward scroll opens. NOT through t.exec:
        -- continue_ takes no target (its own banner: "call it directly and
        -- record it with t.check/t.step instead") and its own success
        -- answer is (ok, nil), which the hollow rule would grade FAIL.
        local continue_result, continue_detail = t.chat.continue_()
        t.expect("cooksassistant.commit_continue", continue_result,
            continue_detail or "chat.continue_ dismissed the completion mesbox")

        -- The commit's own varp/inventory updates reach the client-visible
        -- copies (and var.server's own read) some ticks after the click
        -- that triggered them -- an honest bounded await rather than a
        -- guessed sleep, so a genuine stuck commit reads as a named FAIL
        -- here rather than surfacing as an unrelated-looking failure in
        -- quest.expect_complete below.
        local commit_settle_result, commit_settle_detail = t.await({
            level = function()
                local r, v = t.var.server("cookquest")
                return r == "ok" and v == 2
            end,
            note = "cooksassistant.commit_settle",
        }, 20)
        t.step("cooksassistant.commit_settle",
            commit_settle_result == "ok" and "PASS" or "FAIL",
            "server cookquest -> 2 within 20 ticks (" .. tostring(commit_settle_result)
                .. ") " .. tostring(commit_settle_detail))

        -- Measured live (D2 pass): cookquest's own settle above can already
        -- read 2 (server, then client) while the backpack still shows all
        -- three ingredients -- the varp and the inventory container reach
        -- the client on separate channels and do not land the same frame.
        -- A client-visibility race, not a second commit, so it gets the
        -- same honest bounded await rather than a second guessed sleep.
        -- This row IS the "the quest ate the ingredients" assertion (the
        -- three separate expect_absent rows it replaces each wrote an empty
        -- detail); the counts it names are read back after the await.
        local gone_result, gone_detail = t.await({
            level = function()
                local er, ec = t.inv.count("egg")
                local mr, mc = t.inv.count("bucket_milk")
                local fr, fc = t.inv.count("pot_flour")
                return er == "ok" and ec == 0 and mr == "ok" and mc == 0
                    and fr == "ok" and fc == 0
            end,
            note = "cooksassistant.ingredients_settle",
        }, 20)
        local egg_after_read, egg_after = t.inv.count("egg")
        local milk_after_read, milk_after = t.inv.count("bucket_milk")
        local flour_after_read, flour_after = t.inv.count("pot_flour")
        t.step("cooksassistant.ingredients_consumed",
            (gone_result == "ok" and egg_after == 0 and milk_after == 0 and flour_after == 0)
                and "PASS" or "FAIL",
            "all three consumed within 20 ticks (" .. tostring(gone_result)
                .. ") " .. tostring(gone_detail)
                .. " egg=" .. tostring(egg_after) .. "(" .. tostring(egg_after_read) .. ")"
                .. " bucket_milk=" .. tostring(milk_after) .. "(" .. tostring(milk_after_read) .. ")"
                .. " pot_flour=" .. tostring(flour_after) .. "(" .. tostring(flour_after_read) .. ")")

        -- ---------------------------------------------- the reward scroll
        -- What the scroll ITSELF advertises, read off the component before
        -- anything closes it -- scroll.reward_xp (phase 2b) exists for this
        -- row and this is its only live caller. The number it answers is
        -- then what the stat gain below is checked against, so the two
        -- halves cross-check each other; nothing here is pinned to a
        -- literal reward amount.
        local reward_result, reward_xp = t.scroll.reward_xp("cooking")
        t.check("cooksassistant.scroll_reward_xp",
            reward_result == "ok" and type(reward_xp) == "number",
            "scroll.reward_xp(cooking) -> " .. tostring(reward_result)
                .. " " .. tostring(reward_xp) .. " Cooking XP")

        -- The stat moved by exactly what the scroll just said it would.
        -- skill.expect_gain accepts either unit (whole xp, or the server's
        -- xp*10 tenths) and names which one it matched. `or -1` only guards
        -- the arithmetic inside it when the row above already failed -- a
        -- scroll that carried no Cooking line makes this row fail too,
        -- which is the honest answer, not a skipped check.
        t.expect("cooksassistant.cooking_xp_up",
            t.skill.expect_gain("cooking", reward_xp or -1, xp_snapshot))

        -- ------------------------------------------------- the committed state
        -- Four rows (varp_complete, scroll_title, points, journal), all
        -- graded by quest.expect_complete itself. It also CLOSES the reward
        -- scroll on its way to the journal (quest.lua's own banner), which
        -- is what the next row checks.
        t.quest.expect_complete()

        -- quest.expect_complete's scroll.close answer only reaches a detail
        -- string, never a verdict (quest.lua grades the JOURNAL close, not
        -- this one), so a reward scroll that refused to close would report
        -- PASS everywhere. This row is that missing grade: after
        -- expect_complete the scroll must be gone, and scroll.title says so
        -- with `not_visible`. It is also the only capture of the screen
        -- after every modal this quest opened has been dismissed.
        local closed_result = t.scroll.title()
        t.check("cooksassistant.scroll_closed", closed_result == "not_visible",
            "scroll.title after quest.expect_complete -> " .. tostring(closed_result)
                .. " (expected not_visible -- expect_complete closes the reward scroll)")

        t.finish(0)
    end,
}
