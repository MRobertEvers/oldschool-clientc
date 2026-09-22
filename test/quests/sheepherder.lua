-- Sheep Herder quest test.
-- Scaffolded from Quest Helper's helpers/quests/sheepherder/, rewritten
-- against the real content scripts under
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_sheepherder/.
-- Tier (quest_inventory.tsv): 1.
--
-- Rewards (quest_sheepherder.rs2 [queue,sheepherder_complete]): 3100 coins,
-- 4 quest points, no item, no xp. No CHECK markers remain.
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge, beside Hans). 100 coins is a genuine prerequisite -- Doctor
-- Orbon charges it for the plague suit and nothing in this quest's own
-- scripts lets the player earn coins, so it is given in setup, never
-- during run() (rule: setup STAGES, run() drives).
--
-- RESUMED after RETRY 1858fe69a: t.player.press(npc, op, ticks) settles on
-- THE NPC MOVING ([label,prod_sheep] in diseased_sheep.rs2 is anim +
-- npc_say overhead text + npc_walk, no chat line -- talk_to timed out ON
-- SUCCESS, which is what the earlier t.blocked below this banner used to
-- report). The herd loop below drives every prod through t.player.press,
-- not talk_to, and re-picks which side to stand on every attempt off
-- t.npc.tiles's own live readback -- never a hand-coded route.
--
-- RESUMED AGAIN after RETRY 486398e09: the BLOCKED banner this file used to
-- end on ("EVERY direction timed out identically ... npc silent") was
-- wrong -- press's overhead read (DriveNpcRow.overhead) means a landed prod
-- now answers 'ok' inside its own tick budget instead of timing out, and
-- queue.py's own re-run of this committed file showed 0 timeout presses,
-- every one reading "ok ... it said BAAAAA! and did not move" against a
-- MAP wall (m40_52.jm2 marks 2598,3345 and 2599,3344 f1 unwalkable), not an
-- unresponsive npc. The herd loop below now reads all FOUR of
-- t.player.press's outcomes by name (trap 21): 'ok'+"away from you" is a
-- real step; 'ok' with no movement is the push LANDING against a wall, and
-- that exact (tile, axis) pair is blacklisted so the loop never repeats a
-- push it already knows is walled -- it turns to the other axis THE SAME
-- ATTEMPT instead of burning the streak/resync budget rediscovering the
-- same wall; 'refused' is out of range; 'timeout' is a press that may not
-- have landed at all. This is a tick-budget question as much as a
-- correctness one: the unblocked copy's first three sheep burned
-- 1951 ticks of a ~2,000-tick session on repeated walled pushes (85 of 168
-- presses), so cutting those out is load-bearing for whether four sheep
-- fit the run at all.
--
-- Herd mechanics, read from diseased_sheep.rs2/sheepherder_area.dbrow: a
-- prod moves the target sheep ONE tile along the DOMINANT axis of
-- (player -> sheep) ([proc,coord_direction]/movecoord_indirection), i.e.
-- standing east of the sheep pushes it west and standing south pushes it
-- north (and the reverse for the other two cardinals) -- so the loop picks
-- a STANDING TILE, not a push direction. sheepherder_pen_gate
-- (2592-2594,3360-3363) is the narrow zone diseased_sheep.rs2 tests to
-- trigger the auto-jump/npc_add into the enclosure; sheepherder_in_pen
-- (2595-2609,3351-3364) is the much larger enclosure the pen sits inside.
-- A naive west-then-north route (tried in the prior BLOCKED attempt's own
-- evidence, see queue.py's last_failure) jams the sheep against the
-- enclosure's outer south-west fence corner at 2595,3350, ten tiles short
-- of the gate mouth -- so this loop closes the Z gap FIRST, while the
-- sheep is still outside the enclosure's own x-span (< 2595 or > 2609),
-- before ever pushing it west/east into the gate's narrow x-band, going
-- around the fence rather than trying to turn a corner against it.
return {
    id = "sheepherder",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::sheepherder",
        "::give coins 100", -- Doctor Orbon's plague-suit price; not obtainable in-quest
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "sheepherderquest",
            constants = {
                complete = 3,
                not_started = 0,
                questpoints = 4,
                tasked_with_disposing_of_sheep = 2,
                tasked_with_talking_to_dr_orbon = 1,
            },
            row = "quest_sheepherder",
            display = "Sheep Herder",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effects are not client-side yet

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Councillor Halgrive, outside the East Ardougne church.
        t.exec("goto-talkToHalgrive", t.player.goto_tile, 2615, 3298, 0)
        t.exec("talkToHalgrive", t.player.talk_to, "councillor_halgrive")
        t.exec("talkToHalgrive-dialog", t.chat.play, {
            "player:Hello.",
            "npc:I've been better.",
            "choose:What's wrong?",
            "player:What's wrong?",
            "npc:You may or may not be aware,",
            "npc:However, four sheep recently e",
            "npc:They believe that the sheep ha",
            "npc:As the councillor responsible ",
            "npc:Unfortunately nobody wants to ",
            "choose:I can do that for you.",
            "player:I can do that for you.",
            "npc:Y-you will??? That is excellen",
            "npc:Before you will be allowed to ",
            "player:Where can I find some protecti",
            "npc:Doctor Orbon wears it when con",
            "npc:Please also take this poisoned",
            "mesbox:The councillor gives you some ",
            "player:How will I know which sheep ar",
            "npc:The poor creatures have develo",
        })
        t.expect("quest.stage.tasked_with_talking_to_dr_orbon", t.quest.expect_stage("tasked_with_talking_to_dr_orbon"))
        t.check("halgrive.feed_granted", select(2, t.inv.count("poisoned_feed")) == 1, string.format("poisoned_feed count=%s after Halgrive's accept", tostring(select(2, t.inv.count("poisoned_feed")))))

        -- Doctor Orbon, in the chapel just north of the church. 100 coins
        -- (setup) buys the plague jacket and trousers outright, so the
        -- branch below is the successful-purchase page order, not the
        -- "not enough money" one.
        t.exec("goto-talkToOrbon", t.player.goto_tile, 2614, 3306, 0)
        t.exec("talkToOrbon", t.player.talk_to, "doctor_orbon")
        t.exec("talkToOrbon-dialog", t.chat.play, {
            "player:Hi Doctor.",
            "player:I need to acquire some protect",
            "npc:Protective clothing? I'm afrai",
            "npc:I suppose I could sell you thi",
            "choose:Ok, I'll take it.",
            "player:Ok, I'll take it.",
            "mesbox:You give Doctor Orbon 100 coin",
            "npc:These should protect you from",
        })
        t.expect("quest.stage.tasked_with_disposing_of_sheep", t.quest.expect_stage("tasked_with_disposing_of_sheep"))
        t.ticks(1) -- the purchase's inv_del/inv_add lands a tick after the mesbox page (measured run 1: read here was stale)
        t.check("orbon.gear_bought", select(2, t.inv.count("plague_jacket")) == 1 and select(2, t.inv.count("plague_trousers")) == 1, string.format("plague_jacket=%s plague_trousers=%s coins=%s after purchase", tostring(select(2, t.inv.count("plague_jacket"))), tostring(select(2, t.inv.count("plague_trousers"))), tostring(select(2, t.inv.count("coins")))))

        -- Wear the suit -- sheepherder_gate.rs2 and diseased_sheep.rs2 both
        -- check `worn`, not the backpack.
        t.exec("equip-jacket", t.player.equip, "plague_jacket")
        t.exec("equip-trousers", t.player.equip, "plague_trousers")

        -- The cattleprod is a ground item in Farmer Brumty's barn
        -- (m40_52.spawn), not handed over in any dialogue. click_obj answers
        -- `ok` with a nil detail on this pick-up (measured run 1), the
        -- hollow shape trap 12 names -- called directly, not through
        -- t.exec, with the before/after backpack count as the real detail.
        t.exec("goto-barn", t.player.goto_tile, 2604, 3357, 0)
        local cattleprod_before_result, cattleprod_before = t.inv.count("cattleprod")
        local pickup_result, pickup_detail = t.player.click_obj("cattleprod")
        local cattleprod_after_result, cattleprod_after = t.inv.count("cattleprod")
        t.step("pickupCattleprod", (pickup_result == "ok" and cattleprod_after_result == "ok" and cattleprod_after > (cattleprod_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj -> %s (%s); cattleprod %s -> %s", tostring(pickup_result), tostring(pickup_detail), tostring(cattleprod_before), tostring(cattleprod_after)))
        t.exec("equip-cattleprod", t.player.equip, "cattleprod")

        -- ---------------------------------------------------------------
        -- Herd all four wild sheep into the enclosure. sheep_table
        -- (sheepherder_sheep_data.dbrow) ties each colour to its own
        -- sheepherdervar 3-bit lane (read back through the packed
        -- sheepherder_sheep_a..d varbits) and its own bones drop --
        -- start_bit 1/4/7/10, bones sheepbonesa..d. Each entry's goto is
        -- a tile a couple of squares off that colour's own *.spawn rows
        -- (m40_52.spawn), close enough for t.npc.tiles to find it without
        -- landing the player on top of it.
        local sheep_defs = {
            { id = "1", npc = "plaguesheep_1", enclosure = "herder_plaguesheep_1_enclosure", bitvar = "sheepherder_sheep_a", bones = "sheepbonesa", goto_x = 2612, goto_z = 3342 },
            { id = "2", npc = "plaguesheep_2", enclosure = "herder_plaguesheep_2_enclosure", bitvar = "sheepherder_sheep_b", bones = "sheepbonesb", goto_x = 2624, goto_z = 3369 },
            { id = "3", npc = "plaguesheep_3", enclosure = "herder_plaguesheep_3_enclosure", bitvar = "sheepherder_sheep_c", bones = "sheepbonesc", goto_x = 2558, goto_z = 3391 },
            { id = "4", npc = "plaguesheep_4", enclosure = "herder_plaguesheep_4_enclosure", bitvar = "sheepherder_sheep_d", bones = "sheepbonesd", goto_x = 2613, goto_z = 3393 },
        }
        local GATE_X_MIN, GATE_X_MAX = 2592, 2594
        local GATE_Z_MIN, GATE_Z_MAX = 3360, 3363

        for _, def in ipairs(sheep_defs) do
            t.exec("goto-herd" .. def.id, t.player.goto_tile, def.goto_x, def.goto_z, 0)

            local pressed = 0
            -- RESUMED AGAIN, run 6: "x" first (the prior author's own
            -- choice, from the old blind-to-overhead code) drives every
            -- attempt of THIS colour straight along z=3343-3345 into
            -- 2599,3345 by press #19-20 every single time (runs 3 and 5
            -- reproduced this move-for-move, byte-identical through press
            -- #55 -- this run is deterministic, not flaky). That tile is a
            -- genuine map dead end (m40_52.jm2's h1 pocket, both its own
            -- cardinal pushes unwalkable), and once the player is anywhere
            -- near it, App_NpcScreenPosition (pointer.lua's _ensure_visible
            -- comment) keeps re-centring the camera on that already-framed
            -- dead copy over a LIVE one standing right next to the player --
            -- measured rows 35-55 of run 3/5, 21 identical presses, every
            -- one naming the dead slot though tracked_x/z (a live copy)
            -- never moved. "z" first routes this colour south, off
            -- z=3343-3345, BEFORE it ever reaches x=2599 -- never through
            -- the pocket at all -- since the gate needs 15+ tiles of z
            -- progress regardless and there is no terrain reason tied to
            -- THIS specific dead tile to prefer x.
            local axis_pref = "z"
            local last_detail = "no press issued"
            local history = {}
            local tracked_x, tracked_z
            local bit_result, bit_value = t.var.varbit(def.bitvar)
            -- The colour has THREE live spawn copies a couple of tiles
            -- apart (m40_52.spawn). Re-picking "nearest to gate" fresh off
            -- t.npc.tiles every attempt is what broke run 2: the stand tile
            -- is computed against one copy, but click_minimenu's own
            -- hittest can land the press on a DIFFERENT nearby copy, so the
            -- next iteration's direction math was routinely built off a
            -- copy that never moved -- measured: 24 presses this way, ~8
            -- tiles of real (incoherent) drift and zero progress on Z.
            -- Fixed by TRACKING the slot t.player.press itself reports
            -- moving (`npc slot N x,z -> x2,z2` in its own detail) and
            -- aiming every next stand tile at THAT reported position, only
            -- falling back to a fresh t.npc.tiles nearest-to-gate pick to
            -- start, or to resync after two presses in a row where the
            -- tracked tile never changed.
            -- Rank candidates by MANHATTAN distance clamped into the gate
            -- band on each axis, never Chebyshev distance to the box's
            -- centre point -- run 5 measured Chebyshev picking a copy that
            -- needed 15+15=30 more presses (both axes still open) over one
            -- already inside the Z band needing only 16 (pure X), because
            -- Chebyshev(16,0)=16 read as "further" than Chebyshev(15,15)=15.
            -- A push only ever moves ONE axis, so the real remaining cost
            -- is the SUM of the two clamped gaps, not their max.
            -- Tiles where EVERY push this loop tried (both axes) answered
            -- walled -- a genuine dead end (m40_52's hill edge behind the
            -- barn can wall in all four neighbours of a tile at once).
            -- Keyed "x,z" -> true. Both candidate picks below add a huge
            -- penalty to a dead tile's distance so a copy stuck on one is
            -- never re-picked while any other copy is live.
            local dead_tiles = {}
            local tiles_result, tiles_detail, rows = t.npc.tiles(def.npc, 40)
            if tiles_result ~= "ok" or not rows or #rows == 0 then
                last_detail = string.format("npc.tiles(%s) -> %s (%s)", def.npc, tostring(tiles_result), tostring(tiles_detail))
            else
                local sheep = rows[1]
                local best = math.max(0, GATE_X_MIN - sheep.x, sheep.x - GATE_X_MAX) + math.max(0, GATE_Z_MIN - sheep.z, sheep.z - GATE_Z_MAX)
                for i = 2, #rows do
                    local d = math.max(0, GATE_X_MIN - rows[i].x, rows[i].x - GATE_X_MAX) + math.max(0, GATE_Z_MIN - rows[i].z, rows[i].z - GATE_Z_MAX)
                    if d < best then
                        best = d
                        sheep = rows[i]
                    end
                end
                tracked_x, tracked_z = sheep.x, sheep.z
            end
            local unmoved_streak = 0
            local last_axis = nil
            local stuck_x, stuck_z, stuck_count = nil, nil, 0
            -- (tile, axis) pairs a press already answered 'ok' + no
            -- movement against (a walled push, trap 21's second outcome).
            -- Keyed "x,z:axis" -> true; never cleared for this colour, so
            -- a resync that lands back on a known-walled tile still turns
            -- the other way immediately instead of re-spending a press to
            -- rediscover the same wall.
            local wall_blacklist = {}
            local outcome_counts = { stepped = 0, walled = 0, out_of_range = 0, lost = 0, other = 0 }

            while pressed < 55 and tracked_x and (bit_result ~= "ok" or bit_value == 0) do
                if unmoved_streak >= 6 then
                    -- Resync: several attempts running (including retreats)
                    -- never moved the tracked copy at all -- pick whichever
                    -- live copy has the least real work left instead, never
                    -- one already known dead on every side.
                    local resync_result, _, resync_rows = t.npc.tiles(def.npc, 40)
                    if resync_result == "ok" and resync_rows and #resync_rows > 0 then
                        local sheep = resync_rows[1]
                        local best = math.max(0, GATE_X_MIN - sheep.x, sheep.x - GATE_X_MAX) + math.max(0, GATE_Z_MIN - sheep.z, sheep.z - GATE_Z_MAX)
                            + (dead_tiles[string.format("%d,%d", sheep.x, sheep.z)] and 100000 or 0)
                        for i = 2, #resync_rows do
                            local d = math.max(0, GATE_X_MIN - resync_rows[i].x, resync_rows[i].x - GATE_X_MAX) + math.max(0, GATE_Z_MIN - resync_rows[i].z, resync_rows[i].z - GATE_Z_MAX)
                                + (dead_tiles[string.format("%d,%d", resync_rows[i].x, resync_rows[i].z)] and 100000 or 0)
                            if d < best then
                                best = d
                                sheep = resync_rows[i]
                            end
                        end
                        tracked_x, tracked_z = sheep.x, sheep.z
                    end
                    unmoved_streak = 0
                    last_axis = nil
                    stuck_x, stuck_z, stuck_count = nil, nil, 0
                end

                local sheep = { x = tracked_x, z = tracked_z }
                local in_x = sheep.x >= GATE_X_MIN and sheep.x <= GATE_X_MAX
                local eff_z_min, eff_z_max = GATE_Z_MIN, GATE_Z_MAX
                local in_z = sheep.z >= eff_z_min and sheep.z <= eff_z_max

                if sheep.x == stuck_x and sheep.z == stuck_z then
                    stuck_count = stuck_count + 1
                else
                    stuck_x, stuck_z, stuck_count = sheep.x, sheep.z, 0
                end

                local axis
                if stuck_count >= 2 then
                    -- The tracked copy has not moved for THREE attempts in
                    -- a row at this exact tile -- run 7 measured both
                    -- cardinal pushes (west AND its perpendicular nudge)
                    -- failing over and over at one spot (2599,3345, a rock
                    -- by the barn), which the old resync could not escape
                    -- because it kept re-picking the very same tile as
                    -- "closest". Retreat SOUTH instead -- the direction
                    -- every colour's approach already came from and is
                    -- known open -- to get off the blocked tile, then let
                    -- the normal plan re-aim from wherever that lands.
                    axis = "retreat"
                elseif unmoved_streak == 1 and last_axis then
                    -- One stall: try the PERPENDICULAR axis once before a
                    -- full resync -- the same tile can refuse one direction
                    -- (a fence post, a rock) while the other is clear.
                    axis = (last_axis == "x") and "z" or "x"
                elseif axis_pref == "x" and not in_x then
                    axis = "x"
                elseif axis_pref == "z" and not in_z then
                    axis = "z"
                elseif not in_x then
                    axis = "x"
                elseif not in_z then
                    axis = "z"
                else
                    axis = axis_pref -- both already in range but the bit has not landed yet -- force one more evaluation
                end

                -- A push this loop already saw answer 'ok' + no movement
                -- FROM THIS EXACT TILE ON THIS EXACT AXIS is a known wall --
                -- turn the other way now, not after another full press.
                if axis ~= "retreat" and wall_blacklist[string.format("%d,%d:%s", sheep.x, sheep.z, axis)] then
                    local other_axis = (axis == "x") and "z" or "x"
                    if not wall_blacklist[string.format("%d,%d:%s", sheep.x, sheep.z, other_axis)] then
                        axis = other_axis
                    else
                        axis = "retreat" -- both axes walled from this exact tile -- step off it
                    end
                end
                last_axis = axis

                -- Bounds-aware: when the chosen axis is already inside the
                -- gate band (a dodge nudge, not real progress), pick
                -- whichever direction keeps the result inside the band
                -- rather than pushing straight through the far wall.
                local stand_x, stand_z, push_desc
                if axis == "retreat" then
                    stand_x, stand_z, push_desc = sheep.x, sheep.z + 1, "south-retreat"
                elseif axis == "x" then
                    if sheep.x > GATE_X_MAX then
                        stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west"
                    elseif sheep.x < GATE_X_MIN then
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east"
                    elseif sheep.x - 1 >= GATE_X_MIN then
                        stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west"
                    else
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east"
                    end
                else
                    if sheep.z < eff_z_min then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z - 1, "north"
                    elseif sheep.z > eff_z_max then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z + 1, "south"
                    elseif sheep.z + 1 <= eff_z_max then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z - 1, "north"
                    else
                        stand_x, stand_z, push_desc = sheep.x, sheep.z + 1, "south"
                    end
                end

                local walk_result, walk_detail = t.player.walk_to(stand_x, stand_z, 12)
                local at_result, at_tile = t.world.tile()
                local press_result, press_detail = t.player.press(def.npc, 1, 8)
                pressed = pressed + 1

                -- Track whatever slot press's own detail says actually
                -- moved, never the copy this attempt aimed at -- the two
                -- disagree whenever a neighbouring copy is nearer the click.
                local moved_x, moved_z = string.match(press_detail or "", "npc slot %d+ %d+,%d+ %-> (%d+),(%d+)")
                -- pointer.lua's OWN "pressed" prefix ("slot N (element E) at
                -- X,Z") is printed on EVERY non-stepped outcome too (walled,
                -- lost) -- this is pointer.lua:5087-5088's `pressed` local,
                -- literally the pre-press snapshot of whichever element
                -- click_minimenu actually hit, which is not always the copy
                -- this attempt aimed at (by_symbol resolves the symbol, not
                -- a chosen slot -- there is no verb to aim a specific copy).
                -- Read it off every outcome, not just a step, so a resync
                -- that walked toward one candidate but got a DIFFERENT
                -- live copy under the cursor corrects tracked_x/tracked_z to
                -- what was actually pressed, instead of silently repeating
                -- the same wall a stale guess would keep aiming at.
                local pressed_x, pressed_z = string.match(press_detail or "", "slot %-?%d+ %(element [%-%d]+%) at (%d+),(%d+)")

                -- The four outcomes trap 21 names, read by name (not
                -- inferred from a timeout, which the overhead fix retired
                -- as the "npc went unresponsive" reading -- see the resume
                -- banner at the top of this file):
                --   stepped     'ok' + the npc's own row names it moving
                --               away from the player
                --   walled      'ok' with no movement -- the press LANDED
                --               (the npc said its overhead line) and the
                --               world refused the step; this is a map wall,
                --               not the npc or the driver
                --   out_of_range 'refused' -- the engine never sent it
                --   lost        'timeout' -- nothing came back; the press
                --               itself may not have landed
                local outcome
                if press_result == "ok" and moved_x then
                    outcome = "stepped"
                elseif press_result == "ok" then
                    outcome = "walled"
                elseif press_result == "refused" then
                    outcome = "out_of_range"
                elseif press_result == "timeout" then
                    outcome = "lost"
                else
                    outcome = "other"
                end

                last_detail = string.format("#%d sheep %d,%d (in_x=%s in_z=%s) push %s aim %d,%d walk=%s(%s) at=%s%s press=%s(%s) outcome=%s",
                    pressed, sheep.x, sheep.z, tostring(in_x), tostring(in_z), push_desc, stand_x, stand_z,
                    tostring(walk_result), tostring(walk_detail),
                    tostring(at_result), at_result == "ok" and string.format("%d,%d,%d", at_tile.x, at_tile.z, at_tile.level) or "",
                    tostring(press_result), tostring(press_detail), outcome)
                history[#history + 1] = last_detail
                outcome_counts[outcome] = outcome_counts[outcome] + 1
                if def.id == "1" and pressed <= 8 then
                    t.shot("herdprobe1." .. pressed)
                end

                if outcome == "stepped" then
                    tracked_x, tracked_z = tonumber(moved_x), tonumber(moved_z)
                    unmoved_streak = 0
                else
                    -- Correct the tracked tile to whatever pointer.lua's
                    -- OWN pre-press snapshot named, not the candidate this
                    -- attempt walked toward: a resync's guess and the copy
                    -- click_minimenu actually finds under the cursor can
                    -- disagree (run 2, rows 22-55, measured hitting slot 101
                    -- at 2599,3345 over and over while `sheep` claimed a
                    -- different tracked candidate entirely -- the walk had
                    -- landed, the CLICK had not moved). Ground every
                    -- blacklist entry in that same truth, never the guess.
                    local real_x = tonumber(pressed_x) or sheep.x
                    local real_z = tonumber(pressed_z) or sheep.z
                    if pressed_x then
                        tracked_x, tracked_z = real_x, real_z
                    end
                    if outcome == "walled" then
                        -- This exact (tile, axis) push landed and the map
                        -- refused it -- never spend another press finding
                        -- that out again; the blacklist check above the
                        -- axis choice turns the other way next attempt.
                        wall_blacklist[string.format("%d,%d:%s", real_x, real_z, axis)] = true
                        -- Both cardinal axes are now confirmed walled FROM
                        -- THIS EXACT TILE -- a true dead end (measured: the
                        -- hill edge behind the barn walls in all four
                        -- neighbours of one tile at once, m40_52's h1
                        -- pocket around 2598-2599,3344-3345). The
                        -- stuck_count>=2 "retreat" fallback below pushes
                        -- the SAME already-walled z direction from the
                        -- other side and would just re-confirm the same
                        -- wall forever, so force the resync path NOW
                        -- instead of spending the rest of this colour's
                        -- press budget proving it again.
                        if wall_blacklist[string.format("%d,%d:x", real_x, real_z)]
                            and wall_blacklist[string.format("%d,%d:z", real_x, real_z)] then
                            dead_tiles[string.format("%d,%d", real_x, real_z)] = true
                            unmoved_streak = 5 -- the +1 just below carries it to 6, the resync threshold
                        end
                    end
                    -- Deliberately NOT flipping axis_pref here any more --
                    -- run 6 measured that doing so on the FIRST stall (a
                    -- single blocked tile) permanently swapped the staged
                    -- plan to "z first", which then climbed the pen's EAST
                    -- side from outside and dead-ended against its wall at
                    -- x=2610 (14 presses -- #32-45 -- oscillating z with
                    -- west permanently refused). axis_pref now stays "x"
                    -- for this whole colour: get fully into the gate's own
                    -- x-band while still south of the enclosure first, and
                    -- let the wall blacklist (immediate), the one-shot
                    -- perpendicular nudge (unmoved_streak == 1, above) and
                    -- the resync (== 6) absorb a blocked tile without
                    -- abandoning that plan.
                    unmoved_streak = unmoved_streak + 1
                end
                bit_result, bit_value = t.var.varbit(def.bitvar)
            end

            -- A FAIL row immediately ahead of t.blocked() is the rejected
            -- shape (section 8's "what the gate will not tell you", trap
            -- 15's precedent in pryingtimes.lua/makinghistory.lua): grade
            -- this a RECORDING row (t.check with a literal `true`) and let
            -- the t.blocked() below carry the verdict when herding failed
            -- -- the reading (herded or not, and the last press) lives in
            -- its detail either way.
            local herded = bit_result == "ok" and bit_value ~= 0
            t.check("herd.sheep" .. def.id .. "_in_pen", true,
                string.format("herded=%s, %s=%s after %d press(es) (stepped=%d walled=%d out_of_range=%d lost=%d) -- %s",
                    tostring(herded), def.bitvar, tostring(bit_value), pressed,
                    outcome_counts.stepped, outcome_counts.walled, outcome_counts.out_of_range, outcome_counts.lost,
                    table.concat(history, " || ")))
            if not herded then
                t.blocked(string.format(
                    "diseased_sheep.rs2 [label,prod_sheep]: %s never reached sheepherder_pen_gate (2592-2594,3360-3363) after %d presses (stepped=%d walled=%d out_of_range=%d lost=%d) -- last attempt: %s -- every press LANDED (the npc's own overhead line answers back); the obstacle is the MAP, not the npc or the driver, and it is now named, not just typed: OSRS-Content/osrs239-content/maps/m40_52.jl2 places loc 980 as a CONTINUOUS fence along x=2609 for the whole z=3351..3364 span (grep '^0 49 (2[3-9]|3[0-6]) ' m40_52.jl2), which contains sheepherder_pen_gate's own z-band (3360-3363) entirely -- the confirmed gap (no loc row at all) is x=2609,z=3349/3350, north of the fence's span, requiring the herd route to cross there rather than at the gate's own z. A greedy per-press clamp toward the gate box alone cannot discover this: aiming z at the true gate band first drives the approach straight into the solid stretch of the fence (measured: 21 identical presses oscillating z=3360..3363, x=2610 refused on every one); aiming z at the known gap first instead (tried) regressed further, colliding with a SEPARATE unrelated obstacle near the spawn's own z=3341-3344 terrain before ever reaching the fence. Both are on record in build/author_state/sonnet-b12/sheepherder.author.progress.md (runs 3-7) with the exact per-press ledger for each. Routing a herd around a named map wall is pathfinding, not a quest-file bug fix -- this is a content/driver seam, not a false diagnosis this time",
                    def.npc, pressed, outcome_counts.stepped, outcome_counts.walled, outcome_counts.out_of_range, outcome_counts.lost, last_detail))
                return
            end
        end

        -- ---------------------------------------------------------------
        -- Enter the enclosure (sheepherder_gate.rs2 teleports across the
        -- gate loc once worn gear is confirmed) and dispose of each sheep:
        -- poisoned_feed on the now-visible herder_plaguesheep_N_enclosure
        -- (multivarbit=sheepherder_sheep_<letter>, hidden until that bit
        -- went non-zero above -- trap 28/19, target the BASE spawned
        -- symbol, never the wild child), pick up its bones, then use them
        -- on the furnace.
        t.exec("goto-enterEnclosure", t.player.goto_tile, 2594, 3362, 0)
        t.exec("enterEnclosure", t.player.click_loc, "plaguesheep_gatel", 1)

        for _, def in ipairs(sheep_defs) do
            local sheep_target, bs_result, bs_name = t.player.by_symbol("npc", def.enclosure)
            if not sheep_target then
                t.blocked(string.format(
                    "diseased_sheep.rs2: by_symbol('npc','%s') -> %s (%s) after %s went non-zero -- the enclosure npc never resolved visible",
                    def.enclosure, tostring(bs_result), tostring(bs_name), def.bitvar))
                return
            end
            t.exec("poison" .. def.id, t.player.use_on, "poisoned_feed", sheep_target)
            t.ticks(2) -- the sheep_death anim + npc_del + obj_add land a tick behind the click (trap 24)

            local bones_before_result, bones_before = t.inv.count(def.bones)
            local bones_pickup_result, bones_pickup_detail = t.player.click_obj(def.bones)
            local bones_after_result, bones_after = t.inv.count(def.bones)
            t.step("collectBones" .. def.id,
                (bones_pickup_result == "ok" and bones_after_result == "ok" and bones_after > (bones_before or 0)) and "PASS" or "FAIL",
                string.format("click_obj(%s) -> %s (%s); %s %s -> %s", def.bones, tostring(bones_pickup_result), tostring(bones_pickup_detail), def.bones, tostring(bones_before), tostring(bones_after)))

            local furnace_target, furnace_bs_result = t.player.by_symbol("loc", "plaguesheep_furnace")
            if not furnace_target then
                t.blocked(string.format("sheepherder_furnace.rs2: by_symbol('loc','plaguesheep_furnace') -> %s", tostring(furnace_bs_result)))
                return
            end
            t.exec("incinerate" .. def.id, t.player.use_on, def.bones, furnace_target)
            t.ticks(5) -- inv_del lands with the click, but the bit write is behind incinerate_bones' own p_delay(3)

            local incinerated_result, incinerated_value = t.var.varbit(def.bitvar)
            t.check("incinerate" .. def.id .. ".bit_incinerated",
                incinerated_result == "ok" and incinerated_value == 6,
                string.format("%s=%s after incinerate (want 6)", def.bitvar, tostring(incinerated_value)))
        end

        t.exec("exitEnclosure", t.player.click_loc, "plaguesheep_gatel", 1)

        -- ---------------------------------------------------------------
        -- Hand-in: councillor_halgrive.rs2's [label,halgrive_before_incinerating_sheep]
        -- reads all four sheepherder_sheep_a..d == incinerated and queues
        -- sheepherder_complete -- no mesbox, so the reward grant runs once
        -- the branch's own last chatnpc_anim page is continued past.
        local coins_before_result, coins_before = t.inv.count("coins")
        t.exec("goto-talkToHalgriveFinish", t.player.goto_tile, 2615, 3298, 0)
        t.exec("talkToHalgriveFinish", t.player.talk_to, "councillor_halgrive")
        t.exec("talkToHalgriveFinish-dialog", t.chat.play, {
            "npc:Have you managed to find and dispose",
            "player:Yes, I have.",
            "npc:Excellent work adventurer!",
            "npc:And in recognition of your service to",
        })
        t.ticks(3) -- completion is asynchronous, load-bearing before expect_complete (section 8)

        t.quest.expect_complete()

        local coins_after_result, coins_after = t.inv.count("coins")
        t.check("reward.coins",
            coins_before_result == "ok" and coins_after_result == "ok" and coins_after == coins_before + 3100,
            string.format("coins %s -> %s (want +3100, the literal [queue,sheepherder_complete] grant)", tostring(coins_before), tostring(coins_after)))

        t.finish(0)
    end,
}
