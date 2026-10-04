-- Cook's Assistant, end to end through the real client -- docs/
-- QUEST_SUITE_KIT.md phase 3, worker 3d; ingredient gathering re-driven for
-- real 2026-09-23 (helper_coverage.py flagged the debugproc-gathered
-- version as a CHEAT on every "Getting the X" panel).
--
-- HOW THIS FILE WAS MADE, precisely. `tools/quest_gate/new_quest.py
-- cooksassistant` emits a skeleton from the Quest Helper guide; that
-- skeleton was READ and then discarded for this one quest, and the D2-era
-- hand file was modernised onto the phase-2 verb kit instead (quest.bind,
-- t.exec, inv.await, skill.snapshot, quest.expect_complete). Checked against
-- content rather than guessed:
--
--   * Giving all three ingredients (`::give egg/pot_flour/bucket_milk`)
--     BEFORE ever talking to the Cook -- which the generator used to do in
--     `setup` -- is wrong for this quest. quest_cook.rs2's accept branch
--     ([label,cooks_assistant_whats_wrong] case 1) checks `inv_total` for
--     all three and jumps straight to `cooks_assistant_completion` in the
--     SAME Talk-to when they are already held -- so that setup collapses
--     the whole test into one dialogue and destroys the exact re-talk this
--     file exists to exercise (QUEST_SUITE_KIT phase 2d: player.talk_to's
--     re-talk fix, proved by this file staying green with no local
--     `talk_to_and_settle` shim). So the three ingredients are gathered
--     AFTER accepting (below, by real clicks: bought, picked, milled and
--     milked -- see the "gather the ingredients" section), and the hand-in
--     is driven through a real SECOND player.talk_to click, exactly the
--     shape `quest_cook_test_ingredients.rs2`'s own banner describes --
--     just without reaching for its debugproc, which hands over the three
--     FINISHED items and skips every step Quest Helper's guide lists for
--     getting them (a CHEAT, CLAUDE.md/trap 16: it remains a legitimate BMP
--     screenshot-rig adapter, just not evidence a playthrough happened).
--   * Hans has no Quest Helper guide at all, so the "regenerated from the
--     scaffold" half of the phase-3 proof holds for neither quest. What IS
--     proved here is the other half: the phase-2 verbs doing the work, and a
--     re-talk that mounts a genuinely different page. (fix_b57 added the
--     route helpers below -- tile_text, walk_check, pass_door, climb -- for the door,
--     gate and ladder rule; none of them wraps a dialogue verb.)
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

local function tile_text(r, tt)
    if r ~= "ok" then
        return tostring(r)
    end
    return tt.x .. "," .. tt.z .. "," .. tt.level
end

-- Walk to x,z on level 0 and grade the tile reached: within one tile of the
-- target, and on the side `side_ok` names (walk_to answers ok with no detail,
-- so the tile read is the evidence).
local function walk_check(t, name, x, z, side_ok, side_desc)
    local wr = t.player.walk_to(x, z, 40)
    local tr, tt = t.world.tile()
    t.check(name, tr == "ok" and tt.level == 0 and math.abs(tt.x - x) <= 1 and math.abs(tt.z - z) <= 1 and side_ok(tt),
        "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. "; tile " .. tile_text(tr, tt) .. " (want within 1, " .. side_desc .. ")")
end

-- Cross one door or gate on foot (the pass_door pattern of
-- docs/quest_authoring/sampler-findings.md, b56). Walk to the tile on this side;
-- if the closed leaf (closed_sym) stands on door_x,door_z, click THAT copy (op1
-- Open); otherwise an earlier press left it open (a door swings back after 500
-- ticks), so assert the open leaf (open_sym) stands within 2 tiles of the door
-- tile -- an opened leaf's loc shifts -- a row that fails when neither leaf is
-- there, and do not press it again. Then walk to the far side and grade the tile.
local function pass_door(t, prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
    walk_check(t, prefix .. ".atDoor", near_x, near_z, function() return true end,
        "this side of " .. closed_sym .. " at " .. door_x .. "," .. door_z)
    local cr, cd = t.world.loc_near(closed_sym, 3)
    if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z then
        t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
        t.ticks(1)
    else
        local orr, od = t.world.loc_near(open_sym, 3)
        t.check(prefix .. ".doorStandsOpen",
            orr == "ok" and math.abs(od.tile_x - door_x) <= 2 and math.abs(od.tile_z - door_z) <= 2,
            closed_sym .. " at " .. door_x .. "," .. door_z .. ": "
                .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z) or tostring(cr))
                .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z) or tostring(orr))
                .. " (want within 2 of the door tile: already standing open, so walked through, not pressed again)")
    end
    walk_check(t, prefix .. ".throughDoor", far_x, far_z, far_ok, far_desc)
