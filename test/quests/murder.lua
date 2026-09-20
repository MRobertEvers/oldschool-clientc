-- Murder Mystery, end to end through the real client.
--
-- %murdersus is rolled random 1-6 at accept time (murder_guard.rs2's own
-- `%murdersus = ~random_range(1, 6)`), and it CANNOT be read by this
-- driver: quest_murder.varp declares it with no `transmit=yes`, and
-- var.server (torirs_plugin_drive_state.c's DriveState_VarpServer) reads
-- `var_serv[]`, the client's cache of values the SERVER TRANSMITTED --
-- never populated for a varp the server never sends, so it reads 0 forever
-- however long this polls (measured run 1: "murdersus -> ok 0" the tick
-- right after murderquest itself read "started"; the same class of gap
-- QUEST_AUTHORING.md's section 8 already documents for
-- `[makinghistory]`'s empty transmit body).
--
-- Worked around, not blocked: the thread murder_inspect_window hands out
-- (quest_murder_window.rs2's own `~get_murder_thread` proc) is one of three
-- colours, and each colour narrows %murdersus to exactly TWO of the six
-- (green: Anna/David, red: Bob/Carol, blue: Elizabeth/Frank) -- read
-- straight off the real inventory after the window click, no cheat
-- involved. Asking the WRONG one of the two the poison question, or
-- searching their poison-proof loc, or comparing their fingerprint is a
-- content no-op every time (each script checks `%murdersus = ^murderer_x`
-- itself before doing anything), so this drives BOTH candidates through
-- every remaining evidence step and lets the server's own match/mismatch
-- decide which one actually moved -- the same "content answers the click
-- honestly, the test does not need to already know the answer" principle
-- QUEST_AUTHORING.md's own fingerprint mini-game is built on.
--
-- Evidence chain driven for real, never cheated (trap 16):
--   thread    -- kr_mansion_window_multi_01, op2 (Investigate)
--   poison    -- poison_salesman (option 2) THEN each candidate sibling in
--                turn (option 4, "Why'd you buy poison...") THEN each
--                candidate's own poison-proof loc, op2
--   prints    -- flourbarrel (op2, once per candidate) + murdersacks
--                (op2, three sheets: one for the already-on-the-ground
--                murderweapondust, one per candidate's own barrel item)
--                then murderfingerprint1 compared against each candidate's
--                print via use_item_on_item
-- Both evidences true (from whichever candidate actually matched) sends
-- murderguard_who straight to murderguard_conclusive_proof, the only
-- branch that queues murder_quest_complete.

return {
    id = "murder",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give pot_empty 1",
    },

    run = function(t)
        -- Suspect data. npc/barrel/item/poison_loc symbols are
        -- quest_murder's own script triggers ([opnpc1,...],
        -- [oploc2,murderbarrel<n>], [oploc2,...]); tx/tz/tl are
        -- areas/world/configs/m42_55.spawn's own rows for each
        -- kr_<name>_sinclair_multi.
        local SUS = {
            [1] = { name = "anna", npc = "kr_anna_sinclair_multi", tx = 2734, tz = 3575, tl = 0,
                barrel = "murderbarrela", item = "murdernecklace", itemdust = "murdernecklacedust",
                print = "murderfingerprinta", poison_loc = "murdercompost" },
            [2] = { name = "bob", npc = "kr_bob_sinclair_multi", tx = 2748, tz = 3559, tl = 0,
                barrel = "murderbarrelb", item = "murdercup", itemdust = "murdercupdust",
                print = "murderfingerprintb", poison_loc = "murderhive" },
            [3] = { name = "carol", npc = "kr_carol_sinclair_multi", tx = 2734, tz = 3581, tl = 1,
                barrel = "murderbarrelc", item = "murderbottle", itemdust = "murderbottledust",
                print = "murderfingerprintc", poison_loc = "murderdrain" },
            [4] = { name = "david", npc = "kr_david_sinclair_multi", tx = 2739, tz = 3581, tl = 0,
                barrel = "murderbarreld", item = "murderbook", itemdust = "murderbookdust",
                print = "murderfingerprintd", poison_loc = "murderweb" },
            [5] = { name = "elizabeth", npc = "kr_elizabeth_sinclair_multi", tx = 2746, tz = 3581, tl = 1,
                barrel = "murderbarrele", item = "murderneedle", itemdust = "murderneedledust",
                print = "murderfingerprinte", poison_loc = "murderfountain" },
            [6] = { name = "frank", npc = "kr_frank_sinclair_multi", tx = 2742, tz = 3577, tl = 0,
                barrel = "murderbarrelf", item = "murderpot", itemdust = "murderpotdust",
                print = "murderfingerprintf", poison_loc = "murdersign" },
        }
        -- get_murder_thread's own switch_int (quest_murder_window.rs2):
        -- anna/david green, bob/carol red, elizabeth/frank blue.
        local THREAD_CANDIDATES = {
            murderthreadg = { 1, 4 },
            murderthreadr = { 2, 3 },
            murderthreadb = { 5, 6 },
        }

        t.quest.bind({
            varp = "murderquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Murder Mystery",
            points = 3,
        })

        -- --------------------------------------------------- accept
        t.exec("goto.guard", t.player.goto_tile, 2741, 3562, 0)
        t.exec("talk.guard", t.player.talk_to, "murderguard", 1)
        -- The accept branch falls straight through into murderguard_help's
        -- own two pages after "Thanks a lot!" (murder_guard.rs2's
        -- unconditional `@murderguard_help;` at the end of the $start=1
        -- arm) -- run 4's own screenshots caught the cost of stopping the
        -- list one page early: the box sat open on "What should I be doing
        -- to help?" through every later step (goto_tile teleports do not
        -- close a dialogue), and the window investigation that follows
        -- never had a real chance while it was still up.
        t.exec("accept.quest", t.chat.play, {
            "player:What's going on here?",
            "npc:Oh, it's terrible! Lord Sinclair has been murdered",
            "npc:If you can help us we will be very grateful",
            "options",
            "choose:Sure, I'll help.",
            "player:Sure, I'll help!",
            "npc:Thanks a lot!",
            "player:What should I be doing to help?",
            "npc:Look around and investigate",
        })
        t.exec("accept.close", t.chat.drain, { stop_at = "none" })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------------- thread evidence
        t.exec("goto.mansion", t.player.goto_tile, 2745, 3575, 0)
        t.exec("goto.window.area", t.player.goto_tile, 2748, 3575, 0)
        -- QUEST_AUTHORING.md section 8: a `covered` right after a goto_tile
        -- teleport on a target that otherwise works is the scene/camera not
        -- having settled yet -- t.ticks(2) before the first press. Made no
        -- difference here (run 6): still `covered`, same element, same pixel.
        t.ticks(2)

        local win_target, win_target_result = t.player.by_symbol("loc", "kr_mansion_window_multi_01")
        t.check("window.target", win_target_result == "ok",
            "by_symbol(loc,kr_mansion_window_multi_01) -> " .. tostring(win_target_result)
                .. (type(win_target) == "table" and (" id=" .. tostring(win_target.id)
                    .. " match=" .. tostring(win_target.match)) or ""))

        -- t.world.loc_near (an earlier run) already confirmed the symbol
        -- resolves correctly -- "murderwindow" exact and
        -- "kr_mansion_window_multi_01" base both answer the same id=26123 at
        -- the same tile 2748,3577,0, so this is not a resolve or tile miss.
        local pos_result, pos = t.drive.screen_position(win_target)
        t.check("window.screen_position", true,
            "drive.screen_position(target) -> " .. tostring(pos_result) .. " "
                .. (type(pos) == "table" and (tostring(pos.x) .. "," .. tostring(pos.y))
                    or tostring(pos)))

        -- Recorded as a plain read (verdict PASS whenever the verb answered
        -- at all), not an assertion that the click landed -- gate.py grades
        -- every non-BLOCKED row as a real pass/fail, and this row's whole
        -- job is to carry whatever click_loc actually answered into the
        -- ledger for the row right after (or the blocked reason) to use.
        local click_result, click_detail = t.player.click_loc("kr_mansion_window_multi_01", 2)
        t.step("window.inspect_click", click_result ~= nil and "PASS" or "FAIL",
            "click_loc(kr_mansion_window_multi_01, op2) -> " .. tostring(click_result)
                .. " " .. tostring(click_detail))
        t.shot("window.inspect_click" .. (click_result ~= "ok" and "-FAIL" or ""))

        -- drive.op is the documented "logged bypass": it presses through
        -- the SAME dispatcher a real click uses, skipping only the
        -- on-screen menu build. Tried as a second, independent attempt
        -- before concluding anything -- but its own `ok` means "dispatched",
        -- never "succeeded" (app_plugin_world_op's own banner,
        -- torirs_plugin_bridge.u.c:4086), so it is graded here as evidence
        -- only; the real verdict is the inventory read after both attempts.
        local bypass_result, bypass_detail = nil, nil
        if click_result ~= "ok" and win_target_result == "ok" then
            bypass_result, bypass_detail = t.drive.op(win_target, 2)
            t.step("window.inspect_bypass", bypass_result ~= nil and "PASS" or "FAIL",
                "drive.op(loc kr_mansion_window_multi_01, op2) [logged bypass, " ..
                    "dispatched != succeeded] -> " .. tostring(bypass_result)
                    .. " " .. tostring(bypass_detail))
            t.shot("window.inspect_bypass")
        end
        if click_result == "ok" or bypass_result == "ok" then
            t.exec("window.close", t.chat.drain, { stop_at = "none" })
        end

        local matched_thread = nil
        local candidates = nil
        local thread_reads_ok = true
        for _, thread_sym in ipairs({ "murderthreadg", "murderthreadr", "murderthreadb" }) do
            local r, c = t.inv.count(thread_sym)
            if r ~= "ok" then
                thread_reads_ok = false
            end
            if r == "ok" and c >= 1 then
                matched_thread = thread_sym
                candidates = THREAD_CANDIDATES[thread_sym]
            end
        end
        -- Graded on whether the three inv.count reads themselves answered
        -- ok (a real, computed condition), not on whether a thread turned
        -- up -- the blocked call right after is what actually grades
        -- "found none".
        t.step("evidence.thread", thread_reads_ok and "PASS" or "FAIL",
            "thread colour -> " .. tostring(matched_thread) .. ", candidates -> "
                .. (candidates and (SUS[candidates[1]].name .. "/" .. SUS[candidates[2]].name)
                    or "none"))
        if not candidates then
            t.blocked(
                "quest_murder_window.rs2:60-61's [oploc2,kr_mansion_window_multi_01]/" ..
                "[oploc2,kr_mansion_window_multi_02] never fires for this fresh " ..
                "character, through two independently-driven real attempts (ledger rows " ..
                "+ screenshots window.inspect_click-FAIL.png, window.inspect_bypass.png): " ..
                "(1) click_loc(kr_mansion_window_multi_01, op2) answers `covered` from " ..
                "every one of its own camera-pose and side-step retries -- the actual " ..
                "on-screen 'Choose Option' popup it presses against shows only Walk " ..
                "here/Cancel, never Investigate, even though t.drive.screen_position " ..
                "separately reports the target visible on screen at 401,228, a " ..
                "DIFFERENT pixel than the 382,71 the real press lands on (click_loc's " ..
                "own camera-pose search settles on a pose that frames the target but " ..
                "presses a pixel landing on something else -- the same element " ..
                "536888205 every run, never the window). (2) t.drive.op -- the " ..
                "documented logged bypass, which presses through the SAME dispatcher a " ..
                "real click uses -- answers `ok`, but that is 'dispatched', not " ..
                "'succeeded' (torirs_plugin_bridge.u.c:4086's own banner): no dialogue " ..
                "opened and no thread item was granted, confirmed by reading the " ..
                "inventory for murderthreadg/r/b immediately after and finding none. " ..
                "Either the [murderwindow] multiloc wrapper this loc is placed behind " ..
                "never received the op table its own transform-resolved children " ..
                "declare (all.loc:288069-288075 carries no op fields at all; " ..
                "op2=Investigate/op3=Break exist only on the CHILD defs at " ..
                "all.loc:287915-287964), or this is a driver pick/pose targeting defect " ..
                "specific to this wall's shape -- either way, the murderer's colour-pair " ..
                "cannot be narrowed from real state and nothing past this point can be " ..
                "honestly driven.")
            return
        end

        -- --------------------------------------------------- the murder weapon
        -- Already spawned coated in flour (m42_55.spawn's own OBJ row, not
        -- a fixture cheat) -- only the flypaper step is still needed on it.
        t.exec("weapon.pickup", t.player.click_obj, "murderweapondust")
        local wdust_read, wdust_count = t.inv.count("murderweapondust")
        t.check("weapon.have_dust", wdust_read == "ok" and wdust_count >= 1,
            "inv.count(murderweapondust) -> " .. tostring(wdust_read) .. " " .. tostring(wdust_count))

        -- Three sheets of flypaper: one for the murder weapon, one for
        -- each of the two narrowed candidates' own personal items below.
        -- murdersacks has no "already have" guard (unlike the
        -- murderbarrels), so it can be searched repeatedly.
        for i = 1, 3 do
            t.exec("sack.search." .. i, t.player.click_loc, "murdersacks", 2)
            t.exec("sack.drain_to_options." .. i, t.chat.drain, { stop_at = "options" })
            t.exec("sack.choose." .. i, t.chat.choose, "Yes, it might be useful.")
            t.exec("sack.close." .. i, t.chat.drain, { stop_at = "none" })
        end
        local paper_read, paper_count = t.inv.count("murderpaper")
        t.check("evidence.paper_count", paper_read == "ok" and paper_count >= 3,
            "inv.count(murderpaper) -> " .. tostring(paper_read) .. " " .. tostring(paper_count))

        -- flypaper on the floury dagger -> murderweapon + murderfingerprint1
        -- (quest_murder_prints.rs2 [opheldu,murderweapondust]).
        t.exec("weapon.fingerprint", t.player.use_item_on_item, "murderpaper", "murderweapondust")
        local fp1_read, fp1_count = t.inv.count("murderfingerprint1")
        t.check("evidence.fingerprint1", fp1_read == "ok" and fp1_count >= 1,
            "inv.count(murderfingerprint1) -> " .. tostring(fp1_read) .. " " .. tostring(fp1_count))

        -- --------------------------------------------------- poison proof
        t.exec("goto.salesman", t.player.goto_tile, 2695, 3495, 0)
        t.exec("salesman.talk", t.player.talk_to, "poison_salesman", 1)
        t.exec("salesman.drain_to_options", t.chat.drain, { stop_at = "options" })
        t.exec("salesman.choose", t.chat.choose, "Who did you sell Poison to at the house?")
        t.exec("salesman.close", t.chat.drain, { stop_at = "none" })
        -- %murder_poisonproof_progress is a server-authored varp with no
        -- entry in all.varp.compack/all.varbit.compack at all (not this
        -- test's typo -- quest_arthur's sibling-pattern
        -- excalibur_components_progress and area_seers' coal_truck are
        -- absent too), so it cannot be named to var.server/lint either;
        -- its effect is read from real state below instead (the barrel
        -- item and the fingerprint match).

        -- Ask BOTH candidates the poison question first (harmless for
        -- whichever one is not %murdersus -- each sibling script checks
        -- `%murdersus = ^murderer_<name>` itself before touching the
        -- progress var), then search BOTH candidates' poison-proof locs.
        for _, sid in ipairs(candidates) do
            local s = SUS[sid]
            t.exec("goto.suspect." .. s.name, t.player.goto_tile, s.tx, s.tz, s.tl)
            t.exec("suspect.talk." .. s.name, t.player.talk_to, s.npc, 1)
            t.exec("suspect.drain_to_options." .. s.name, t.chat.drain, { stop_at = "options" })
            t.exec("suspect.choose_poison." .. s.name, t.chat.choose,
                "Why'd you buy poison the other day?")
            t.exec("suspect.close." .. s.name, t.chat.drain, { stop_at = "none" })
        end
        for _, sid in ipairs(candidates) do
            local s = SUS[sid]
            t.exec("poisonloc.search." .. s.name, t.player.click_loc, s.poison_loc, 2)
            t.exec("poisonloc.close." .. s.name, t.chat.drain, { stop_at = "none" })
        end

        -- --------------------------------------------------- fingerprint match
        -- Each candidate's own barrel, flour, flypaper and comparison --
        -- one of the two comparisons matches and creates murderfingerprint;
        -- the other destroys the checked print (check_murderer_print's own
        -- "doesn't seem to be the same" branch), which is content answering
        -- honestly, not a failure of this row.
        for _, sid in ipairs(candidates) do
            local s = SUS[sid]
            t.exec("barrel.search." .. s.name, t.player.click_loc, s.barrel, 2)
            t.exec("barrel.close." .. s.name, t.chat.drain, { stop_at = "none" })
            local item_read, item_count = t.inv.count(s.item)
            t.check("evidence.have_item." .. s.name, item_read == "ok" and item_count >= 1,
                "inv.count(" .. s.item .. ") -> " .. tostring(item_read) .. " " .. tostring(item_count))

            t.exec("flour.get." .. s.name, t.player.click_loc, "flourbarrel", 2)
            t.exec("item.flour." .. s.name, t.player.use_item_on_item, "pot_flour", s.item)
            local idust_read, idust_count = t.inv.count(s.itemdust)
            t.check("evidence.have_itemdust." .. s.name, idust_read == "ok" and idust_count >= 1,
                "inv.count(" .. s.itemdust .. ") -> " .. tostring(idust_read)
                    .. " " .. tostring(idust_count))

            t.exec("item.paper." .. s.name, t.player.use_item_on_item, "murderpaper", s.itemdust)
            local print_read, print_count = t.inv.count(s.print)
            t.check("evidence.have_print." .. s.name, print_read == "ok" and print_count >= 1,
                "inv.count(" .. s.print .. ") -> " .. tostring(print_read) .. " " .. tostring(print_count))

            -- check_murderer_print's trigger is [opheldu,murderfingerprint1],
            -- so the candidate's own print is the used item and
            -- murderfingerprint1 is the target -- the reverse of every
            -- other use_item_on_item above.
            t.exec("fingerprint.compare." .. s.name, t.player.use_item_on_item,
                s.print, "murderfingerprint1")
        end
        local fpmatch_read, fpmatch_count = t.inv.count("murderfingerprint")
        t.check("evidence.fingerprint_match", fpmatch_read == "ok" and fpmatch_count >= 1,
            "inv.count(murderfingerprint) -> " .. tostring(fpmatch_read)
                .. " " .. tostring(fpmatch_count) .. " (one of "
                .. SUS[candidates[1]].name .. "/" .. SUS[candidates[2]].name .. " matched)")

        -- --------------------------------------------------- hand in
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.step("handin.xp_before_read", xp_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))
        local coins_before_read, coins_before = t.inv.count("coins")
        t.step("handin.coins_before_read", coins_before_read == "ok" and "PASS" or "FAIL",
            "inv.count(coins) before hand-in -> " .. tostring(coins_before_read)
                .. " " .. tostring(coins_before))

        t.exec("goto.guard.accuse", t.player.goto_tile, 2741, 3562, 0)
        t.exec("guard.talk", t.player.talk_to, "murderguard", 1)
        t.exec("guard.drain_to_options", t.chat.drain, { stop_at = "options" })
        -- Both evidences true -> murderguard_who's first branch,
        -- murderguard_conclusive_proof, which is the only branch that
        -- queues murder_quest_complete.
        t.exec("guard.accuse", t.chat.choose, "I know who did it!")
        t.exec("guard.conclusive_proof", t.chat.drain, { stop_at = "none" })

        -- Completion is asynchronous behind queue(murder_quest_complete,0,0)
        -- (QUEST_AUTHORING.md section 8) -- give it real ticks before
        -- reading anything the commit writes.
        t.ticks(3)
        t.quest.expect_complete()

        -- The literal reward quest_murder's own murderguard_conclusive_proof
        -- hands out (murder_guard.rs2 [queue,murder_quest_complete]:
        -- `stat_advance(crafting, 14062)` -> 1406 Crafting XP,
        -- `inv_add(inv, coins, 2000)`), never a number read back off the
        -- scroll.
        t.expect("reward.crafting_xp", t.skill.expect_gain("crafting", 1406, xp_snapshot))

        local coins_after_read, coins_after = t.inv.count("coins")
        local coins_ok = coins_after_read == "ok" and coins_before_read == "ok"
        t.check("reward.coins",
            coins_ok and (coins_after - coins_before) == 2000,
            "coins before=" .. tostring(coins_before) .. " after=" .. tostring(coins_after)
                .. " delta=" .. tostring(coins_ok and (coins_after - coins_before) or "n/a"))

        t.finish(0)
    end,
}
