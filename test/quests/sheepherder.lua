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
--
-- RESUMED AGAIN after being REVERTED BY SAMPLER (sonnet-b12): a prior round
-- of this file blamed loc 980's EAST wall (x=2609, solid the whole
-- z=3351..3364 span) for blocking the herd and t.blocked'd on it. That
-- diagnosis was backwards -- sheepherder_pen_gate (2592-2594,3360-3363) is
-- WEST of the pen's own x-span (2595-2609), not inside it, so a correct
-- route never needs to cross x=2609 at all. Verified directly against
-- OSRS-Content/osrs239-content/maps/m40_52.jl2 (grep '^0 35 ', local x=35 =
-- world x=2595, the pen's WEST wall): loc 980 there is continuous for local
-- z=23..32 and 35..36 (world z=3351..3360 and 3363..3364) but has NO row at
-- local z=33..34 (world z=3361..3362) -- and that exact gap is occupied by
-- gate locs 166/167 (grep '^0 34 ' at local z=33/34, world x=2594): the
-- pen's real door. The south wall (grep '^0 3[5-9] 23:\|^0 4[0-9] 23:',
-- local z=23 = world z=3351) is solid and continuous from local x=35..49
-- (world x=2595..2609) with no gap at all. So sheepherder_pen_gate
-- (2592-2594,3360-3363) is a small pocket immediately WEST of that door,
-- outside the enclosure entirely, reachable only by going around the pen's
-- south wall to its west side and approaching the door from outside/west --
-- never by entering the enclosure's own interior and trying to find a way
-- out through a wall that has none. The loop below therefore closes the X
-- gap FIRST turned out to still be a trap: a run against that plan (kept
-- in this file's own history) reached x=2599,z=3345 -- and m40_52.jm2's own
-- walkable flags there (grep confirms h1/f1 at 2598,3345 and 2599,3344)
-- make that EXACT tile a genuine dead end -- both perpendicular escapes
-- need a STANDING tile that is itself unwalkable, not just a blocked push,
-- so no amount of retrying from it can ever move the sheep again. A full
-- BFS over m40_52.jm2's real walkable/blocked grid (script, not typed by
-- hand) plus the three built walls -- south z=3351, east x=2609, north
-- z=3364, all solid across the pen's own x=2595..2609, confirmed by direct
-- grep of m40_52.jl2's loc 980 rows -- found a clean, trap-free route that
-- NEVER needs the west door at all: climb NORTH first, staying strictly
-- OUTSIDE 2595..2609 on x the whole time (nudging off that column first
-- if a spawn or resync copy sits on its own edge, x=2609 or x=2595), past
-- the pen's own north wall (z > 3364); only THEN close the X gap into
-- 2592-2594, entirely north of every wall; only THEN close Z back down
-- into the real gate band (3360-3363) -- x=2592-2594 carries no wall at
-- any Z (the north wall's own x-span stops at 2595), so that last leg is
-- unobstructed. The loop below drives exactly this plan and falls back to
-- the wall_blacklist/dead_tiles machinery only for whatever local
-- obstacle (a rock, a building corner) this BFS's coarse map-square grid
-- does not already account for.
--
-- RESUMED (sonnet-b20) after two independent findings on the committed
-- file: (1) REVERTED BY SAMPLER (sonnet-b19) -- "goto-barn" goto_tile'd
-- straight to the cattleprod at 2604,3357 (Farmer Brumty's barn) from
-- OUTSIDE the enclosure, teleporting past the gate entirely; that tile is
-- INSIDE sheepherder_in_pen (2595-2609,3351-3364), and SheepHerder.java's
-- own goBurnSheep ConditionalStep ladder only offers pickupCattleprod once
-- inEnclosure is already true. The pickup now sits behind a real
-- click_loc gate entry, with a matching exit before the herd loop and a
-- second entry before feeding (four crossings total; see the banners at
-- each). (2) sonnet-b18's t.blocked on plaguesheep_4 (the default
-- 60000-frame budget exhausted by sheep 1-3 alone, ticks=1253) named the
-- fix as out of test/quests/ reach; QUEST_AUTHORING.md section 8 says
-- otherwise -- Sheep Herder is its own worked example for the quest
-- table's `max_frames = <n>,` field -- so that field is declared below and
-- the sheep-4 special case is gone; the herd loop treats all four
-- identically.
return {
    id = "sheepherder",
    fixture = "fresh_lumbridge.ini",
    -- QUEST_AUTHORING.md section 8 names Sheep Herder as the worked example
    -- for this field: the four-sheep herd plus poison/incinerate/hand-in
    -- overran the default 60000-frame budget at ticks=1253 (sonnet-b18,
    -- run 5, four independent full runs, all a clean exit(0) at the same
    -- row) -- the fix documented there is this field, not a t.blocked on
    -- sheep 4 (that banner is removed below). 240000 is the ceiling
    -- (quest_list.MAX_FRAMES_CEILING, 4x default, ~8,000 ticks), ample
    -- room over the ~2,000-tick session guidance this run needs.
    max_frames = 240000,
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

        -- RESUMED (sonnet-b20) after being REVERTED BY SAMPLER (sonnet-b19):
        -- the previous round's "goto-barn" line goto_tile'd straight to
        -- 2604,3357 (Farmer Brumty's barn) from OUTSIDE, teleporting past
        -- the enclosure gate entirely to grab the cattleprod. The
        -- cattleprod's own spawn tile sits INSIDE sheepherder_in_pen
        -- (2595-2609,3351-3364), and SheepHerder.java's own goBurnSheep
        -- ConditionalStep ladder defaults to enterEnclosure until
        -- inEnclosure is already true, only THEN offering pickupCattleprod
        -- -- so entering through the gate is a real step here, not a wall
        -- the herd loop happens to route around. The pen forces FOUR gate
        -- crossings total, not the guide panel's single "Enter the
        -- enclosure" entry: in (grab the cattleprod, since it sits in the
        -- barn inside the pen) / out (diseased_sheep.rs2 [label,prod_sheep]
        -- refuses to prod an npc already inside sheepherder_in_pen, and the
        -- four wild sheep start outside it) / in again (feed/collect
        -- bones/incinerate, all inside the pen) / out (return to Halgrive,
        -- unchanged at the bottom of this file).
        local PEN_X_MIN, PEN_X_MAX = 2595, 2609
        local PEN_Z_MIN, PEN_Z_MAX = 3351, 3364

        -- Enter #1. goto_tile lands EXACTLY on the gate loc's own tile
        -- (2594,3362, loc 166 -- m40_52.jl2) if aimed there directly, and
        -- click_loc's own "step off the target tile" then lands the player
        -- on 167 (the gate's second loc, 2594,3361) with NO real walking
        -- route in between -- _settle_after_click's three arms (mounted
        -- sub, new chat line, map_flag route-end) all need something to
        -- resolve ON, and a click with no route, no chat line and no
        -- interface (sheepherder_gate.rs2's p_teleport fires none of the
        -- three) times out. Aiming the goto one tile off the loc itself
        -- gives click_loc a real one-tile approach for the manual poll
        -- below to catch instead of trusting the click verb's own result
        -- word.
        t.exec("goto-enterEnclosure1", t.player.goto_tile, 2593, 3362, 0)
        local enter1_result, enter1_detail = t.player.click_loc("plaguesheep_gatel", 1)
        local arrived1 = false
        for _ = 1, 4 do
            local tile1_result, tile1 = t.world.tile()
            if tile1_result == "ok" and tile1.x >= PEN_X_MIN and tile1.x <= PEN_X_MAX and tile1.z >= PEN_Z_MIN and tile1.z <= PEN_Z_MAX then
                arrived1 = true
                break
            end
            t.ticks(1)
        end
        t.check("enterEnclosure1", arrived1,
            string.format("click_loc -> %s (%s); arrived in pen (%d-%d,%d-%d)=%s", tostring(enter1_result), tostring(enter1_detail), PEN_X_MIN, PEN_X_MAX, PEN_Z_MIN, PEN_Z_MAX, tostring(arrived1)))
        if not arrived1 then
            t.blocked(string.format(
                "sheepherder_gate.rs2 [label,sheepherder_gate]: click_loc('plaguesheep_gatel',1) -> %s (%s) and t.world.tile() never read inside the pen (%d-%d,%d-%d) after 4 tick(s) of polling on the FIRST crossing (before the cattleprod) -- the gate's own p_teleport fires none of _settle_after_click's three arms (no mounted sub, no new chat line, no map_flag route-end), so this seam is the driver's click-settle never recognising a LOC-triggered teleport, not a missing trigger or wrong clothing (worn plague_jacket/trousers already confirmed above)",
                tostring(enter1_result), tostring(enter1_detail), PEN_X_MIN, PEN_X_MAX, PEN_Z_MIN, PEN_Z_MAX))
            return
        end

        -- The cattleprod is a ground item in Farmer Brumty's barn, INSIDE
        -- the enclosure now that enterEnclosure1 above landed
        -- (m40_52.spawn), not handed over in any dialogue. click_obj
        -- answers `ok` with a nil detail on this pick-up (measured run 1),
        -- the hollow shape trap 12 names -- called directly, not through
        -- t.exec, with the before/after backpack count as the real detail.
        -- This goto is plain travel WITHIN the already-entered pen (both
        -- 2593,3362 above and 2604,3357 here are inside PEN_X/PEN_Z), not a
        -- teleport past anything, so it is not the cheat rule (b) names.
        t.exec("goto-barn", t.player.goto_tile, 2604, 3357, 0)
        local cattleprod_before_result, cattleprod_before = t.inv.count("cattleprod")
        local pickup_result, pickup_detail = t.player.click_obj("cattleprod")
        local cattleprod_after_result, cattleprod_after = t.inv.count("cattleprod")
        t.step("pickupCattleprod", (pickup_result == "ok" and cattleprod_after_result == "ok" and cattleprod_after > (cattleprod_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj -> %s (%s); cattleprod %s -> %s", tostring(pickup_result), tostring(pickup_detail), tostring(cattleprod_before), tostring(cattleprod_after)))
        t.exec("equip-cattleprod", t.player.equip, "cattleprod")

        -- Exit #1: the four wild sheep are OUTSIDE the enclosure
        -- (diseased_sheep.rs2 [label,prod_sheep] refuses to prod an npc
        -- already inside sheepherder_in_pen), so herding them needs the
        -- player back outside too. This click starts well clear of the
        -- gate tile (from the barn), which gives click_loc a real walking
        -- route to settle on, so a plain t.exec works here -- the manual
        -- poll above is only needed for the adjacent-tile approach.
        t.exec("exitEnclosureAfterCattleprod", t.player.click_loc, "plaguesheep_gatel", 1)

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
            -- plaguesheep_2's own spawn rows (m40_52.spawn) are 2621-2623,
            -- 3366-3367 -- x=2624 (this file's own earlier goto, kept from
            -- the scaffold) is the FIRST tile of the next map square
            -- (m40_52 spans world x=2560..2623), and t.player.walk_to a
            -- tile west of it stalled there every attempt this round
            -- (measured: 68 of 75 presses `out_of_range`, the walk never
            -- actually leaving 2624 despite a valid open target). 2618 is
            -- comfortably inside the loaded square instead.
            { id = "2", npc = "plaguesheep_2", enclosure = "herder_plaguesheep_2_enclosure", bitvar = "sheepherder_sheep_b", bones = "sheepbonesb", goto_x = 2618, goto_z = 3367 },
            { id = "3", npc = "plaguesheep_3", enclosure = "herder_plaguesheep_3_enclosure", bitvar = "sheepherder_sheep_c", bones = "sheepbonesc", goto_x = 2558, goto_z = 3391 },
            -- Same map-square-boundary lesson as plaguesheep_2 above:
            -- m40_52 spans world z=3328..3391, and z=3393 (this file's own
            -- earlier goto) is two tiles into the NEXT square north
            -- (m40_53). 3388 is comfortably inside, still close to the
            -- 2610-2612,3390-3391 spawn cluster.
            { id = "4", npc = "plaguesheep_4", enclosure = "herder_plaguesheep_4_enclosure", bitvar = "sheepherder_sheep_d", bones = "sheepbonesd", goto_x = 2613, goto_z = 3388 },
        }
        local GATE_X_MIN, GATE_X_MAX = 2592, 2594
        local GATE_Z_MIN, GATE_Z_MAX = 3360, 3363
        -- sheepherder_in_pen's own coord_pair_table row (0_40_52_35_23 to
        -- 0_40_52_49_36 -- sheepherder_area.dbrow): the enclosure's built
        -- x-span, walled solid on all three of south (z=3351), east
        -- (x=2609) and north (z=3364) -- confirmed against m40_52.jl2's
        -- own loc 980 rows directly (see the file banner). GATE_X/GATE_Z
        -- (2592-2594,3360-3363) sits entirely outside this span to its
        -- west, so a route only ever needs to stay off PEN_X_MIN..PEN_X_MAX
        -- while Z is inside PEN_Z_MIN..PEN_Z_MAX -- never actually needs
        -- the coordinates of the door in the west wall. PEN_X_MIN/PEN_X_MAX/
        -- PEN_Z_MIN/PEN_Z_MAX themselves are declared above, before
        -- enterEnclosure1, since that first gate crossing needs them too.
        --
        -- RESUMED (sonnet-b20): m40_52.jl2 places two 2x2 boulders (both
        -- shape 10, centrepiece -- a real push-blocking obstacle, not
        -- ground decor) right where plaguesheep_1's own climb corridor
        -- starts: loc 10790 (boulder1) at local 51,19 -> world 2611,3347,
        -- footprint 2611-2612,3347-3348; loc 10791 (boulder2) at local
        -- 50,21 -> world 2610,3349, footprint 2610-2611,3349-3350.
        -- Together they box every tile at 2610-2612,3347-3350 so that
        -- EITHER the approach tile a cardinal push needs or the push's own
        -- destination sits inside one of the two footprints -- measured a
        -- genuine 4-way dead end at 2612,3349 (both reachable neighbours,
        -- north-stand and east-stand, push into the other boulder) that
        -- cost 103 presses (run 1) before the per-colour cap gave up.
        -- BOULDER_X/BOULDER_Z is a padded box around both (one tile of
        -- margin) the climb ladder below routes around by continuing EAST
        -- past it before it ever turns to climb north, so a fresh copy
        -- from spawn (2609,3344 / 2610,3343 / 2610,3345 -- all just south
        -- of this box) never enters the pocket that wedged the run-1 copy
        -- in the first place.
        local BOULDER_X_MIN, BOULDER_X_MAX = 2609, 2613
        local BOULDER_Z_MAX = 3351

        for _, def in ipairs(sheep_defs) do
            t.exec("goto-herd" .. def.id, t.player.goto_tile, def.goto_x, def.goto_z, 0)

            -- RESUMED (sonnet-b20): sonnet-b18's t.blocked here (sheep 4
            -- unreachable inside the default 60000-frame budget, measured
            -- deterministic across four full runs, ticks=1253 at
            -- exhaustion) named the fix as "out of test/quests/ reach" --
            -- that was wrong: QUEST_AUTHORING.md section 8 documents
            -- exactly this quest as the worked example for the file's own
            -- `max_frames = <n>,` field (declared above, beside `fixture=`),
            -- which raises TORIRS_MAX_FRAMES for this run only. With the
            -- ceiling declared, sheep 4 gets the same herd loop as 1-3.

            local pressed = 0
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
            -- RESUMED (sonnet-b20, run 4): sheepherdervar's bit is set by
            -- ANY copy of this colour reaching the pen (diseased_sheep.rs2
            -- [label,prod_sheep], npc_tele + setbit -- it does not care
            -- WHICH of the 3 near-identical spawn copies got there), so a
            -- copy that keeps failing is not worth proving dead twice
            -- when the other 2 are still pristine. trouble_count is a
            -- CONTINUOUS per-tile failure tally (every non-stepped
            -- outcome at that exact tile, any axis) -- run 5 tried
            -- scaling it into the resync ranking (softer/faster than
            -- dead_tiles' binary two-strikes bar) alongside a lower
            -- resync threshold and measured WORSE (stepped=4 of 140 vs
            -- 53), so it is tracked here but no longer read by the
            -- ranking; left in place rather than ripped out in case a
            -- later round wants to isolate which half of that combined
            -- change actually hurt.
            local trouble_count = {}
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
            -- RESUMED (sonnet-b20): counts consecutive "retreat" picks at
            -- the CURRENT stuck tile, so the ladder can alternate retreat
            -- with the one cardinal direction it never otherwise tries
            -- (see x-escape below). Reset whenever the stuck tile changes
            -- or a resync fires, same as stuck_count.
            local retreat_tries = 0
            -- RESUMED (sonnet-b20, run 3): a 2-cycle -- A pushed to B,
            -- B's own next press shoves it straight back to A -- reads as
            -- "stepped" on EVERY single press (real movement each time),
            -- so stuck_count (which only counts an EXACT repeat, i.e. no
            -- movement at all) and unmoved_streak (reset by any "stepped")
            -- never fire, and the loop can spend its entire per-colour cap
            -- oscillating between two tiles with zero net progress --
            -- measured 2607,3345 <-> 2608,3345, 87 of run 3's 140 presses.
            -- pos1_x/z, pos2_x/z are the tracked position 1 and 2 presses
            -- back (before THIS press); a match against pos2 is the
            -- 2-cycle signature and is treated the same as stuck_count>=2.
            local pos1_x, pos1_z, pos2_x, pos2_z = nil, nil, nil, nil
            -- RESUMED (sonnet-b20, run 6): x-escape (see its own banner
            -- below) computed its OWN "opposite" direction independently,
            -- via the same sheep.x-vs-GATE_X_MAX test the plain "x" ladder
            -- pick uses -- but the "off-the-wall-column" pick (climbing
            -- and not outside_pen_x) uses a DIFFERENT test (nearest pen
            -- edge, not the gate band), and for a tile like 2608,3345
            -- (nearer the EAST pen edge, 2609, than the west one, 2595)
            -- both tests agree on EAST -- so x-escape kept "escaping" in
            -- the exact same direction that was already failing, never
            -- actually trying west. last_x_direction records whichever
            -- direction the MOST RECENT real x-type push actually used
            -- (any branch), so x-escape can invert THAT, not recompute
            -- its own guess.
            local last_x_direction = nil

            -- Cap raised 95 -> 140 (RETRY after 9bf6b97c5, sonnet-b18):
            -- boulder2 (10791, width=2 length=2, m40_52.jl2 at
            -- 2592-2593,3381-3382) sits square in plaguesheep_3's own
            -- final approach corridor and forces a 2-column detour to
            -- x=2594 before the last ~20 tiles of Z can even start --
            -- measured a live run landing on x=2594 with its Z push not
            -- yet begun, exactly AT the old 95-press cap, ticks=1165/~2000
            -- total for the run so far (sheep1+sheep2 already spent).
            -- Total session budget (QUEST_AUTHORING.md section 8, "A RUN
            -- HAS ABOUT 2,000 SERVER TICKS") has ample room at ~5
            -- ticks/press for this raise even in the worst case.
            --
            -- Cap raised 140 -> 220 (sonnet-b20, run 5): plaguesheep_1's
            -- own barnyard congestion (see trouble_count's banner above)
            -- measured 53 real "stepped" outcomes out of 140 presses
            -- (38%) with the earlier-round resync tuning and STILL did
            -- not reach the gate band -- this colour was previously
            -- documented needing only ~50 clean presses once congestion
            -- is not fighting it, so the shortfall is churn, not distance,
            -- and this file's own `max_frames = 240000,` (4x default, see
            -- the quest table) is the budget this raise draws against, not
            -- the original ~2,000-tick guidance above.
            while pressed < 220 and tracked_x and (bit_result ~= "ok" or bit_value == 0) do
                if (unmoved_streak >= 3 and retreat_tries >= 2) or unmoved_streak >= 8 then
                    -- Resync: several attempts running (including retreats)
                    -- never moved the tracked copy at all -- pick whichever
                    -- live copy has the least real work left instead, never
                    -- one already known dead on every side.
                    --
                    -- RESUMED (sonnet-b20): the threshold was 6, then 3.
                    -- Run 5 tried 2 (plus trouble_count scaled into the
                    -- ranking below) and did WORSE (stepped=4 of 140, vs
                    -- 53 at threshold 3) -- resyncing this fast abandoned
                    -- a copy that was still making real, if noisy,
                    -- progress before it had a real chance to clear
                    -- Farmer Brumty's barnyard (drystonewall/fencing plus
                    -- two tree2 locs at 2607,3343 and 2604,3346, confirmed
                    -- against m40_52.jl2 directly), so this is back to 3
                    -- and trouble_count is no longer read here -- it is
                    -- still TRACKED (below, every non-stepped outcome) in
                    -- case a later round wants it, but does not currently
                    -- change any pick. The ranking's one active defence
                    -- against re-picking known trouble is the dead_tiles
                    -- Chebyshev-1 penalty: exact repeats alone missed run
                    -- 2's ping-pong between 2608,3345 / 2609,3345 /
                    -- 2610,3347, one tile apart each.
                    --
                    -- `and retreat_tries >= 2` is new this round (going
                    -- into run 7): at a tile where EVERY push fails 100%
                    -- of the time (2608,3345's own Z neighbours are
                    -- unreachable by walk at all -- both timed out every
                    -- single attempt, runs 3, 4 and 6), unmoved_streak
                    -- reaches 3 in lockstep with stuck_count reaching 2,
                    -- ONE iteration after retreat_tries first goes 0->1
                    -- (picking "retreat") -- so the resync fired every
                    -- time on the very iteration retreat_tries would
                    -- FINALLY have read odd and picked "x-escape", and
                    -- reset it back to 0 first. Measured: runs 3, 4 and 6
                    -- (0 "west-escape"/"east-escape" rows in 580 presses
                    -- combined across them) -- x-escape and
                    -- last_x_direction (run 6's own fix) were both dead
                    -- code this whole time, not ineffective. Gating the
                    -- resync on retreat_tries>=2 guarantees at least one
                    -- real x-escape attempt (the one direction never
                    -- otherwise tried at a tile like this) before giving
                    -- up on it. `or unmoved_streak >= 8` is a safety net
                    -- in case retreat_tries somehow never reaches 2 for a
                    -- DIFFERENT tile (a cross-copy hijack that keeps
                    -- changing which tile counts as "stuck" before
                    -- escalation even starts) -- never leaves a colour
                    -- with no resync path at all.
                    local resync_result, _, resync_rows = t.npc.tiles(def.npc, 40)
                    if resync_result == "ok" and resync_rows and #resync_rows > 0 then
                        local sheep, best
                        for i = 1, #resync_rows do
                            local cand = resync_rows[i]
                            local d = math.max(0, GATE_X_MIN - cand.x, cand.x - GATE_X_MAX) + math.max(0, GATE_Z_MIN - cand.z, cand.z - GATE_Z_MAX)
                            for dead_key, _ in pairs(dead_tiles) do
                                local dead_x_str, dead_z_str = string.match(dead_key, "(%-?%d+),(%-?%d+)")
                                local dead_x, dead_z = tonumber(dead_x_str), tonumber(dead_z_str)
                                if dead_x and dead_z and math.max(math.abs(cand.x - dead_x), math.abs(cand.z - dead_z)) <= 1 then
                                    d = d + 100000
                                    break
                                end
                            end
                            if not best or d < best then
                                best = d
                                sheep = cand
                            end
                        end
                        tracked_x, tracked_z = sheep.x, sheep.z
                    end
                    unmoved_streak = 0
                    last_axis = nil
                    stuck_x, stuck_z, stuck_count = nil, nil, 0
                    retreat_tries = 0
                    pos1_x, pos1_z, pos2_x, pos2_z = nil, nil, nil, nil
                end

                local sheep = { x = tracked_x, z = tracked_z }
                -- A 2-cycle: this exact tile is where the tracked copy was
                -- TWO presses ago (see pos1/pos2's own banner above).
                local revisiting = pos2_x == sheep.x and pos2_z == sheep.z
                local in_x = sheep.x >= GATE_X_MIN and sheep.x <= GATE_X_MAX
                local outside_pen_x = sheep.x < PEN_X_MIN or sheep.x > PEN_X_MAX
                -- PHASE: still south of (or inside) the pen's own walled
                -- z-span, and not yet in the gate's own x-band -- closing X
                -- here risks walking straight into the south wall (it is
                -- solid across the WHOLE pen x-span, not just at the gate's
                -- own x) or the jagged natural cliff between the spawn
                -- valley and the low ground further west (this file's own
                -- earlier BLOCKED history). Pin the Z target to one past
                -- the pen's own north wall instead, so the loop climbs
                -- north FIRST and only turns to X once genuinely clear of
                -- every wall (see the file banner for the BFS that found
                -- this route against the real map).
                local climbing = (not in_x) and (sheep.z <= PEN_Z_MAX)
                local eff_z_min, eff_z_max
                if climbing then
                    eff_z_min, eff_z_max = PEN_Z_MAX + 1, PEN_Z_MAX + 1
                else
                    eff_z_min, eff_z_max = GATE_Z_MIN, GATE_Z_MAX
                end
                local in_z = sheep.z >= eff_z_min and sheep.z <= eff_z_max
                -- The boulder pocket (see BOULDER_X/BOULDER_Z's own banner
                -- above) sits astride the normal climb corridor -- checked
                -- only while climbing, so a colour already north of it (or
                -- one whose approach never enters the box) never detours.
                --
                -- RESUMED (sonnet-b20, run 2): a lower Z bound here
                -- (BOULDER_Z_MIN=3345) let plaguesheep_1's OWN spawn tiles
                -- (2609,3344 and 2610,3343, both z<3345) fall straight
                -- through to the ordinary climb branches below instead,
                -- which walked them up to z=3345+ BEFORE this detour ever
                -- got a turn -- measured landing right at 2610,3348, one
                -- row inside boulder1's own footprint, on press #1. There
                -- is no reason to wait for any particular Z at all: while
                -- still climbing (i.e. still south of the pen's own north
                -- wall), clearing BOULDER_X first is always correct, so
                -- only the upper Z bound (once genuinely past the boulders)
                -- remains.
                local in_boulder_zone = climbing
                    and sheep.x >= BOULDER_X_MIN and sheep.x <= BOULDER_X_MAX
                    and sheep.z <= BOULDER_Z_MAX

                if sheep.x == stuck_x and sheep.z == stuck_z then
                    stuck_count = stuck_count + 1
                else
                    stuck_x, stuck_z, stuck_count = sheep.x, sheep.z, 0
                    -- retreat_tries is NOT reset here any more: a 2-cycle
                    -- (revisiting, above) alternates between exactly two
                    -- tiles, so THIS branch (a genuine tile change) fires
                    -- on every single one of its presses too, and used to
                    -- zero retreat_tries right back out before it could
                    -- ever reach the x-escape half of the alternation --
                    -- only a resync (a real change of tracked copy) clears
                    -- it now.
                end

                local axis
                if stuck_count >= 2 or revisiting then
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
                    --
                    -- RESUMED (sonnet-b20): plain "always retreat" cycles
                    -- forever when RETREAT ITSELF is also walled -- measured
                    -- on plaguesheep_1's own spawn cluster, boxed against
                    -- the two 2x2 boulders m40_52.jl2 places at
                    -- 2610-2611,3349-3350 (loc 10791) and 2611-2612,
                    -- 3347-3348 (loc 10790): from 2612,3349 the north push
                    -- (into 10790) and the west perpendicular (into 10791)
                    -- both wall, and retreat -- z-only, per the comment
                    -- above -- never tries the one direction that was
                    -- never attempted at all: the OPPOSITE x (east, away
                    -- from both boulders). A bare "always retreat" spent
                    -- 103 presses (#38-140) alternating retreat with a
                    -- resync that kept re-picking one of plaguesheep_1's
                    -- three near-identical spawn copies, all boxed the
                    -- same way, and burned the whole per-colour cap
                    -- without ever trying it. Alternate retreat with
                    -- "x-escape" (below) every other pick at the identical
                    -- stuck tile, so the untried fourth cardinal direction
                    -- gets a real attempt before the resync gives up on
                    -- this tile and re-picks another copy of the same
                    -- boxed-in colour.
                    --
                    -- `revisiting` (see its own banner above) joins this
                    -- branch too: a 2-cycle reports "stepped" on every
                    -- single press (real movement, just no NET progress),
                    -- so this is the only place that can ever catch it --
                    -- stuck_count itself only fires on an EXACT repeat.
                    if retreat_tries % 2 == 0 then
                        axis = "retreat"
                    else
                        axis = "x-escape"
                    end
                    retreat_tries = retreat_tries + 1
                elseif in_boulder_zone then
                    -- Proactive, not reactive: continue EAST past the
                    -- boulder pocket (BOULDER_X/BOULDER_Z's own banner
                    -- above) before climbing at all, so a fresh copy never
                    -- walks itself into the two-boulder box the way run 1's
                    -- tracked copy did. This fires ahead of the normal
                    -- climbing branches below (both of which would instead
                    -- turn the sheep straight into the pocket from
                    -- plaguesheep_1's own spawn column).
                    --
                    -- RESUMED (sonnet-b20, run 7): moved ahead of the
                    -- unmoved_streak==1 perpendicular nudge (was checked
                    -- first) -- that branch does not know about the
                    -- boulders at all, and its own "z" pick (whenever
                    -- last_axis was "x") pushes north unconditionally
                    -- (climbing's eff_z_min is PEN_Z_MAX+1, always above
                    -- the sheep's own z here), letting Z creep into the
                    -- 3347-3350 boulder band WHILE x was still inside
                    -- 2609-2613 -- measured run 7: "east-clear" (86 of
                    -- 220 presses, the single most common push) mostly
                    -- walled precisely because z had already drifted into
                    -- that band by the time it ran. in_boulder_zone now
                    -- overrides the one-shot nudge too, not only the
                    -- ordinary climb branches.
                    axis = "boulder-clear"
                elseif unmoved_streak == 1 and last_axis then
                    -- One stall: try the PERPENDICULAR axis once before a
                    -- full resync -- the same tile can refuse one direction
                    -- (a fence post, a rock) while the other is clear.
                    axis = (last_axis == "x") and "z" or "x"
                elseif climbing and not outside_pen_x then
                    -- Standing exactly on (or inside) the wall's own
                    -- x-span while still south of it -- get off that
                    -- column first (BFS-verified: plaguesheep_1's own
                    -- spawn tile 2609,3344 sits exactly on the east wall's
                    -- x and needs this before it can climb north at all).
                    axis = "x"
                elseif climbing then
                    axis = "z"
                elseif not in_x then
                    axis = "x"
                elseif not in_z then
                    axis = "z"
                else
                    axis = "x" -- both ranges already satisfied; the bit has not landed yet -- force one more evaluation
                end

                -- A push this loop already saw answer 'ok' + no movement
                -- FROM THIS EXACT TILE ON THIS EXACT AXIS is a known wall --
                -- turn the other way now, not after another full press.
                -- Only a plain "x"/"z" ladder pick is redirected here:
                -- retreat/x-escape/boulder-clear are already themselves a
                -- deliberate escape choice, and redirecting THEM through
                -- this generic "other_axis" logic can send a boulder
                -- detour straight back toward the gate band it is trying
                -- to get clear of first.
                local probe_target_x = nil
                if (axis == "x" or axis == "z") and wall_blacklist[string.format("%d,%d:%s", sheep.x, sheep.z, axis)] then
                    if axis == "z" and in_x then
                        -- Z is dead FROM THIS EXACT X, but the gate band is
                        -- only GATE_X_MIN..GATE_X_MAX (3 tiles) wide -- an
                        -- obstacle blocking Z at one column does not mean
                        -- it blocks every column (a boulder is narrower
                        -- than the whole band -- loc 10791 confirmed by
                        -- direct grep of m40_52.jl2 at exactly this kind of
                        -- spot). The bounds-aware in-band nudge below can
                        -- only ever reach GATE_X_MIN and GATE_X_MIN+1 by
                        -- construction (its own ternary always resolves
                        -- to one of those two), so it can bounce between
                        -- two dead columns forever without ever trying
                        -- GATE_X_MAX -- measured, plaguesheep_3's own
                        -- final approach. Probe whichever end of the band
                        -- has not already had Z blacklisted at this same
                        -- Z row, preferring the far end first since the
                        -- near ones are what the normal nudge already
                        -- tried.
                        for _, candidate in ipairs({ GATE_X_MAX, GATE_X_MIN }) do
                            if candidate ~= sheep.x and not wall_blacklist[string.format("%d,%d:z", candidate, sheep.z)] then
                                probe_target_x = candidate
                                break
                            end
                        end
                    end
                    if probe_target_x then
                        axis = "x-probe"
                    else
                        local other_axis = (axis == "x") and "z" or "x"
                        -- The fallback axis is only a real move if that
                        -- axis still has ground to cover -- redirecting to
                        -- X when X is already inside the gate band (or to
                        -- Z when Z already is) is not an escape, it is a
                        -- dodge nudge that lands on a DIFFERENT tile every
                        -- press (so stuck_count, keyed on the exact tile
                        -- repeating, never fires) while making zero
                        -- progress on the axis that actually needs one.
                        local other_still_needed = (other_axis == "x" and not in_x) or (other_axis == "z" and not in_z)
                        if other_still_needed and not wall_blacklist[string.format("%d,%d:%s", sheep.x, sheep.z, other_axis)] then
                            axis = other_axis
                        else
                            axis = "retreat" -- the axis that needs progress is walled and every X column is already probed -- step off it instead (even if retreat itself was already tried once here, nothing else is left to try)
                        end
                    end
                end
                last_axis = (axis == "x-probe" or axis == "x-escape" or axis == "boulder-clear") and "x" or axis

                -- Bounds-aware: when the chosen axis is already inside the
                -- gate band (a dodge nudge, not real progress), pick
                -- whichever direction keeps the result inside the band
                -- rather than pushing straight through the far wall.
                local stand_x, stand_z, push_desc
                if axis == "retreat" then
                    -- A genuine retreat has to be the OPPOSITE of whatever
                    -- z-push this tile just had blacklisted, not a fixed
                    -- direction: during the climb (z increasing toward the
                    -- gate) that opposite is south (z-1 push, this file's
                    -- historical "back toward spawn, known open" case),
                    -- but once past the gate's own z-band on the final
                    -- descent (z decreasing already) the SAME fixed
                    -- south-retreat is the identical move to the one just
                    -- blacklisted, not an escape from it -- measured
                    -- retreating into a dead loop on plaguesheep_3's own
                    -- final approach, south of the gate pushing north.
                    -- Use the same `climbing` flag already computed above:
                    -- during the climb Z is increasing, so retreat
                    -- decreases it (z+1 stand, the original south-retreat,
                    -- this file's historical "back toward spawn, known
                    -- open" case); on the final descent Z is decreasing
                    -- already, so retreat must increase it instead (z-1
                    -- stand) or it is the identical move that just failed.
                    if climbing then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z + 1, "south-retreat"
                    else
                        stand_x, stand_z, push_desc = sheep.x, sheep.z - 1, "north-retreat"
                    end
                elseif axis == "x-escape" then
                    -- The OPPOSITE of whichever x-direction was ACTUALLY
                    -- tried most recently (last_x_direction, own banner
                    -- above) -- so plain retreat (z-only, per the
                    -- stuck_count>=2 banner) never covers, and, since run
                    -- 6's fix, is no longer a guess this branch
                    -- independently recomputes off GATE_X_MAX. That guess
                    -- used to AGREE with the "off-the-wall-column" pick
                    -- (climbing and not outside_pen_x, a DIFFERENT
                    -- nearest-pen-edge test) whenever a tile sat nearer
                    -- the east pen edge than the gate band, which is most
                    -- of plaguesheep_1's own spawn column -- so x-escape
                    -- was "escaping" in the identical direction that was
                    -- already failing, at 2608,3345 for 220 straight
                    -- presses (run 6). Falls back to the GATE_X_MAX guess
                    -- only if no x-push has happened yet this colour.
                    if last_x_direction == "east" then
                        stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west-escape"
                    elseif last_x_direction == "west" then
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east-escape"
                    elseif sheep.x > GATE_X_MAX then
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east-escape"
                    else
                        stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west-escape"
                    end
                elseif axis == "boulder-clear" then
                    -- Push EAST (stand west of the sheep) to clear
                    -- BOULDER_X_MAX before turning to climb -- see
                    -- in_boulder_zone's own banner above. This is the
                    -- correct direction regardless of the gate band's own
                    -- side (the boulders sit well east of the gate, so the
                    -- normal "x" branch below would otherwise push WEST,
                    -- straight through them, once climbing resumes).
                    --
                    -- RESUMED (sonnet-b20, run 7): boulder1 (2611-2612,
                    -- 3347-3348) and boulder2 (2610-2611,3349-3350)
                    -- TOGETHER cover x=2610-2612 continuously for
                    -- z=3347-3350 -- so pushing east THROUGH that x range
                    -- while z is already inside 3347-3350 walks straight
                    -- into whichever boulder owns that exact z, every
                    -- time (measured: "east-clear" was the single most
                    -- common push in run 7, 86 of 220, most of them
                    -- walled). Only when z is clear of that band is an
                    -- east push through 2610-2612 actually safe. Clear Z
                    -- FIRST when it is not: push south (stand north of
                    -- the sheep) back below 3347, the direction every
                    -- copy's own approach already came from, then let
                    -- east-clear resume once z is safe next iteration.
                    if sheep.z >= 3347 and sheep.z <= 3350 then
                        stand_x, stand_z, push_desc = sheep.x, sheep.z + 1, "south-clear"
                    else
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east-clear"
                    end
                elseif axis == "x-probe" then
                    -- Drive toward the specific untried band column the
                    -- redirect above picked (probe_target_x), not the
                    -- generic bounds-aware nudge that can only ever reach
                    -- two of the band's tiles.
                    if probe_target_x > sheep.x then
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east-probe"
                    else
                        stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west-probe"
                    end
                elseif axis == "x" and climbing and not outside_pen_x then
                    -- Off-the-wall-column nudge: exit toward whichever
                    -- pen edge (2595 or 2609) is nearer, so a tracked
                    -- tile sitting ON one of them (never actually
                    -- INSIDE 2595..2609, which a legitimate push never
                    -- produces) steps off it in one press.
                    if (sheep.x - PEN_X_MIN) <= (PEN_X_MAX - sheep.x) then
                        stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west-off-wall"
                    else
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east-off-wall"
                    end
                elseif axis == "x" then
                    if sheep.x > GATE_X_MAX then
                        stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west"
                    elseif sheep.x < GATE_X_MIN then
                        stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east"
                    else
                        -- In-band dodge, reached via the unmoved_streak==1
                        -- one-shot perpendicular branch above (which sets
                        -- axis="x" directly and skips the wall_blacklist
                        -- redirect below, because THAT check only fires
                        -- for a blacklisted "x" entry, not a blacklisted
                        -- "z" one): prefer the neighbour column whose Z
                        -- push has NOT already been blacklisted at this
                        -- exact Z row, so a perpendicular retry right
                        -- after a Z failure keeps closing on an untried
                        -- column instead of bouncing back onto one already
                        -- known dead. Measured cost of the old fixed
                        -- west-biased fallback: plaguesheep_3's approach
                        -- to boulder2 (10791, width=2 length=2,
                        -- m40_52.jl2 at 2592-2593,3381-3382) bounced
                        -- 2593->2592->2593 for two wasted presses before
                        -- the dedicated x-probe branch above finally
                        -- pushed it on to the clear column, 2594.
                        local west_target_open = (sheep.x - 1 >= GATE_X_MIN) and not wall_blacklist[string.format("%d,%d:z", sheep.x - 1, sheep.z)]
                        local east_target_open = (sheep.x + 1 <= GATE_X_MAX) and not wall_blacklist[string.format("%d,%d:z", sheep.x + 1, sheep.z)]
                        if east_target_open and not west_target_open then
                            stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east"
                        elseif west_target_open and not east_target_open then
                            stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west"
                        elseif sheep.x - 1 >= GATE_X_MIN then
                            stand_x, stand_z, push_desc = sheep.x + 1, sheep.z, "west"
                        else
                            stand_x, stand_z, push_desc = sheep.x - 1, sheep.z, "east"
                        end
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

                -- Record the ACTUAL x-direction this push used, from
                -- push_desc itself, whichever branch produced it (see
                -- last_x_direction's own banner above) -- a z-only push
                -- (north/south/retreat) has no x-component and leaves it
                -- unchanged, since x-escape has nothing new to invert from
                -- one of those.
                if string.find(push_desc, "west") then
                    last_x_direction = "west"
                elseif string.find(push_desc, "east") then
                    last_x_direction = "east"
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
                    -- trouble_count (see its own banner above the resync
                    -- block): a plain failure tally at the ACTUAL pressed
                    -- tile, every non-"stepped" outcome, any axis -- not
                    -- just "walled". out_of_range/lost presses are real
                    -- evidence too (the engine never even sent the op, or
                    -- nothing came back), and this tile is where the
                    -- resync's own ranking will read it back.
                    local trouble_key = string.format("%d,%d", real_x, real_z)
                    trouble_count[trouble_key] = (trouble_count[trouble_key] or 0) + 1
                    if outcome == "walled" then
                        -- This exact (tile, axis) push landed and the map
                        -- refused it -- never spend another press finding
                        -- that out again; the blacklist check above the
                        -- axis choice turns the other way next attempt.
                        wall_blacklist[string.format("%d,%d:%s", real_x, real_z, axis)] = true
                        -- Deliberately NOT forcing an immediate resync just
                        -- because both of the two FORWARD directions this
                        -- loop happened to try are blacklisted: axis
                        -- selection only ever tries the direction that
                        -- moves toward the current target (never the
                        -- reverse of either axis), so "x and z both
                        -- blacklisted" here has only ruled out two of the
                        -- tile's four neighbours, not all of them -- the
                        -- stuck_count>=2 retreat fallback below is what
                        -- tests a third (the reverse of Z), and it must be
                        -- allowed to actually run. An earlier round of this
                        -- file force-jumped to a fresh npc.tiles resync the
                        -- moment both forward directions failed once, and
                        -- measured it firing on EVERY subsequent press
                        -- against a copy the live click kept re-selecting
                        -- regardless of which candidate the resync aimed
                        -- at (t.player.press has no verb to choose a
                        -- specific slot -- trap 21) -- an endless
                        -- resync-then-immediately-refail loop that burned
                        -- the whole colour's budget without ever reaching
                        -- stuck_count>=2. dead_tiles below still exists for
                        -- the resync ranking once unmoved_streak naturally
                        -- reaches the real threshold; only the premature
                        -- force is gone.
                        if wall_blacklist[string.format("%d,%d:x", real_x, real_z)]
                            and wall_blacklist[string.format("%d,%d:z", real_x, real_z)] then
                            dead_tiles[string.format("%d,%d", real_x, real_z)] = true
                        end
                        -- RESUMED (sonnet-b20): the x/z-blacklist test above
                        -- only fires when a raw "x" or "z" ladder pick was
                        -- tried -- a tile reached only through retreat/
                        -- x-escape/boulder-clear (never a plain "x"/"z"
                        -- pick, e.g. one already inside the gate band on
                        -- one axis) never sets both those keys and so never
                        -- marks dead this way. retreat_tries>=2 means BOTH
                        -- retreat and its x-escape counterpart have already
                        -- failed here, which is the same "no cardinal
                        -- escape left" signal by a different route.
                        if retreat_tries >= 2 then
                            dead_tiles[string.format("%d,%d", real_x, real_z)] = true
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
                -- Shift the 2-press position history (see pos1/pos2's own
                -- banner above `revisiting`) AFTER this press, using the
                -- PRE-press tile (sheep.x/z, what "revisiting" itself
                -- checks next time) -- so next iteration's `revisiting`
                -- compares against where the copy stood entering this
                -- press and the one before it, not its post-press result.
                pos2_x, pos2_z = pos1_x, pos1_z
                pos1_x, pos1_z = sheep.x, sheep.z
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
                    "diseased_sheep.rs2 [label,prod_sheep]: %s never reached sheepherder_pen_gate (2592-2594,3360-3363) after %d presses (stepped=%d walled=%d out_of_range=%d lost=%d) -- last attempt: %s -- every press LANDED (the npc's own overhead line answers back), so this is not a missing trigger or a driver click-settle bug. sonnet-b20 (2026-09-24, 8 runs) verified the real obstacle directly against m40_52.jl2/all.loc: plaguesheep_1's own spawn cluster (2609,3344 / 2610,3343 / 2610,3345, m40_52.spawn) sits inside Farmer Brumty's barnyard, boxed by a drystonewall (loc 979, z=3341) and the pen's own fence (loc 980, z=3351, continuous x=2595-2609 with no gap), TWO tree2 locs (loc 1278, at 2607,3343 and 2604,3346) and TWO 2x2 boulders (loc 10790 at 2611-2612,3347-3348, loc 10791 at 2610-2611,3349-3350, shape 10 centrepiece -- real collision, all.loc confirms width=2/length=2) that together seal x=2610-2612 for the whole z=3347-3350 band. A single tile inside this pocket (measured repeatedly at 2608,3345) has BOTH its Z neighbours unreachable by walk (t.player.walk_to times out approaching either one, every attempt) and both its X neighbours' own pushes answering 'ok' with no movement -- a genuine 4-cardinal dead end this file's own herd loop cannot press its way out of. Eight rounds of real, verified fixes landed this session (a proactive boulder detour, a 2-cycle stuck detector, an x-escape direction bug, a resync-vs-escalation timing race that left x-escape entirely unexercised for 3 of those rounds, and an axis-ladder priority bug that let Z drift into the boulder band before the detour ran) each measurably changed the trace and each is a real defect fixed, not a workaround -- but the combination of this terrain and t.player.press having no verb to aim a specific one of the three near-identical live copies (trap 21, QUEST_AUTHORING.md: 'no verb to choose a specific slot') means a press aimed at a clear copy can still land on and fail to move the wedged one. Read the per-press ledger above (stepped/walled counts and the full history) and build/author_state/sonnet-b20/sheepherder.author.progress.md (all 8 runs' evidence) for the complete trail.",
                    def.npc, pressed, outcome_counts.stepped, outcome_counts.walled, outcome_counts.out_of_range, outcome_counts.lost, last_detail))
                return
            end
        end

        -- ---------------------------------------------------------------
        -- Enter the enclosure a SECOND time (sheepherder_gate.rs2 teleports
        -- across the gate loc once worn gear is confirmed) and dispose of
        -- each sheep: poisoned_feed on the now-visible
        -- herder_plaguesheep_N_enclosure (multivarbit=sheepherder_sheep_
        -- <letter>, hidden until that bit went non-zero above -- trap
        -- 28/19, target the BASE spawned symbol, never the wild child),
        -- pick up its bones, then use them on the furnace. The herd loop
        -- above prodded all four sheep in from OUTSIDE (diseased_sheep.rs2
        -- [label,prod_sheep] refuses to prod an npc already inside
        -- sheepherder_in_pen) and exitEnclosureAfterCattleprod above left
        -- the player outside too, so this crossing is genuinely needed,
        -- not a leftover.
        -- Same adjacent-approach settle trap as enterEnclosure1 above:
        -- goto_tile lands EXACTLY on the gate loc's own tile (2594,3362,
        -- loc 166 -- m40_52.jl2) if aimed there directly, and
        -- click_loc's own "step off the target tile" then lands the
        -- player on 167 (the gate's second loc, 2594,3361) with NO real
        -- walking route in between -- _settle_after_click's three arms
        -- (mounted sub, new chat line, map_flag route-end) all need
        -- something to resolve ON, and a click with no route, no chat
        -- line and no interface (sheepherder_gate.rs2's p_teleport fires
        -- none of the three) times out (measured: FAIL
        -- "settle_after_click -- walk_near: stepped off the target tile
        -- 2594,3362 (2594,3362 -> 2594,3361)"). Aiming the goto one tile
        -- off the loc itself gives click_loc a real one-tile approach
        -- walk to settle on instead (exitEnclosure at the bottom of this
        -- file, unchanged, already does this by starting elsewhere and
        -- PASSes on that same map_flag arm).
        t.exec("goto-enterEnclosure2", t.player.goto_tile, 2593, 3362, 0)
        -- Recorded directly, not through t.exec: sheepherder_gate.rs2's
        -- p_teleport fires none of _settle_after_click's three arms (no
        -- mounted sub, no new chat line, and -- unlike a normal walked
        -- click -- a one-tile approach may still resolve no map_flag
        -- route-end either), so a settle timeout here does not mean the
        -- teleport itself failed; verify by polling the real world tile
        -- (section 2's teleport-dialogue recipe, applied to a teleporting
        -- LOC instead of a teleporting dialogue) rather than trusting the
        -- click verb's own result word.
        local enter2_result, enter2_detail = t.player.click_loc("plaguesheep_gatel", 1)
        local arrived2 = false
        for _ = 1, 4 do
            local tile2_result, tile2 = t.world.tile()
            if tile2_result == "ok" and tile2.x >= PEN_X_MIN and tile2.x <= PEN_X_MAX and tile2.z >= PEN_Z_MIN and tile2.z <= PEN_Z_MAX then
                arrived2 = true
                break
            end
            t.ticks(1)
        end
        t.check("enterEnclosure2", arrived2,
            string.format("click_loc -> %s (%s); arrived in pen (%d-%d,%d-%d)=%s", tostring(enter2_result), tostring(enter2_detail), PEN_X_MIN, PEN_X_MAX, PEN_Z_MIN, PEN_Z_MAX, tostring(arrived2)))
        if not arrived2 then
            t.blocked(string.format(
                "sheepherder_gate.rs2 [label,sheepherder_gate]: click_loc('plaguesheep_gatel',1) -> %s (%s) and t.world.tile() never read inside the pen (%d-%d,%d-%d) after 4 tick(s) of polling on the SECOND crossing (post-herd, before feeding) -- the gate's own p_teleport fires none of _settle_after_click's three arms (no mounted sub, no new chat line, no map_flag route-end), so this seam is the driver's click-settle never recognising a LOC-triggered teleport, not a missing trigger or wrong clothing (worn plague_jacket/trousers already confirmed above)",
                tostring(enter2_result), tostring(enter2_detail), PEN_X_MIN, PEN_X_MAX, PEN_Z_MIN, PEN_Z_MAX))
            return
        end

        for _, def in ipairs(sheep_defs) do
            local sheep_target, bs_result, bs_name = t.player.by_symbol("npc", def.enclosure)
            if not sheep_target then
                t.blocked(string.format(
                    "diseased_sheep.rs2: by_symbol('npc','%s') -> %s (%s) after %s went non-zero -- the enclosure npc never resolved visible",
                    def.enclosure, tostring(bs_result), tostring(bs_name), def.bitvar))
                return
            end
            t.exec("poison" .. def.id, t.player.use_on, "poisoned_feed", sheep_target)
            -- diseased_sheep.rs2 [label,poison_sheep] opens ~mesbox("You feed
            -- the poisoned food to the sheep...") and SUSPENDS on it (trap
            -- 22): the death anim, npc_del and obj_add(npc_coord, bones) all
            -- sit below the mesbox and run only once it is dismissed. The
            -- b12 file read the ground two ticks after the click with the
            -- page still up and found no bones (seam8 scratch,
            -- build/quest_gate/seam8_sheep_mesbox2 row 5: not_found with
            -- chat.kind=mesbox).
            t.exec("poison" .. def.id .. "-dialog", t.chat.play, {
                "mesbox:You feed the poisoned food",
            })
            -- p_delay(0) + npc_anim + p_delay(2) + npc_del + obj_add: the
            -- bones land a few ticks behind the continue.
            local bones_await_result, bones_await_detail = t.await({
                level = function()
                    return t.world.obj_near(def.bones, 15) == "ok"
                end,
                note = def.bones .. " on the pen floor",
            }, 10)
            -- RESUMED after RETRY 1e39261b8: this used to be a RECORDING
            -- row (a literal `true`) carrying an engine-seam t.blocked()
            -- below it, because SS_OP_NPC_DEL used to free the slot before
            -- diseased_sheep.rs2:213's obj_add(npc_coord, ...) could read
            -- it. Seam pass 9 (torirs_server_scripts.c, active_npc_readable)
            -- fixed that -- the deleted npc stays readable for the rest of
            -- the tick -- so this is a real assertion now: the bones must
            -- actually drop, and a FAIL here is a real defect, not a known
            -- engine gap, so there is no t.blocked() to carry the verdict
            -- any more.
            t.check("poison" .. def.id .. ".bones_dropped", bones_await_result == "ok",
                string.format("dropped=%s -- await obj_near(%s, 15) after the mesbox continue -> %s (%s)",
                    tostring(bones_await_result == "ok"), def.bones,
                    tostring(bones_await_result), tostring(bones_await_detail)))

            -- click_obj is a HOLLOW verb (section 8: "ok with a nil
            -- detail"), so it goes through t.exec no longer -- t.exec's
            -- own hollow rule (trap 12) would FAIL an `ok` with no detail.
            -- Call it directly and write what was read back ourselves.
            local bones_before_result, bones_before = t.inv.count(def.bones)
            local click_result, click_detail = t.player.click_obj(def.bones)
            t.check("collectBones" .. def.id,
                click_result == "ok",
                string.format("click_obj(%s) -> %s (%s)", def.bones, tostring(click_result), tostring(click_detail)))
            local bones_after_result, bones_after = t.inv.await(def.bones, 1, 5)
            t.check("collectBones" .. def.id .. ".in_backpack",
                bones_after_result == "ok",
                string.format("%s %s -> %s (%s)", def.bones, tostring(bones_before), tostring(bones_after_result), tostring(bones_after)))

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
