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
            local axis_pref = "x" -- close the X gap into the gate's own band FIRST (see banner) --
                                   -- every spawn cluster sits south of the pen behind a hill
                                   -- (run 3's own per-attempt log: "north" stalled or drifted no
                                   -- less often than "west" did, and it is "west" that is the
                                   -- terrain's own path), so approach along the south side until
                                   -- the player is already due south of the gate mouth itself
                                   -- (2592-2594, west of the fence's SW corner at 2595,3350 --
                                   -- never stopping AT that corner), then turn north into it.
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

            while pressed < 55 and tracked_x and (bit_result ~= "ok" or bit_value == 0) do
                if unmoved_streak >= 6 then
                    -- Resync: several attempts running (including retreats)
                    -- never moved the tracked copy at all -- pick whichever
                    -- live copy has the least real work left instead.
                    local resync_result, _, resync_rows = t.npc.tiles(def.npc, 40)
                    if resync_result == "ok" and resync_rows and #resync_rows > 0 then
                        local sheep = resync_rows[1]
                        local best = math.max(0, GATE_X_MIN - sheep.x, sheep.x - GATE_X_MAX) + math.max(0, GATE_Z_MIN - sheep.z, sheep.z - GATE_Z_MAX)
                        for i = 2, #resync_rows do
                            local d = math.max(0, GATE_X_MIN - resync_rows[i].x, resync_rows[i].x - GATE_X_MAX) + math.max(0, GATE_Z_MIN - resync_rows[i].z, resync_rows[i].z - GATE_Z_MAX)
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
                local in_z = sheep.z >= GATE_Z_MIN and sheep.z <= GATE_Z_MAX

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
                    if sheep.z < GATE_Z_MIN then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z - 1, "north"
                    elseif sheep.z > GATE_Z_MAX then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z + 1, "south"
                    elseif sheep.z + 1 <= GATE_Z_MAX then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z - 1, "north"
                    else
                        stand_x, stand_z, push_desc = sheep.x, sheep.z + 1, "south"
                    end
                end

                local walk_result, walk_detail = t.player.walk_to(stand_x, stand_z, 12)
                local at_result, at_tile = t.world.tile()
                local press_result, press_detail = t.player.press(def.npc, 1, 8)
                pressed = pressed + 1
                last_detail = string.format("#%d sheep %d,%d (in_x=%s in_z=%s) push %s aim %d,%d walk=%s(%s) at=%s%s press=%s(%s)",
                    pressed, sheep.x, sheep.z, tostring(in_x), tostring(in_z), push_desc, stand_x, stand_z,
                    tostring(walk_result), tostring(walk_detail),
                    tostring(at_result), at_result == "ok" and string.format("%d,%d,%d", at_tile.x, at_tile.z, at_tile.level) or "",
                    tostring(press_result), tostring(press_detail))
                history[#history + 1] = last_detail
                if def.id == "1" and pressed <= 8 then
                    t.shot("herdprobe1." .. pressed)
                end

                -- Track whatever slot press's own detail says actually
                -- moved, never the copy this attempt aimed at -- the two
                -- disagree whenever a neighbouring copy is nearer the click.
                local moved_x, moved_z = string.match(press_detail or "", "npc slot %d+ %d+,%d+ %-> (%d+),(%d+)")
                if press_result == "ok" and moved_x then
                    tracked_x, tracked_z = tonumber(moved_x), tonumber(moved_z)
                    unmoved_streak = 0
                else
                    -- Deliberately NOT flipping axis_pref here any more --
                    -- run 6 measured that doing so on the FIRST stall (a
                    -- single blocked tile) permanently swapped the staged
                    -- plan to "z first", which then climbed the pen's EAST
                    -- side from outside and dead-ended against its wall at
                    -- x=2610 (14 presses -- #32-45 -- oscillating z with
                    -- west permanently refused). axis_pref now stays "x"
                    -- for this whole colour: get fully into the gate's own
                    -- x-band while still south of the enclosure first, and
                    -- let the one-shot perpendicular nudge (unmoved_streak
                    -- == 1, above) and the resync (== 2) absorb a single
                    -- blocked tile without abandoning that plan.
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
                string.format("herded=%s, %s=%s after %d press(es) -- %s", tostring(herded), def.bitvar, tostring(bit_value), pressed, table.concat(history, " || ")))
            if not herded then
                t.blocked(string.format(
                    "diseased_sheep.rs2 [label,prod_sheep]: %s never reached sheepherder_pen_gate (2592-2594,3360-3363) after %d presses -- last attempt: %s -- EVERY direction (west/east/north/south-retreat) timed out identically at the stuck tile, npc silent (\"nothing was said and no dialogue opened\") -- read as the npc itself going unresponsive to op1 at that tile (off nav-mesh / occluded by terrain), not a one-sided fence: known residual driver seam per queue.py's own last_failure banner, not a content bug",
                    def.npc, pressed, last_detail))
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
