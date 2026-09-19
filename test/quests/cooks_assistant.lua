-- Cook's Assistant, end to end through the real client -- docs/
-- QUEST_DRIVER_REMAINING.md phase D2. Sequence and symbol names taken from
-- the server selftest reference, src/torirsserver/test/quest_cook_selftest.u.h
-- (accept -> gather -> hand in -> one atomic commit -> reward scroll).
--
-- `::cook` (OSRS-Content/.../quest_cook/scripts/quest_cook.rs2, the existing
-- reset/cheat adapter) resets %cookquest to 0, clears the three ingredients
-- and teleports the player beside the cook: the fixture only has to be A
-- character, not one already standing in the kitchen.
--
-- The three ingredients are given through a test-only debugproc,
-- `::cookbmp_test_give_ingredients` (OSRS-Content/.../quest_cook/scripts/
-- quest_cook_test_ingredients.rs2) -- it does nothing the existing
-- `::cookbmp_handin` does not already do, except that it does NOT also
-- teleport and auto-fire `p_opnpc(1)`, which would hand the quest in through
-- the debugproc itself and skip the exact `player.talk_to` click this test
-- exists to exercise.

return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    setup = { "::cook" },

    run = function(t)
        -- ::cook's own varp write and teleport are server-side; give the
        -- client a couple of ticks to see them before reading anything
        -- (the same class of race hans.lua's own constants read settles
        -- for -- a cheat's effect is not visible client-side the instant
        -- the cheat call returns).
        t.t.ticks(3)

        -- ::cook already reset the quest and teleported next to the cook;
        -- confirm the state a fresh accept starts from before touching it.
        local reset_result, cookquest_before = t.var.varp("cookquest")
        t.t.step("cooksassistant.reset", (reset_result == "ok" and cookquest_before == 0) and "PASS" or "FAIL",
            "cookquest after ::cook -> " .. tostring(cookquest_before) .. " (" .. tostring(reset_result) .. ")")

        -- ------------------------------------------------------- greet
        local talk_result, talk_detail = t.player.talk_to("cook")
        t.t.expect("cooksassistant.greet", talk_result, talk_detail)
        t.t.shot("cook-greeting")

        local head_result, head_detail = t.chat.expect_head("cook")
        t.t.expect("cooksassistant.expect_head", head_result, head_detail)

        -- ---------------------------------------------- "What's wrong?"
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.t.expect("cooksassistant.drain_to_opener", drain1_result, drain1_detail)

        local choose1_result, choose1_detail = t.chat.choose("What's wrong?")
        t.t.expect("cooksassistant.choose_whats_wrong", choose1_result, choose1_detail)

        -- ------------------------------------------- offer to help / accept
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.t.expect("cooksassistant.drain_to_offer", drain2_result, drain2_detail)

        local choose2_result, choose2_detail = t.chat.choose("Yes, I'll help you.")
        t.t.expect("cooksassistant.choose_help", choose2_result, choose2_detail)

        -- Accepting just plays case 1's own two lines ("Yes, I'll help
        -- you." / "Oh thank you, thank you. I need milk, an egg and
        -- flour...") and closes -- [label,cooks_assistant_whats_wrong]'s
        -- case 1 has no trailing `@cooks_assistant_inprogress;` jump, so
        -- RuneScript's labels do NOT fall through into one another. The
        -- "I'm afraid I don't have any yet" / "I'll get right on it."
        -- reminder only exists on a LATER, separate talk while cookquest is
        -- already 1 -- confirmed live (11-npc.png, before this fix, showed
        -- the dialogue already closed to the ordinary chat log where a
        -- third menu was expected).
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.t.expect("cooksassistant.drain_close", drain3_result, drain3_detail)
        t.t.shot("cook-quest-offer")

        t.t.expect("cooksassistant.started", t.var.expect("cookquest", 1))

        -- ------------------------------------------- gather the ingredients
        local give_result, give_detail = t.t.cheat("::cookbmp_test_give_ingredients")
        t.t.expect("cooksassistant.give_ingredients", give_result, give_detail)
        t.t.ticks(2)

        t.t.expect("cooksassistant.has_egg", t.inv.expect_has("egg", 1))
        t.t.expect("cooksassistant.has_milk", t.inv.expect_has("bucket_milk", 1))
        t.t.expect("cooksassistant.has_flour", t.inv.expect_has("pot_flour", 1))
        t.t.shot("cook-ingredients")

        -- ----------------------------------------------------- hand in
        local xp_before_result, xp_before_reading = t.skill("cooking")
        local xp_before = (xp_before_result == "ok" and type(xp_before_reading) == "table")
            and xp_before_reading.experience or nil
        t.t.step("cooksassistant.xp_before_read", xp_before and "PASS" or "FAIL",
            "cooking xp before hand-in -> " .. tostring(xp_before))

        local handin_result, handin_detail = t.player.talk_to("cook")
        t.t.expect("cooksassistant.handin_talk", handin_result, handin_detail)

        -- Drain the "how's it going" / thank-you exchange with shots, and
        -- stop right at the completion mesbox rather than past it, so the
        -- exact completion message can be read off the live page instead of
        -- guessed at.
        local drain5_result, drain5_detail = t.chat.drain({ stop_at = "mesbox" })
        t.t.expect("cooksassistant.handin_drain", drain5_result, drain5_detail)
        -- No named shot here. chat.drain's OWN last capture is already this
        -- page: drain reads the kind, takes its shot, and the server's reply
        -- lands during the pump the capture costs, so the file it writes
        -- ("NN-npc.png") shows the completion mesbox rather than the npc
        -- page its name was chosen from -- verified by eye on
        -- 21-npc.png. A second `t.t.shot("cook-handin")` here photographed
        -- that same static page a frame later, and the two files came out
        -- byte-identical whenever no per-tick counter (the run-energy orb)
        -- happened to tick between them -- which gate.py's duplicate-MD5
        -- rule correctly rejects as "a screenshot that never changed".
        -- It was a coin flip before this file stopped spending a 20-tick
        -- talk_to timeout on the hand-in (see pointer.lua's fourth settle
        -- arm); it is not evidence either way, so it is gone.

        local COMPLETION_MESSAGE = "You give some milk, an egg and some flour to the cook."
        local complete_msg_result, complete_msg_detail = t.chat.expect_text(COMPLETION_MESSAGE)
        t.t.expect("cooksassistant.complete_message", complete_msg_result, complete_msg_detail)

        -- Dismissing the mesbox is what runs the one atomic transaction
        -- (quest_cook.rs2's ~cooks_assistant_commit): state, xp and qp all
        -- move here, then the reward scroll opens.
        local continue_result, continue_detail = t.chat.continue_()
        t.t.expect("cooksassistant.commit_continue", continue_result, continue_detail)

        -- The commit's own varp/inventory updates reach the client-visible
        -- copies (and var.server's own read) some ticks after the click
        -- that triggered them -- the same race t.t.cheat's own effects
        -- settle for elsewhere in this file and in hans.lua. A flat
        -- `t.t.ticks(2)` here is a guess, and measured live it is not a
        -- reliable one: one run under load timed cookquest_client/server
        -- and all three egg/milk/flour_gone checks out reading the
        -- PRE-commit state, while the scroll (read several real steps
        -- later) already showed the completed quest and the 300 xp reward
        -- -- the commit had run, this fixed sleep just was not long enough
        -- to see it. Wait on the SERVER's own value directly instead, an
        -- honest bounded await rather than a guessed nap, so a genuine
        -- stuck commit still reads as a named FAIL here rather than
        -- surfacing as three unrelated-looking inventory failures below.
        local commit_settle_result, commit_settle_detail = t.await({
            level = function()
                local r, v = t.var.server("cookquest")
                return r == "ok" and v == 2
            end,
            note = "cooksassistant.commit_settle",
        }, 20)
        t.t.step("cooksassistant.commit_settle",
            commit_settle_result == "ok" and "PASS" or "FAIL",
            "server cookquest -> 2 within 20 ticks (" .. tostring(commit_settle_result)
                .. ") " .. tostring(commit_settle_detail))

        -- ------------------------------------------- the committed state
        t.t.expect("cooksassistant.cookquest_client", t.var.expect("cookquest", 2))

        local server_result, server_value = t.var.server("cookquest")
        t.t.step("cooksassistant.cookquest_server",
            (server_result == "ok" and server_value == 2) and "PASS" or "FAIL",
            "cookquest server -> " .. tostring(server_value) .. " (" .. tostring(server_result) .. ")")

        -- Measured live: cookquest's own settle above can already read 2
        -- (server, then client) while the backpack still shows all three
        -- ingredients -- the varp and the inventory container reach the
        -- client on separate channels and do not land the same frame. The
        -- reference server selftest (quest_cook_selftest.u.h) has no such
        -- gap because it reads the server's own player struct directly;
        -- this is a client-visibility race, not a second commit, so it
        -- gets the same honest bounded await rather than a second guessed
        -- sleep.
        local ingredients_gone_result, ingredients_gone_detail = t.await({
            level = function()
                local er, ec = t.inv.count("egg")
                local mr, mc = t.inv.count("bucket_milk")
                local fr, fc = t.inv.count("pot_flour")
                return er == "ok" and ec == 0 and mr == "ok" and mc == 0
                    and fr == "ok" and fc == 0
            end,
            note = "cooksassistant.ingredients_settle",
        }, 20)
        t.t.step("cooksassistant.ingredients_settle",
            ingredients_gone_result == "ok" and "PASS" or "FAIL",
            "egg/milk/flour all consumed within 20 ticks ("
                .. tostring(ingredients_gone_result) .. ") " .. tostring(ingredients_gone_detail))

        t.t.expect("cooksassistant.egg_gone", t.inv.expect_absent("egg"))
        t.t.expect("cooksassistant.milk_gone", t.inv.expect_absent("bucket_milk"))
        t.t.expect("cooksassistant.flour_gone", t.inv.expect_absent("pot_flour"))

        -- ------------------------------------------------- the reward scroll
        local title_result, title = t.scroll.title()
        t.t.expect("cooksassistant.scroll_title", title_result,
            title_result == "ok" and ("name=" .. tostring(title and title.name)) or tostring(title))
        t.t.shot("cook-complete")

        local rewards_result, rewards = t.scroll.rewards()
        local reward_xp = nil
        if rewards_result == "ok" and type(rewards) == "table" and type(rewards.lines) == "table" then
            for _, line in ipairs(rewards.lines) do
                local matched = string.match(line, "(%d+)%s+Cooking XP")
                if matched then
                    reward_xp = tonumber(matched)
                end
            end
        end
        t.t.step("cooksassistant.scroll_rewards",
            (rewards_result == "ok" and reward_xp) and "PASS" or "FAIL",
            reward_xp and ("scroll reports " .. reward_xp .. " Cooking XP")
                or ("no 'N Cooking XP' line in the reward scroll -- " .. tostring(rewards)))

        -- Cooking xp went up by exactly the amount the scroll itself just
        -- displayed -- read from content, never pinned as a literal here.
        local xp_after_result, xp_after_reading = t.skill("cooking")
        local xp_after = (xp_after_result == "ok" and type(xp_after_reading) == "table")
            and xp_after_reading.experience or nil
        local xp_ok = false
        local xp_note = "before=" .. tostring(xp_before) .. " after=" .. tostring(xp_after)
        if xp_before ~= nil and xp_after ~= nil and reward_xp then
            local delta = xp_after - xp_before
            -- The client's own xp unit was not pinned by this file either;
            -- accept whichever of the two representations (whole xp, or the
            -- server's xp*10 "tenths") the reward line actually predicts.
            xp_ok = (delta == reward_xp) or (delta == reward_xp * 10)
            xp_note = xp_note .. " delta=" .. delta .. " reward(from scroll)=" .. reward_xp
        end
        t.t.step("cooksassistant.cooking_xp_up", xp_ok and "PASS" or "FAIL", xp_note)

        t.t.expect("cooksassistant.scroll_close", t.scroll.close())
        t.t.shot("cook-scroll-closed")

        t.t.finish(0)
    end,
}