end

-- One ladder of the mill (x 3163-3169 z 3303-3309), climbed by its own op.
-- click_loc settles on its teleport arm (start-and-travel: "Teleport locs settle
-- on the teleport arm"); the row is graded on the level read after it.
local function climb(t, name, sym, op, want_level)
    local cr, cd = t.player.click_loc(sym, op)
    -- From across the room the click settles on `map_flag` (the walk to the
    -- ladder) before the climb lands: wait for the level itself.
    local ar, ad = t.await({
        level = function()
            local lr, lv = t.world.level()
            return lr == "ok" and lv == want_level
        end,
        note = name .. " level " .. want_level,
    }, 10)
    local tr, tt = t.world.tile()
    t.check(name, cr == "ok" and tr == "ok" and tt.level == want_level
            and tt.x >= 3163 and tt.x <= 3169 and tt.z >= 3303 and tt.z <= 3309,
        "click_loc(" .. sym .. ", " .. op .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
            .. "; " .. tostring(ar) .. " " .. tostring(ad) .. "; tile " .. tile_text(tr, tt) .. " (want level " .. want_level .. " inside the mill)")
end

return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    setup = { "::cook" },

    -- A relay file (docs/quest_authoring/relay.md "Checkpoints"): the harness
    -- binds this table with t.quest.bind before the first leg that runs --
    -- after setup, before anything else touches %qp, so
    -- quest.expect_complete's points row has a true "before" reading, and a
    -- leg resumed from a checkpoint (run.py --from-leg K) is bound the same.
    bind = {
        varp = "varp29_cookquest",
        constants = { not_started = 0, started = 1, complete = 2 },
        row = "quest_cooksassistant",
        display = "Cook's Assistant",
        points = 1,
    },

    legs = {
        {
            name = "start",
            run = function(t)
                -- Leg 1: talk to the Cook, accept (stage not_started -> started).
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
            end,
        },
        {
            name = "gather",
            run = function(t)
                -- Leg 2: the four Quest Helper gathering panels (store, egg, flour,
                -- milk). Starts in the kitchen (where `::cook` stood the player) and
                -- ends outside the cow field, out of any dialogue: a quiet checkpoint.
                -- ------------------------------------------- gather the ingredients
                -- Driven for real. `::cookbmp_test_give_ingredients`
                -- (quest_cook_test_ingredients.rs2) hands over the three FINISHED
                -- items directly -- a screenshot-rig adapter, not a route this
                -- quest's own deliverable can be skipped through (trap 16). Every
                -- panel below is one of Quest Helper's own -- "Starting off",
                -- "Getting the Egg", "Getting the Flour", "Getting the Milk" --
                -- driven by the real click its .rs2 trigger names.
                --
                -- WALLS (fix_b57, orchestrator rule 2026-10-03): a goto_tile departs
                -- from and lands on an open street tile only. Every closed space on
                -- this route is entered and left on foot through its door, and every
                -- door is read off the map square (OSRS-Content maps/m49_51.jl2 /
                -- m50_50.jl2, checked with reports/sample_tools/reach.py and
                -- locs_near.py): the store's poshdoor (open in the map, 3214,3245),
                -- the chicken pen's fencegate_l (3181,3289, south edge), the wheat
                -- field's fencegate_l (3163,3290, south edge), the mill's
                -- castledoubledoorl (3166,3302, north edge) and the cow field's
                -- rustic_fencegate_l (3176,3315, north edge). Both gate leaves swing
                -- together (general_use/scripts/gates.rs2:105-106), as do both door
                -- leaves (doors/configs/doubledoors.loc:206-220). The mill's three
                -- floors are climbed by the ladders themselves -- climbLadderOne,
                -- climbLadderTwoUp, climbLadderThree, climbLadderTwoDown -- never a
                -- goto with a level argument.

                -- The kitchen opens on the castle's halls through doorways with no
                -- door (reach.py 3208,3215 -> 3222,3218: closed-doors, 27 tiles), so
                -- the player walks out to the courtyard before the first goto.
                walk_check(t, "cooksassistant.leaveKitchen", 3222, 3218,
                    function(tt) return tt.x >= 3220 end, "the castle courtyard, x >= 3220")

                -- ---- getBucket / getPot: buy from the Lumbridge General Store ----
                t.exec("cooksassistant.goto_store", t.player.goto_tile, 3217, 3244, 0) -- the street outside the store's east door
                pass_door(t, "cooksassistant.storeIn", "poshdoor", "poshdooropen", 3214, 3245, 3215, 3245, 3212, 3246,
                    function(tt) return tt.x <= 3213 end, "inside the store, x <= 3213")
                t.exec("cooksassistant.shop_open", t.shop.open, "generalshopkeeper1", 3, "generalshop1")
                t.exec("cooksassistant.buy_bucket", t.shop.buy, "bucket_empty", 1)
                t.exec("cooksassistant.buy_pot", t.shop.buy, "pot_empty", 1)
                -- shop.close takes no argument (trap 12's hollow-by-arity list, not
                -- the hollow-detail one) -- t.exec would grade it bad verb/target.
                local shop_close_result, shop_close_detail = t.shop.close()
                t.check("cooksassistant.shop_close", shop_close_result == "ok",
                    "shop.close -> " .. tostring(shop_close_result) .. " " .. tostring(shop_close_detail))
                pass_door(t, "cooksassistant.storeOut", "poshdoor", "poshdooropen", 3214, 3245, 3213, 3245, 3216, 3245,
                    function(tt) return tt.x >= 3215 end, "outside the store, x >= 3215")

                -- ------------------------- getEgg: pick one off the ground -------------------------
                -- The egg's OBJ spawn (m49_51.spawn, 3172,3301) is inside the chicken
                -- pen, x 3169-3186 z 3288-3307.
                t.exec("cooksassistant.goto_egg", t.player.goto_tile, 3183, 3286, 0) -- the lane south of the pen gate
                pass_door(t, "cooksassistant.penIn", "fencegate_l", "openfencegate_l", 3181, 3289, 3181, 3288, 3180, 3291,
                    function(tt) return tt.z >= 3289 end, "inside the pen, z >= 3289")
                local egg_walk_result = t.player.walk_to(3173, 3301, 30)
                -- click_obj answers ok with a nil detail (section 8's hollow list) --
                -- call it directly and write the count read back ourselves.
                local egg_click_result, egg_click_detail = t.player.click_obj("egg")
                local egg_have_read, egg_have_count = t.inv.count("egg")
                t.check("cooksassistant.take_egg", egg_click_result == "ok" and egg_have_count == 1,
                    "walk_to 3173,3301 -> " .. tostring(egg_walk_result)
                        .. "; click_obj(egg) -> " .. tostring(egg_click_result) .. " " .. tostring(egg_click_detail)
                        .. " egg=" .. tostring(egg_have_count) .. "(" .. tostring(egg_have_read) .. ")")
                pass_door(t, "cooksassistant.penOut", "fencegate_l", "openfencegate_l", 3181, 3289, 3181, 3290, 3181, 3287,
                    function(tt) return tt.z <= 3288 end, "outside the pen, z <= 3288")

                -- ------------------------------- getWheat: the wheat field -------------------------------
                pass_door(t, "cooksassistant.wheatIn", "fencegate_l", "openfencegate_l", 3163, 3290, 3163, 3289, 3162, 3291,
                    function(tt) return tt.z >= 3290 end, "inside the wheat field, z >= 3290")
                t.exec("cooksassistant.pick_wheat", t.player.click_loc, "fai_varrock_wheat_corner", 2, { at = { 3161, 3292 } }) -- op2=Pick
                t.exec("cooksassistant.have_grain", t.inv.await, "grain", 1, 10)
                pass_door(t, "cooksassistant.wheatOut", "fencegate_l", "openfencegate_l", 3163, 3290, 3162, 3291, 3163, 3288,
                    function(tt) return tt.z <= 3289 end, "outside the wheat field, z <= 3289")

                -- ------- the mill: fillHopper / operateControls / collectFlour -------
                pass_door(t, "cooksassistant.millIn", "castledoubledoorl", "opencastledoubledoorl", 3166, 3302, 3166, 3302, 3166, 3304,
                    function(tt) return tt.z >= 3303 end, "inside the mill, z >= 3303")
                climb(t, "cooksassistant.climbLadderOne", "qip_cook_ladder", 1, 1)       -- op1=Climb-up, 0 -> 1
                climb(t, "cooksassistant.climbLadderTwoUp", "qip_cook_ladder_middle", 2, 2) -- op2=Climb-up, 1 -> 2

                t.exec("cooksassistant.fill_hopper", t.player.click_loc, "hopper1", 1) -- op1=Fill, spends inv grain
                -- inv.await(name, 0, ticks) never actually waits (total >= 0 is
                -- always true -- section 8) -- poll the consumption with a real
                -- level predicate instead.
                local hopper_settle_result, hopper_settle_detail = t.await({
                    level = function()
                        local r, v = t.inv.count("grain")
                        return r == "ok" and v == 0
                    end,
                    note = "cooksassistant.hopper_settle",
                }, 10)
                local grain_after_read, grain_after = t.inv.count("grain")
                t.step("cooksassistant.grain_consumed",
                    (hopper_settle_result == "ok" and grain_after == 0) and "PASS" or "FAIL",
                    "grain consumed by the hopper fill within 10 tick(s) (" .. tostring(hopper_settle_result)
                        .. ") " .. tostring(hopper_settle_detail)
                        .. " grain=" .. tostring(grain_after) .. "(" .. tostring(grain_after_read) .. ")")

                t.exec("cooksassistant.operate_levers", t.player.click_loc, "hopperlevers1", 1) -- op1=Operate, grinds the hopper's grain

                climb(t, "cooksassistant.climbLadderThree", "qip_cook_ladder_top", 1, 1)     -- op1=Climb-down, 2 -> 1
                climb(t, "cooksassistant.climbLadderTwoDown", "qip_cook_ladder_middle", 3, 0) -- op3=Climb-down, 1 -> 0

                -- The multiloc WRAPPER symbol, not either child (trap 20): the
                -- server resolves op1 on it to whichever child this player's own
                -- %mill_showflour currently selects.
                t.exec("cooksassistant.collect_flour", t.player.click_loc, "millbase", 1) -- op1=Empty (millbase_flour's own label), spends inv pot_empty
                t.exec("cooksassistant.have_flour", t.inv.await, "pot_flour", 1, 10)
                pass_door(t, "cooksassistant.millOut", "castledoubledoorl", "opencastledoubledoorl", 3166, 3302, 3166, 3303, 3166, 3301,
                    function(tt) return tt.z <= 3302 end, "outside the mill, z <= 3302")

                -- -------------------------- milkCow: milk the dairy cow --------------------------
                -- fat_cow (3172,3317, a solid loc) is inside the rustic-fenced field.
                pass_door(t, "cooksassistant.cowFieldIn", "rustic_fencegate_l", "rustic_openfencegate_l", 3176, 3315, 3176, 3315, 3175, 3317,
                    function(tt) return tt.z >= 3316 end, "inside the cow field, z >= 3316")
                t.exec("cooksassistant.milk_cow", t.player.click_loc, "fat_cow", 1) -- op1=Milk, spends inv bucket_empty
                t.exec("cooksassistant.have_milk", t.inv.await, "bucket_milk", 1, 10)
                pass_door(t, "cooksassistant.cowFieldOut", "rustic_fencegate_l", "rustic_openfencegate_l", 3176, 3315, 3176, 3316, 3176, 3313,
                    function(tt) return tt.z <= 3315 end, "outside the cow field, z <= 3315")

                -- The three deliverables, read back together as the hand-in gate.
                local egg_read, egg_count = t.inv.count("egg")
                local milk_read, milk_count = t.inv.count("bucket_milk")
                local flour_read, flour_count = t.inv.count("pot_flour")
                t.check("cooksassistant.has_ingredients",
                    egg_count == 1 and milk_count == 1 and flour_count == 1,
                    "egg=" .. tostring(egg_count) .. "(" .. tostring(egg_read) .. ")"
                        .. " bucket_milk=" .. tostring(milk_count) .. "(" .. tostring(milk_read) .. ")"
                        .. " pot_flour=" .. tostring(flour_count) .. "(" .. tostring(flour_read) .. ")")
            end,
        },
        {
            name = "handin",
            run = function(t)
                -- Leg 3: back to the kitchen, the hand-in, the reward scroll.
                -- ----------------------------------------------------- hand in
                -- The gathering above (store, egg field, wheat field, mill, cow
                -- field) walked the player clear across Lumbridge, so -- unlike the
                -- old debugproc-gathered version, which never moved the player away
                -- from the cook at all -- a real goto back to the kitchen is needed
                -- before the second talk_to (^cook_coord = 0_50_50_6_14 decodes to
                -- 3206,3214,0, the same tile Quest Helper's own finishQuest
                -- WorldPoint names). The goto departs from the lane outside the cow
                -- field (the gather leg walked out through its gate) and lands in
                -- the castle courtyard; the kitchen is walked into through the
                -- castle's doorless halls (reach.py 3222,3218 -> 3207,3215:
                -- closed-doors, 26 tiles), never teleported into.
                t.exec("cooksassistant.goto_handin", t.player.goto_tile, 3222, 3218, 0)
                walk_check(t, "cooksassistant.enterKitchen", 3207, 3215,
                    function(tt) return tt.x >= 3205 and tt.x <= 3212 and tt.z >= 3212 and tt.z <= 3217 end,
                    "in the kitchen, x 3205-3212 z 3212-3217")

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
                -- (No row of its own: a status-only grade of a read cannot fail
                -- for the quest's sake. A snapshot that failed makes
                -- cooking_xp_up below fail, which is where it matters.)
                local xp_snapshot_result, xp_snapshot = t.skill.snapshot()

                -- Dismissing the mesbox is what runs the one atomic transaction
                -- (quest_cook.rs2's ~cooks_assistant_commit): state, xp and qp all
                -- move here, then the reward scroll opens. NOT through t.exec:
                -- continue_ takes no target and its own success answer is (ok,
                -- nil), which the hollow rule would grade FAIL -- so its answer is
                -- graded inside commit_settle below, together with the commit it
                -- triggers, rather than on a row with a made-up detail.
                local continue_result, continue_detail = t.chat.continue_()

                -- The commit's own varp/inventory updates reach the client-visible
                -- copies (and var.server's own read) some ticks after the click
                -- that triggered them -- an honest bounded await rather than a
                -- guessed sleep, so a genuine stuck commit reads as a named FAIL
                -- here rather than surfacing as an unrelated-looking failure in
                -- quest.expect_complete below.
                local commit_settle_result, commit_settle_detail = t.await({
                    level = function()
                        local r, v = t.var.server("varp29_cookquest")
                        return r == "ok" and v == 2
                    end,
                    note = "cooksassistant.commit_settle",
                }, 20)
                t.step("cooksassistant.commit_settle",
                    (continue_result == "ok" and commit_settle_result == "ok") and "PASS" or "FAIL",
                    "chat.continue_ on the completion mesbox -> " .. tostring(continue_result) .. " " .. tostring(continue_detail)
                        .. "; server cookquest -> 2 within 20 ticks (" .. tostring(commit_settle_result)
                        .. ") " .. tostring(commit_settle_detail)
                        .. "; skill.snapshot before it -> " .. tostring(xp_snapshot_result))

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
                -- The documented reward, literally: 300 Cooking XP
                -- (quest_cook.rs2:157 `stat_advance(cooking, 3000)` in tenths, and
                -- :158's scroll line "300 Cooking XP"). The scroll must advertise
                -- exactly that, read off the component before anything closes it.
                local REWARD_COOKING_XP = 300
                local reward_result, reward_xp = t.scroll.reward_xp("cooking")
                t.check("cooksassistant.scroll_reward_xp",
                    reward_result == "ok" and reward_xp == REWARD_COOKING_XP,
                    "scroll.reward_xp(cooking) -> " .. tostring(reward_result)
                        .. " " .. tostring(reward_xp) .. " Cooking XP (want " .. REWARD_COOKING_XP .. ")")

                -- And the stat moved by exactly that literal amount. skill.expect_gain
                -- accepts either unit (whole xp, or the server's xp*10 tenths) and
                -- names which one it matched.
                t.expect("cooksassistant.cooking_xp_up",
                    t.skill.expect_gain("cooking", REWARD_COOKING_XP, xp_snapshot))

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
        },
    },
}
