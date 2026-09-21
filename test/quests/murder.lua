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
        -- A `covered` right after a goto_tile teleport is the scene/camera
        -- not having settled yet (QUEST_AUTHORING.md section 8), so the
        -- press gets two ticks of world before it.
        t.ticks(2)

        -- The press. sonnet-b5's run recorded this as "never fires for this
        -- fresh character", and the diagnosis was wrong about WHY: the
        -- window is placed as the multiloc [murderwindow], and
        -- interaction_try used to resolve the varbit transform and then look
        -- the trigger up on the resolved CHILD only -- so
        -- [oploc2,kr_mansion_window_multi_01] was dead code that answered
        -- with content's own ^dm_default sentence. 73a4251d0's
        -- run_loc_trigger_with_base tries the child, then the BASE, and the
        -- op now reaches murder_inspect_window; the ledger row carries
        -- click_loc's own "-> murderwindow (base)" as the proof of which arm
        -- found it.
        t.exec("window.inspect_click", t.player.click_loc, "kr_mansion_window_multi_01", 2)

        -- The thread is NOT handed over with the mesbox. murder_inspect_window
        -- (quest_murder_window.rs2:63-81) opens
        -- `~mesbox("Some thread seems to have been caught on a loose nail on
        -- the window.")`, which SUSPENDS, and only the resume runs
        -- `mes("You take the thread.")` + `inv_add(inv, ~get_murder_thread,
        -- 1)`. So the chain is walked to its end -- a close here skips the
        -- grant, which is what the first post-seam run actually did.
        t.exec("window.mesbox", t.chat.drain, { stop_at = "none" })

        -- ...and the grant is AWAITED, not read on the tick the drain
        -- returns. Measured 2026-09-20 (build/quest_gate/murder_probe, a
        -- scratch probe that read all three colours once per tick after the
        -- same drain): `t+0: murderthreadg=ok/0 murderthreadr=ok/0
        -- murderthreadb=ok/0 | t+1: murderthreadr=ok/1`. One tick.
        -- There is no inv.await over an ALTERNATIVE set -- which colour
        -- lands is %murdersus's random roll and is the whole reading this
        -- step exists to take -- so this is a bounded poll over the three,
        -- and the tick it landed on goes in the detail.
        local matched_thread = nil
        local candidates = nil
        local thread_reads_ok = true
        local thread_tick = nil
        for tick = 0, 8 do
            for _, thread_sym in ipairs({ "murderthreadg", "murderthreadr", "murderthreadb" }) do
                local r, c = t.inv.count(thread_sym)
                if r ~= "ok" then
                    thread_reads_ok = false
                elseif c >= 1 and matched_thread == nil then
                    matched_thread = thread_sym
                    candidates = THREAD_CANDIDATES[thread_sym]
                    thread_tick = tick
                end
            end
            if matched_thread ~= nil then
                break
            end
            t.ticks(1)
        end
        t.step("evidence.thread", (thread_reads_ok and candidates ~= nil) and "PASS" or "FAIL",
            "thread colour -> " .. tostring(matched_thread) .. " after "
                .. tostring(thread_tick) .. " tick(s), candidates -> "
                .. (candidates and (SUS[candidates[1]].name .. "/" .. SUS[candidates[2]].name)
                    or "none"))
        t.shot("evidence.thread" .. (candidates and "" or "-FAIL"))
        if not candidates then
            t.blocked(
                "murder_inspect_window (quest_murder_window.rs2:63-81) ran its " ..
                "mesbox but granted no thread within 8 ticks of the chain ending: " ..
                "none of murderthreadg/r/b turned up in the backpack. Without a " ..
                "colour, %murdersus -- which quest_murder.varp declares with no " ..
                "transmit body, so var.server reads 0 for it forever -- cannot be " ..
                "narrowed to a pair from real state, and every evidence step below " ..
                "would be guessing at the murderer instead of letting content " ..
                "answer.")
            return
        end

        -- ---------------------------------------------- reaching the evidence
        -- Every loc and ground obj from here on is FOUND IN THE LIVE SCENE
        -- and teleported to, never pressed from wherever the step before
        -- happened to leave the player. That is what the first post-seam run
        -- of this file (2026-09-20, 43 PASS / 35 FAIL) was actually failing
        -- on: with the window seam fixed the quest ran forty rows further and
        -- every one of those failures reads the same way -- `I can't reach
        -- that! -- and no other approach tile of murdersacks (8 known) could
        -- be walked to`, or `menu has no row for it -- menu rows: <Cancel>
        -- <Walk here>` -- a loc rooms away from the player, not a loc that
        -- refused. `::goto` teleports without a walkability test
        -- (torirs_server_world.c:9520, straight to ToriRSServer_WorldTeleport),
        -- so landing ON the loc's own south-west tile is legal and is the
        -- best place to press from: click_loc's own _step_off_for_click and
        -- 73a4251d0's _reach_retry both work outward from the target's
        -- square, and from on top of it every approach tile is one step away.
        local function goto_loc(label, sym, fx, fz, fl)
            local find_result, row = t.world.loc_near(sym, 40)
            if find_result == "ok" and type(row) == "table" then
                t.exec("goto." .. label, t.player.goto_tile, row.tile_x, row.tile_z, row.level)
                t.ticks(2)
                return true
            end
            -- The fallback is a tile this quest already knows from content
            -- (a suspect's own spawn row), used only when the loc is not in
            -- the loaded scene yet -- which is itself the diagnosis, so it
            -- goes in the row's detail rather than being swallowed.
            if fx then
                t.note("world.loc_near(" .. sym .. ", 40) -> " .. tostring(find_result)
                    .. "; falling back to the spawn tile " .. tostring(fx) .. ","
                    .. tostring(fz) .. "," .. tostring(fl))
                t.exec("goto." .. label, t.player.goto_tile, fx, fz, fl)
                t.ticks(2)
                return true
            end
            t.step("locate." .. label, "FAIL",
                "world.loc_near(" .. sym .. ", 40) -> " .. tostring(find_result)
                    .. " " .. tostring(row))
            t.shot("locate." .. label .. "-FAIL")
            return false
        end

        -- --------------------------------------------------- the murder weapon
        -- Already spawned coated in flour (m42_55.spawn:46's own OBJ row at
        -- 2746,3578,0, not a fixture cheat) -- only the flypaper step is
        -- still needed on it. It has no [opobj*] trigger of its own, so the
        -- press is the engine's plain Take.
        local weapon_result, weapon_row = t.world.obj_near("murderweapondust", 40)
        t.step("weapon.locate", weapon_result == "ok" and "PASS" or "FAIL",
            "world.obj_near(murderweapondust, 40) -> " .. tostring(weapon_result)
                .. (type(weapon_row) == "table" and (" @" .. tostring(weapon_row.tile_x)
                    .. "," .. tostring(weapon_row.tile_z) .. "," .. tostring(weapon_row.level)) or ""))
        t.shot("weapon.locate" .. (weapon_result == "ok" and "" or "-FAIL"))

        -- The dagger lies inside the study and the study is behind a door:
        -- the first post-seam run pressed it from 2748,3575, outside the
        -- east wall, and the engine answered "I can't reach that!" across
        -- the whole shot (21-weapon.pickup-FAIL.png). A teleport onto its
        -- own square puts the player in the room, and Take routes onto the
        -- stack's square by itself -- but a stack at the player's FEET
        -- projects off the bottom of the viewport at a flat pitch
        -- (click_obj's own banner), so two viewing tiles are held in
        -- reserve behind the square itself and the vantage that answered
        -- goes in the row. The press is one row whatever it took: a retry
        -- that is EXPECTED to be needed sometimes is not a failure to
        -- record, only a detail to name.
        local weapon_press, weapon_press_detail = "not_found", "murderweapondust is not in the scene"
        if weapon_result == "ok" and type(weapon_row) == "table" then
            local vantages = { { 0, 0 }, { 0, -2 }, { 2, 0 } }
            local tried = {}
            for i = 1, #vantages do
                t.exec("goto.weapon." .. i, t.player.goto_tile,
                    weapon_row.tile_x + vantages[i][1],
                    weapon_row.tile_z + vantages[i][2], weapon_row.level)
                t.ticks(2)
                weapon_press, weapon_press_detail = t.player.click_obj("murderweapondust")
                tried[#tried + 1] = "+" .. tostring(vantages[i][1]) .. ","
                    .. tostring(vantages[i][2]) .. " -> " .. tostring(weapon_press)
                if weapon_press == "ok" then
                    break
                end
            end
            weapon_press_detail = "click_obj(murderweapondust): "
                .. table.concat(tried, " | ") .. " -- " .. tostring(weapon_press_detail)
        end
        t.step("weapon.pickup", weapon_press == "ok" and "PASS" or "FAIL", weapon_press_detail)
        t.shot("weapon.pickup" .. (weapon_press == "ok" and "" or "-FAIL"))
        -- inv.await, never inv.count: an inv_add lands on the tick AFTER the
        -- press the client drained (measured 2026-09-20,
        -- build/quest_gate/murder_probe: the thread reads 0 on the drain's
        -- own tick and 1 on the next). Every inventory assertion below is an
        -- await for the same reason -- the committed file read all of them
        -- bare and two of its FAIL rows were nothing but that one tick.
        t.expect("weapon.have_dust", t.inv.await("murderweapondust", 1, 6))

        -- Three sheets of flypaper: one for the murder weapon, one for
        -- each of the two narrowed candidates' own personal items below.
        -- murdersacks has no "already have" guard (unlike the
        -- murderbarrels), so it can be searched repeatedly.
        goto_loc("sacks", "murdersacks")
        for i = 1, 3 do
            t.exec("sack.search." .. i, t.player.click_loc, "murdersacks", 2)
            t.exec("sack.drain_to_options." .. i, t.chat.drain, { stop_at = "options" })
            t.exec("sack.choose." .. i, t.chat.choose, "Yes, it might be useful.")
            t.exec("sack.close." .. i, t.chat.drain, { stop_at = "none" })
        end
        t.expect("evidence.paper_count", t.inv.await("murderpaper", 3, 8))

        -- flypaper on the floury dagger -> murderweapon + murderfingerprint1
        -- (quest_murder_prints.rs2 [opheldu,murderweapondust]).
        t.exec("weapon.fingerprint", t.player.use_item_on_item, "murderpaper", "murderweapondust")
        t.expect("evidence.fingerprint1", t.inv.await("murderfingerprint1", 1, 6))

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
        -- whichever one is not %murdersus -- each sibling script's own
        -- option-4 arm ends `if (%murdersus = ^murderer_<name> &
        -- %murder_poisonproof_progress = ^poisonproof_spoken_salesman)`
        -- before it writes anything), then search BOTH candidates'
        -- poison-proof locs -- again only the murderer's own arm writes
        -- ^poisonproof_searched_loc, which is what murderguard_who reads.
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
            if goto_loc("poisonloc." .. s.name, s.poison_loc) then
                t.exec("poisonloc.search." .. s.name, t.player.click_loc, s.poison_loc, 2)
                t.exec("poisonloc.close." .. s.name, t.chat.drain, { stop_at = "none" })
            end
        end

        -- --------------------------------------------------- fingerprint match
        -- Each candidate's own barrel, flour, flypaper and comparison.
        -- check_murderer_print consumes murderfingerprint1 on a MATCH
        -- (`inv_del(inv, murderfingerprint1, 1)` then
        -- `inv_add(inv, murderfingerprint, 1)`) and destroys the CHECKED
        -- print on a miss, so the loop stops the moment the match lands --
        -- a second comparison after it would have no murderfingerprint1 to
        -- use and would fail on this test's own arithmetic, not on content.
        local matched_suspect = nil
        for _, sid in ipairs(candidates) do
            local s = SUS[sid]
            -- The suspect's own spawn tile first: it puts the player in
            -- that suspect's room and on that suspect's PLANE (Carol's and
            -- Elizabeth's barrels are upstairs), which is what puts the
            -- barrel in the scene for loc_near to find at all.
            t.exec("goto.suspect.room." .. s.name, t.player.goto_tile, s.tx, s.tz, s.tl)
            goto_loc("barrel." .. s.name, s.barrel, s.tx, s.tz, s.tl)
            t.exec("barrel.search." .. s.name, t.player.click_loc, s.barrel, 2)
            t.exec("barrel.close." .. s.name, t.chat.drain, { stop_at = "none" })
            t.expect("evidence.have_item." .. s.name, t.inv.await(s.item, 1, 6))

            goto_loc("flourbarrel." .. s.name, "flourbarrel")
            t.exec("flour.get." .. s.name, t.player.click_loc, "flourbarrel", 2)
            t.exec("flour.close." .. s.name, t.chat.drain, { stop_at = "none" })
            t.expect("evidence.have_flour." .. s.name, t.inv.await("pot_flour", 1, 6))

            t.exec("item.flour." .. s.name, t.player.use_item_on_item, "pot_flour", s.item)
            t.expect("evidence.have_itemdust." .. s.name, t.inv.await(s.itemdust, 1, 6))

            t.exec("item.paper." .. s.name, t.player.use_item_on_item, "murderpaper", s.itemdust)
            t.expect("evidence.have_print." .. s.name, t.inv.await(s.print, 1, 6))

            -- check_murderer_print's trigger is [opheldu,murderfingerprint1],
            -- so the candidate's own print is the used item and
            -- murderfingerprint1 is the target -- the reverse of every
            -- other use_item_on_item above.
            t.exec("fingerprint.compare." .. s.name, t.player.use_item_on_item,
                s.print, "murderfingerprint1")
            t.exec("fingerprint.close." .. s.name, t.chat.drain, { stop_at = "none" })
            local match_result = t.inv.await("murderfingerprint", 1, 5)
            if match_result == "ok" then
                matched_suspect = s.name
                break
            end
        end
        t.step("evidence.fingerprint_match", matched_suspect and "PASS" or "FAIL",
            "murderfingerprint -> " .. tostring(matched_suspect) .. " (of "
                .. SUS[candidates[1]].name .. "/" .. SUS[candidates[2]].name
                .. "; content's own match/mismatch decided it, not this test)")
        t.shot("evidence.fingerprint_match" .. (matched_suspect and "" or "-FAIL"))

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
