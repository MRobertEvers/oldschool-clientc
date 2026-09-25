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
--
-- RESUMED (sonnet-b21) after RETRY 52f0b3a88 (seam13): the committed
-- herd loop (sonnet-b20's 8-round file, t.blocked at the bottom of this
-- history) pressed every sheep with `t.player.press(def.npc, 1, 8)` --
-- NO `opts` selector at all -- and its own t.blocked named the reason as
-- "t.player.press having no verb to aim a specific one of the three
-- near-identical live copies". That verb landed 2026-09-24
-- (pointer.lua's QD.player._npc_copy/_click_npc_copy, seam13's proof
-- build/quest_gate/seam13_aim_after1 14/14): `press(npc, op, ticks, opts)`
-- now takes `{ slot = n }` and aims ONLY that live copy, answering
-- `no_row` rather than silently landing on a neighbour. The whole herd
-- loop is rewritten around it -- every colour tracks the SERVER SLOT
-- `t.npc.tiles` hands back, never a tile, and every press names that
-- exact slot, so the "which of the three copies actually got hit"
-- ambiguity the previous 8 rounds fought (resync timing races, a 2-cycle
-- detector, an x-escape direction bug) cannot happen any more and the
-- machinery built to defend against it is gone. The route itself is
-- unchanged in spirit from the BFS above (climb north of the pen's solid
-- wall, close X into the gate band, close Z down into it) but is now a
-- single small `herd_plan_move` function driving all four colours off
-- their live position, with a boulder-column guard that PREVENTS ever
-- pushing north while still inside the danger x-range instead of
-- detecting the boulder reactively after the fact. See the fresh banner
-- immediately above the herd loop itself for the map evidence
-- (m40_52.jl2 greps for the pen's three walls, the boulders at
-- 2611-2612,3347-3348 / 2610-2611,3349-3350 / 2592-2593,3381-3382, and
-- every colour's own *.spawn rows) re-verified this round.
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
        -- pen_x/pen_z: sheepherder_sheep_data.dbrow's own sheep_in_pen_coord
        -- for each colour (0_40_52_36_34 -> 2596,3362, etc, decoded the same
        -- way section 5's `^*_coord` bullet decodes any of these: world x =
        -- 40*64+local, world z = 52*64+local) -- the FIXED tile
        -- diseased_sheep.rs2's own npc_tele/npc_add lands each enclosure
        -- sheep on, used below to aim a real approach tile at each one
        -- rather than trust a generic walk_near.
        local sheep_defs = {
            { id = "1", npc = "plaguesheep_1", enclosure = "herder_plaguesheep_1_enclosure", bitvar = "sheepherder_sheep_a", bones = "sheepbonesa", goto_x = 2612, goto_z = 3342, pen_x = 2596, pen_z = 3362 },
            -- plaguesheep_2's own spawn rows (m40_52.spawn) are 2621-2623,
            -- 3366-3367 -- x=2624 (this file's own earlier goto, kept from
            -- the scaffold) is the FIRST tile of the next map square
            -- (m40_52 spans world x=2560..2623), and t.player.walk_to a
            -- tile west of it stalled there every attempt this round
            -- (measured: 68 of 75 presses `out_of_range`, the walk never
            -- actually leaving 2624 despite a valid open target). 2618 is
            -- comfortably inside the loaded square instead.
            { id = "2", npc = "plaguesheep_2", enclosure = "herder_plaguesheep_2_enclosure", bitvar = "sheepherder_sheep_b", bones = "sheepbonesb", goto_x = 2618, goto_z = 3367, pen_x = 2597, pen_z = 3363 },
            { id = "3", npc = "plaguesheep_3", enclosure = "herder_plaguesheep_3_enclosure", bitvar = "sheepherder_sheep_c", bones = "sheepbonesc", goto_x = 2558, goto_z = 3391, pen_x = 2597, pen_z = 3360 },
            -- Same map-square-boundary lesson as plaguesheep_2 above:
            -- m40_52 spans world z=3328..3391, and z=3393 (this file's own
            -- earlier goto) is two tiles into the NEXT square north
            -- (m40_53). 3388 is comfortably inside, still close to the
            -- 2610-2612,3390-3391 spawn cluster.
            { id = "4", npc = "plaguesheep_4", enclosure = "herder_plaguesheep_4_enclosure", bitvar = "sheepherder_sheep_d", bones = "sheepbonesd", goto_x = 2613, goto_z = 3388, pen_x = 2596, pen_z = 3359 },
        }
        local GATE_X_MIN, GATE_X_MAX = 2592, 2594
        local GATE_Z_MIN, GATE_Z_MAX = 3360, 3363

        -- RETRY after 52f0b3a88 (seam13): t.player.press now takes an npc
        -- SELECTOR (`{ slot = n }`) that aims ONE named live copy and
        -- answers `no_row` rather than silently landing on a different
        -- one -- QUEST_AUTHORING.md trap 21's fix for "a press aimed at a
        -- clear copy can still land on ... the wedged one", which is what
        -- sonnet-b20's t.blocked used to report here (the committed file
        -- pressed with no `opts` argument at all -- see git history on
        -- this file). The herd loop below is rewritten around that
        -- selector: every colour tracks the SERVER SLOT `t.npc.tiles`
        -- hands back, never a tile, and every press names that exact slot
        -- -- there is no more "which of the three near-identical copies
        -- actually got hit" ambiguity to defend against, so the
        -- multi-hundred-line resync/dead-tile/x-escape machinery the
        -- previous round needed for that ambiguity is gone.
        --
        -- Route (source-checked against LostCity's 2004 map, per the
        -- queue's own RETRY note): plaguesheep_1 spawns inside Farmer
        -- Brumty's barnyard (2609,3344 / 2610,3343 / 2610,3345,
        -- m40_52.spawn) a few tiles south of two 2x2 boulders (loc 10790
        -- at 2611-2612,3347-3348, loc 10791 at 2610-2611,3349-3350 --
        -- confirmed directly against m40_52.jl2, shape 10 centrepiece,
        -- real collision). herd_plan_move below pushes that colour EAST
        -- ONLY while it is still south of/at the boulder band (x<2613 and
        -- z<=3355) -- a pure x-push never changes z, so it cannot ever
        -- walk into the boulders by construction, unlike the previous
        -- round's axis ladder, which let z drift north while x was still
        -- inside the danger column. Once clear (x>=2613, east of both the
        -- pen's own solid east wall at x=2609 and the boulders), it climbs
        -- NORTH past the pen's solid north wall (confirmed continuous,
        -- local z=36 = world z=3364, across the pen's whole x=2595-2609
        -- span, m40_52.jl2) to z>=3365, where no wall crosses x at all.
        -- From there it closes X into the gate's own band (2592-2594 --
        -- confirmed wall-free at any Z the same way, the north wall's own
        -- x-span stopping at 2595) and finally closes Z down into the gate
        -- zone itself (2592-2594,3360-3363). diseased_sheep.rs2's
        -- [label,prod_sheep] tests exactly that zone
        -- (~inzone_coord_pair_table(sheepherder_pen_gate, npc_coord)) after
        -- EVERY push and auto-teleports the sheep in the moment it lands
        -- there -- the loop never has to enter the pen's own west door
        -- itself. The other three colours' own spawns (m40_52.spawn) land
        -- inside different legs of this same route: plaguesheep_2
        -- (2621-2623,3366-3367) and plaguesheep_4 (2610-2612,3390-3391)
        -- already stand north of the pen's wall, so they skip straight to
        -- the X/Z-into-gate legs; plaguesheep_3 (2560-2561,3388-3390)
        -- stands north and WEST of the gate band, so it closes X eastward
        -- (the same "x < gate band, push east" arm the gate-approach leg
        -- uses for every colour) before descending Z -- and its own final
        -- approach is walled by a THIRD copy of the same boulder id at
        -- 2592-2593,3381-3382 (m40_52.jl2, confirmed directly), which the
        -- wall_blacklist x-probe below (try the gate band's other columns)
        -- is what routes around, the same way sonnet-b18's file first
        -- found it.
        local EAST_CLEAR_X = 2613
        local SAFE_NORTH_Z = PEN_Z_MAX + 1 -- 3365, one tile past the solid north wall (z=3364)

        -- The single source of truth for "what does this colour do next",
        -- a pure function of its current tile -- never a hand-picked
        -- direction, so every colour (four very different starting
        -- corners) drives off the same staged plan. Returns
        -- (axis, stand_x, stand_z, description) or nil once the tile is
        -- already inside the gate zone (the bit should be about to flip).
        local function herd_plan_move(x, z)
            if x < EAST_CLEAR_X and z <= 3355 then
                -- Still south of (or level with) the pen's own barnyard;
                -- clear the boulder column eastward first. A pure x-push
                -- never touches z, so this can never drift into the
                -- boulder band (3347-3350) by construction.
                return "x", x - 1, z, "east-clear"
            end
            -- RETRY (sonnet-b21, run 1): gated on `x > GATE_X_MAX`, not on
            -- z alone. z-only gating re-triggered "still climbing" the
            -- moment the very first south-into-the-gate push (below)
            -- landed one tile shy of SAFE_NORTH_Z (3365 -> 3364), sending
            -- the sheep straight back north to 3365 and back south again
            -- forever -- measured a clean 130/130 "stepped" run (no wall
            -- ever hit) that oscillated 2594,3364 <-> 2594,3365 for its
            -- whole press budget once x had already reached the gate's
            -- own column. Once x is at or past GATE_X_MAX there is no
            -- wall left to climb around (the pen's own north wall only
            -- spans x=2595..2609, confirmed against m40_52.jl2), so this
            -- arm must stop applying the instant x reaches the gate band,
            -- not only while z is still short of 3365.
            if x > GATE_X_MAX and z < SAFE_NORTH_Z then
                return "z", x, z - 1, "north"
            end
            if x < GATE_X_MIN then
                return "x", x - 1, z, "east"
            end
            if x > GATE_X_MAX then
                return "x", x + 1, z, "west"
            end
            if z > GATE_Z_MAX then
                return "z", x, z + 1, "south"
            end
            if z < GATE_Z_MIN then
                return "z", x, z - 1, "north-to-gate"
            end
            return nil
        end

        -- Same staged plan, collapsed to a single remaining-tiles number --
        -- used only to rank live copies against each other during a
        -- resync, never to steer a press.
        local function herd_remaining(x, z)
            local cost = 0
            if x < EAST_CLEAR_X and z <= 3355 then
                cost = cost + (EAST_CLEAR_X - x)
                x = EAST_CLEAR_X
            end
            if x > GATE_X_MAX and z < SAFE_NORTH_Z then
                cost = cost + (SAFE_NORTH_Z - z)
                z = SAFE_NORTH_Z
            end
            if x < GATE_X_MIN then
                cost = cost + (GATE_X_MIN - x)
                x = GATE_X_MIN
            elseif x > GATE_X_MAX then
                cost = cost + (x - GATE_X_MAX)
                x = GATE_X_MAX
            end
            if z > GATE_Z_MAX then
                cost = cost + (z - GATE_Z_MAX)
            elseif z < GATE_Z_MIN then
                cost = cost + (GATE_Z_MIN - z)
            end
            return cost
        end

        for _, def in ipairs(sheep_defs) do
            t.exec("goto-herd" .. def.id, t.player.goto_tile, def.goto_x, def.goto_z, 0)

            local pressed = 0
            local last_detail = "no press issued"
            local history = {}
            local outcome_counts = { stepped = 0, walled = 0, out_of_range = 0, lost = 0, no_row = 0, wrong_side = 0, other = 0 }
            local bit_result, bit_value = t.var.varbit(def.bitvar)

            -- Tracked by SERVER SLOT (seam13's selector), never a tile --
            -- a tile goes stale the moment the copy is pushed.
            local slot, tracked_x, tracked_z
            -- A slot proven to have no escape from its own tile (both
            -- forward axes there blacklisted) or that vanished from the
            -- pool outright -- never re-picked; diseased_sheep.rs2 credits
            -- ANY of a colour's copies reaching the gate zone, so a stuck
            -- one is simply abandoned for a fresher one.
            local dead_slots = {}
            -- (x,z,axis) pairs a press already answered 'ok' + no movement
            -- against -- never re-tried; keyed by TILE, not slot, since
            -- the obstacle is the map, not the sheep.
            local wall_blacklist = {}
            local unmoved_streak = 0
            local stuck_x, stuck_z, stuck_count = nil, nil, 0

            local function resync(prefer_slot)
                local tiles_result, tiles_detail, rows = t.npc.tiles(def.npc, 40)
                if tiles_result ~= "ok" or not rows or #rows == 0 then
                    return false, string.format("npc.tiles(%s) -> %s (%s)", def.npc, tostring(tiles_result), tostring(tiles_detail))
                end
                if prefer_slot then
                    for i = 1, #rows do
                        if rows[i].slot == prefer_slot and not dead_slots[prefer_slot] then
                            slot, tracked_x, tracked_z = rows[i].slot, rows[i].x, rows[i].z
                            return true, string.format("kept slot %d at %d,%d", slot, tracked_x, tracked_z)
                        end
                    end
                end
                local best, best_d
                for i = 1, #rows do
                    if not dead_slots[rows[i].slot] then
                        local d = herd_remaining(rows[i].x, rows[i].z)
                        if not best or d < best_d then
                            best, best_d = rows[i], d
                        end
                    end
                end
                if not best then
                    -- every live copy is already marked dead -- press the
                    -- first one again anyway rather than give up with
                    -- copies still visible; the outer cap ends the loop.
                    best, best_d = rows[1], herd_remaining(rows[1].x, rows[1].z)
                end
                slot, tracked_x, tracked_z = best.slot, best.x, best.z
                return true, string.format("picked slot %d at %d,%d (remaining %d)", slot, tracked_x, tracked_z, best_d)
            end

            local _, resync_detail = resync(nil)
            last_detail = "initial resync: " .. tostring(resync_detail)

            -- RETRY (sonnet-b21, run 2): diseased_sheep.rs2's own gate-zone
            -- test reads `npc_coord` immediately after calling `npc_walk`
            -- in the SAME script invocation -- but `npc_walk` only QUEUES
            -- a waypoint (torirs_server_scripts.c: "the npc phase's
            -- stepper walks one tile a tick toward the waypoint"; it does
            -- not move the npc's own coord synchronously). So the check
            -- always reads the npc's PRE-this-press tile, and a press that
            -- pushes the sheep INTO the zone can only be CONFIRMED by a
            -- FOLLOW-UP interaction, once that move has actually settled --
            -- exactly what a real player's next click on it would do.
            -- Measured run 2: plaguesheep_1 stepped into 2594,3363 (dead
            -- centre of the gate) on its 40th and last press, herd_plan_move
            -- correctly read nil next iteration, and the loop used to just
            -- break there -- no confirming press was ever issued, so the
            -- teleport branch never ran and the bit sat at 0 forever. A
            -- small bounded confirm counter below issues a few follow-up
            -- presses once the tracked tile reads inside the zone, instead
            -- of stopping the instant it looks reached.
            local confirm_tries = 0
            while pressed < 130 and slot and (bit_result ~= "ok" or bit_value == 0) do
                local x, z = tracked_x, tracked_z
                local axis, want_x, want_z, desc = herd_plan_move(x, z)
                if axis == nil then
                    confirm_tries = confirm_tries + 1
                    if confirm_tries > 4 then
                        break -- gave it several follow-up presses; stop trying
                    end
                    want_x, want_z, desc, axis = x, z - 1, "confirm-" .. confirm_tries, "z"
                else
                    confirm_tries = 0
                end
                local key = string.format("%d,%d:%s", x, z, axis)
                if wall_blacklist[key] then
                    if axis == "z" and x >= GATE_X_MIN and x <= GATE_X_MAX then
                        -- The gate band is only 3 tiles wide (plaguesheep_3's
                        -- own final approach is walled by a boulder at
                        -- 2592-2593,3381-3382, two of the three columns) --
                        -- probe whichever column has not already had Z
                        -- blacklisted at this exact row before giving up.
                        local probed = false
                        for _, cand_x in ipairs({ GATE_X_MAX, GATE_X_MIN, 2593 }) do
                            if cand_x ~= x and not wall_blacklist[string.format("%d,%d:z", cand_x, z)] then
                                -- Stand on the FAR side of the sheep from
                                -- cand_x so the push moves TOWARD it --
                                -- "standing east pushes west" (the file's
                                -- own push-direction rule), so reaching a
                                -- LARGER cand_x needs the player standing
                                -- WEST (x-1), not east. Run 4 (sonnet-b21)
                                -- had this inverted and it stood east
                                -- while aiming at 2594, which pushed the
                                -- sheep WEST instead -- a clean 2-cycle
                                -- (2591,3383 <-> 2592,3383) that burned
                                -- plaguesheep_3's entire press budget with
                                -- 129/130 presses reading "stepped".
                                want_x = (cand_x > x) and (x - 1) or (x + 1)
                                want_z = z
                                desc = "x-probe-to-" .. tostring(cand_x)
                                axis = "x"
                                probed = true
                                break
                            end
                        end
                        if not probed then
                            want_x, want_z, desc, axis = x, z + 1, "retreat-south", "z"
                        end
                    elseif axis == "x" then
                        -- The forward x push is blocked here -- a south
                        -- nudge is always the direction every colour's own
                        -- approach already came from and is known open;
                        -- never nudge NORTH while still clearing the
                        -- boulder column (x<EAST_CLEAR_X), which is exactly
                        -- how the previous round walked into the boulders.
                        if not wall_blacklist[string.format("%d,%d:z", x, z)] then
                            want_x, want_z, desc = x, z + 1, "south-nudge"
                        else
                            want_x, want_z, desc = x, z - 1, "north-nudge"
                        end
                        axis = "z"
                    else -- axis == "z", outside the gate band (the north climb)
                        if not wall_blacklist[string.format("%d,%d:x", x, z)] then
                            want_x, want_z, desc = x - 1, z, "east-nudge"
                        else
                            want_x, want_z, desc = x + 1, z, "west-nudge"
                        end
                        axis = "x"
                    end
                end

                local walk_result, walk_detail = t.player.walk_to(want_x, want_z, 12)

                -- RETRY after 87ec0250d (npc wander parity): with the
                -- sheep now wandering on its own between presses, a
                -- straight walk_to the intended stand tile can stop
                -- short of it (a fenced row on the direct line) and
                -- leave the player on the SAME side as the sheep instead
                -- of the opposite one -- t.player.press then aims off the
                -- ACTUAL player position, so the press pushes the sheep
                -- the WRONG way. That is exactly what drove
                -- plaguesheep_3 forty tiles off its route (2592,3383 ->
                -- 2552,3392 over presses 86-113, this queue row's own
                -- measurement). Check the actual landed side against the
                -- want tile before pressing; want_x/want_z always differ
                -- from x/z by exactly 1 on the axis in play (every
                -- herd_plan_move/reroute arm above returns that shape).
                local reached = true
                do
                    local tile_result, tile = t.world.tile()
                    if tile_result == "ok" then
                        if axis == "x" then
                            reached = (want_x < x) == (tile.x < x)
                        elseif axis == "z" then
                            reached = (want_z < z) == (tile.z < z)
                        end
                    end
                end
                if not reached then
                    -- Go around: step out 3 tiles on the CROSS axis
                    -- (try both directions -- the obstacle's side is not
                    -- known ahead of time), then back onto the want
                    -- tile, and recheck.
                    for _, cross_off in ipairs({ -3, 3 }) do
                        local around_x, around_z
                        if axis == "x" then
                            around_x, around_z = want_x, z + cross_off
                        else
                            around_x, around_z = x + cross_off, want_z
                        end
                        t.player.walk_to(around_x, around_z, 10)
                        walk_result, walk_detail = t.player.walk_to(want_x, want_z, 10)
                        local tile_result2, tile2 = t.world.tile()
                        if tile_result2 == "ok" then
                            if axis == "x" then
                                reached = (want_x < x) == (tile2.x < x)
                            elseif axis == "z" then
                                reached = (want_z < z) == (tile2.z < z)
                            end
                        end
                        if reached then
                            break
                        end
                    end
                end

                local press_result, press_detail
                if not reached then
                    -- Pressing from here would push the sheep away from
                    -- the goal (the bug this retry fixes) -- skip the
                    -- press this attempt instead of landing a harmful
                    -- one; the stuck-streak machinery below still
                    -- advances (and eventually resyncs to a fresher live
                    -- copy) on a non-"stepped" outcome.
                    press_result, press_detail = "wrong_side", string.format(
                        "walk stopped short of the %s stand tile %d,%d (walk=%s(%s)) -- pressing from the wrong side would push %s away from the goal, skipped",
                        desc, want_x, want_z, tostring(walk_result), tostring(walk_detail), def.npc)
                else
                    press_result, press_detail = t.player.press(def.npc, 1, 8, { slot = slot })
                end
                pressed = pressed + 1

                local moved_x, moved_z = string.match(press_detail or "", "npc slot %d+ %d+,%d+ %-> (%d+),(%d+)")
                local outcome
                if press_result == "ok" and moved_x then
                    outcome = "stepped"
                elseif press_result == "ok" then
                    outcome = "walled"
                elseif press_result == "refused" then
                    outcome = "out_of_range"
                elseif press_result == "timeout" then
                    outcome = "lost"
                elseif press_result == "no_row" then
                    outcome = "no_row"
                elseif press_result == "wrong_side" then
                    outcome = "wrong_side"
                else
                    outcome = "other"
                end
                outcome_counts[outcome] = (outcome_counts[outcome] or 0) + 1

                last_detail = string.format("#%d slot %s %d,%d push %s aim %d,%d walk=%s(%s) press=%s(%s) outcome=%s",
                    pressed, tostring(slot), x, z, desc, want_x, want_z,
                    tostring(walk_result), tostring(walk_detail),
                    tostring(press_result), tostring(press_detail), outcome)
                history[#history + 1] = last_detail
                if def.id == "1" and pressed <= 8 then
                    t.shot("herdprobe1." .. pressed)
                end

                if outcome == "stepped" then
                    tracked_x, tracked_z = tonumber(moved_x), tonumber(moved_z)
                    unmoved_streak = 0
                    stuck_x, stuck_z, stuck_count = nil, nil, 0
                elseif outcome == "no_row" then
                    -- The aimed slot is no longer live at all (out of the
                    -- pool's own radius, or gone) -- a selector never falls
                    -- back to another copy on its own (trap 21), so this
                    -- loop does it explicitly: mark it dead and resync.
                    dead_slots[slot] = true
                    local ok2, detail2 = resync(nil)
                    last_detail = last_detail .. " -- resync: " .. tostring(detail2)
                    unmoved_streak = 0
                    stuck_x, stuck_z, stuck_count = nil, nil, 0
                    if not ok2 then
                        break
                    end
                else
                    if outcome == "walled" then
                        wall_blacklist[key] = true
                    end
                    if x == stuck_x and z == stuck_z then
                        stuck_count = stuck_count + 1
                    else
                        stuck_x, stuck_z, stuck_count = x, z, 0
                    end
                    unmoved_streak = unmoved_streak + 1
                    -- Three attempts running have not moved this exact
                    -- slot at all, or six non-steps have piled up chasing
                    -- nudges -- abandon this copy for a fresher one rather
                    -- than keep re-proving the same tile dead.
                    if stuck_count >= 3 or unmoved_streak >= 6 then
                        dead_slots[slot] = true
                        local ok2, detail2 = resync(nil)
                        last_detail = last_detail .. " -- resync: " .. tostring(detail2)
                        unmoved_streak = 0
                        stuck_x, stuck_z, stuck_count = nil, nil, 0
                        if not ok2 then
                            break
                        end
                    end
                end
                bit_result, bit_value = t.var.varbit(def.bitvar)
            end

            -- RETRY (sonnet-b21, run 2): trap 24 -- the engine writes the
            -- setbit into the NEXT tick's player update, one tick behind
            -- the press's own settle, so a bare `t.var.varbit` read taken
            -- in the SAME iteration as the landing press can still read
            -- the OLD value. Measured: run 2's plaguesheep_1 stepped
            -- slot 100 from 2594,3364 to 2594,3363 -- squarely inside the
            -- gate zone -- on its 40th and final press, herd_plan_move
            -- correctly read nil next iteration (already there) and broke
            -- out, but the immediately-following bit read was still 0 and
            -- the run reported BLOCKED as if the sheep had never arrived.
            -- Poll a few ticks here before grading, the same shape
            -- enterEnclosure1/2 above use for their own loc-teleport
            -- settle.
            for _ = 1, 5 do
                if bit_result == "ok" and bit_value ~= 0 then
                    break
                end
                t.ticks(1)
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
                string.format("herded=%s, %s=%s after %d press(es) (stepped=%d walled=%d out_of_range=%d lost=%d no_row=%d wrong_side=%d) -- %s",
                    tostring(herded), def.bitvar, tostring(bit_value), pressed,
                    outcome_counts.stepped, outcome_counts.walled, outcome_counts.out_of_range, outcome_counts.lost, outcome_counts.no_row, outcome_counts.wrong_side,
                    table.concat(history, " || ")))
            if not herded then
                t.blocked(string.format(
                    "diseased_sheep.rs2 [label,prod_sheep]: %s never reached sheepherder_pen_gate (2592-2594,3360-3363) after %d presses (stepped=%d walled=%d out_of_range=%d lost=%d no_row=%d wrong_side=%d) -- last attempt: %s -- driven with the seam13 npc selector (t.player.press(..., { slot = n })) plus a wrong-side check (RETRY after 87ec0250d, the npc wander-parity landing) that skips a press whenever the walk stopped short of the intended stand tile, so a wrong_side count above 0 names attempts the loop deliberately declined rather than a press that ran and did nothing; this is a real routing seam in the staged east/north/west/south plan (herd_plan_move) or the go-around retry, not the multi-copy ambiguity seam13 fixed. Read the per-press ledger above (the full history) and build/author_state/sonnet-b22/sheepherder.author.progress.md for the trail.",
                    def.npc, pressed, outcome_counts.stepped, outcome_counts.walled, outcome_counts.out_of_range, outcome_counts.lost, outcome_counts.no_row, outcome_counts.wrong_side, last_detail))
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
            -- RETRY (sonnet-b21, run 6): a plain `t.player.walk_near(
            -- sheep_target, 10, 1)` answered "ok (within 1)" for poison2
            -- and `use_on` STILL answered "I can't reach that!" three
            -- times running (the initial press plus both of its own npc
            -- reach retries) -- the shot (70-poison2.png) shows the player
            -- standing at a fence CORNER a step from the sheep, so "within
            -- 1 tile" is not the same as "on a side the pen's own interior
            -- fencing actually lets you press the sheep from". `use_on`
            -- has no multi-side fallback for an NPC target the way
            -- `click_loc` does for a loc (section 3's own use_on banner),
            -- so this drives one by hand: `pen_x`/`pen_z` above is the
            -- sheep's own FIXED landing tile (sheepherder_sheep_data.dbrow
            -- sheep_in_pen_coord), and each of its four neighbours is tried
            -- in turn with a real `goto_tile` -- plain travel within the
            -- already-entered enclosure (the "goto-barn" precedent above),
            -- never a cheat past anything -- until one presses cleanly.
            local poison_result, poison_detail
            local approach_tried = {}
            for _, off in ipairs({ { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } }) do
                local ax, az = def.pen_x + off[1], def.pen_z + off[2]
                t.player.goto_tile(ax, az, 0)
                poison_result, poison_detail = t.player.use_on("poisoned_feed", sheep_target)
                approach_tried[#approach_tried + 1] = string.format("%d,%d->%s", ax, az, tostring(poison_result))
                if poison_result == "ok" then
                    break
                end
            end
            t.check("poison" .. def.id, poison_result == "ok",
                string.format("use_on(poisoned_feed, %s) -> %s (%s) -- approach tiles tried: %s",
                    def.enclosure, tostring(poison_result), tostring(poison_detail), table.concat(approach_tried, "; ")))
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
