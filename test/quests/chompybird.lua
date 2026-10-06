-- Big Chompy Bird Hunting (quest_chompybird). Guide: Quest Helper
-- helpers/quests/bigchompybirdhunting/BigChompyBirdHunting.java.
-- Route: talkToRantz -> getLogs -> makeShafts -> useFeathersOnShafts ->
-- useChiselOnBones -> useTipsOnShafts -> useArrowsOnRantz ->
-- askRantzQuestions -> enterCave -> getBellow -> leaveCave -> fillBellows ->
-- inflateToad -> talkToRantzWithToad -> dropToad -> waitForChompy ->
-- talkToRantzForBow -> placeAnotherToad -> killChompy (ogre bow, ogre arrows,
-- op5) -> pluck -> talkToRantzWithChompy -> enterCaveAgain -> talkToBugs ->
-- talkToFycie -> leaveCaveAgain -> getIngredients -> cookChompy ->
-- giveRantzSeasonedChompy.
-- The chompy and the bait are owner-private and random (a 1-in-5 roll every
-- 25 ticks, four rolls per bait), so the hunt loops with fresh toads.
return {
    id = "chompybird",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv", -- the fixture's tutorial slots, so a requirement fits
        "::give feather 100",
        "::give knife 1",
        "::give chisel 1",
        "::give wolf_bones 4",
        "::give bronze_axe 1",
        "::give lobster 10", -- food for the chompy fight
        "::setlevel fletching 10",
        "::setlevel crafting 10",
        "::setlevel cooking 99",
        "::setlevel strength 60",
        "::setlevel ranged 40",
        "::setlevel hitpoints 60",
    },

    run = function(t)
        -- open ground on the swamp's west edge (reach.py REACH closed-doors from the cave mouth,
        -- Rantz and the bait clearing; see fillBellows)
        local SWAMP_X, SWAMP_Z = 2600, 2969
        -- The swamp's east edge is wolf ground: aggressive level-64 wolves (combat_stats.generated.npc
        -- [wolf]: hitpoints=69, huntmode=aggressive, huntrange 5; m40_46.spawn 2605,2963 / 2607,2967 /
        -- 2610,2958-2965 / 2602,2955) attack a player catching toads there (a b68 probe read 60 -> 14
        -- hp inside one catch). Hitpoints are sampled after every hunt row and a lobster is eaten
        -- below EAT_BELOW, as a player would; hunt.margin grades the lowest sample.
        local EAT_BELOW = 40
        local hunt_low, hunt_eaten = nil, 0
        local function vitals(where)
            local hr, hp = t.skill.read("hitpoints")
            if hr ~= "ok" or type(hp) ~= "table" or hp.level == nil then
                return
            end
            if hunt_low == nil or hp.level < hunt_low then
                hunt_low = hp.level
            end
            if hp.level < EAT_BELOW then
                local er, ed = t.player.inv_op("lobster", 1)
                if er == "ok" then
                    hunt_eaten = hunt_eaten + 1
                end
                t.note("vitals after " .. where .. ": hp " .. hp.level .. " < " .. EAT_BELOW .. ", ate lobster -> " .. tostring(er) .. " " .. tostring(ed))
                t.ticks(3)
            end
        end
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp293_chompybird",
            constants = {
                not_started = 0, started = 5, given_arrows = 10, kids_play_with_toad = 15,
                removed_rock_from_chest = 20, shown_toad = 25, dropped_toad = 30,
                chompy_bird_spawned = 35, rantz_tried_to_shoot_chompy = 40,
                rantz_gave_player_bow = 45, player_killed_chompy = 50,
                told_to_cook_chompy = 55, chompy_cooked = 60, complete = 65,
            },
            row = "quest_bigchompybirdhunting",
            display = "Big Chompy Bird Hunting",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---- talkToRantz: the start menu, every looping branch, then accept ----
        -- Door rule for the first goto (owner 2026-10-05): from the Lumbridge fixture every walk to
        -- the Feldip Hills goes through the Taverley members' gate membergater 2933,3320 (reach.py
        -- 3206,3233 -> 2630,2984: UNREACHABLE at 30/80/160; helper_coverage's fewest-door walk at 400
        -- opens that gate). So: travel to the open ground SOUTH of the gate (reach.py 3206,3233 ->
        -- 2933,3318: REACH closed-doors len=388 at 30/80/160), press the walk-through gate by its
        -- verb (gates.rs2 [label,member_fencegate_try]; as currentaffairs.lua), then the overland walk
        -- to Rantz (reach.py 2933,3322 -> 2630,2984: REACH closed-doors len=1207 at margin 250 --
        -- no door, no gate). No teleport, so no Magic level is staged.
        t.exec("goto-memberGate", t.player.goto_tile, 2933, 3318, 0)
        t.exec("talkToRantz.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
            near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
        t.exec("goto-rantz", t.player.goto_tile, 2630, 2984, 0)
        t.exec("talkToRantz", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantz.dialog.questions", t.chat.play, {
            "npc:Hey you creature! Make me some stabbers",
            "choose:What are 'stabbers'?",
            "player:What are stabbers?",
            "npc:For da stabbie chucker",
            "*",
            "player:I think I understand",
            "npc:Yeah, is what Rantz sayed, make da stabbers",
            "choose:How do I make the 'stabbers'?",
            "player:How do I make the 'stabbers'?",
            "npc:Ahhh, da creature wants to know",
            "choose:What's a 'chompy'?",
            "player:What's a 'chompy'?",
            "npc:Da chompy is der bestest yummies",
            "player:Ah, so 'da chompy' is some kind of bird?",
            "npc:Yeah, is what Rantz sayed, Da chompy is da big flapper",
            "choose:Er, make you're own 'stabbers'!",
            "player:Er, make you're own",
            "npc:When I make 'stabbers'",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.still_not_started", t.quest.expect_stage("not_started"))
        t.exec("talkToRantz.accept", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantz.accept.dialog", t.chat.play, {
            "npc:Hey you creature! Make me some stabbers",
            "choose:Ok, I'll make you some 'stabbers'.",
            "player:OK, I'll make you some 'stabbers'.",
            "npc:Good you creature, you need sticksies",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---- getLogs: five achey logs, chopped ----
        t.exec("goto-achey", t.player.goto_tile, 2628, 2977, 0)
        for i = 1, 5 do
            t.exec("getLogs." .. i, t.player.click_loc, "achey_tree", 1)
            t.exec("getLogs.inv." .. i, t.inv.await, "achey_tree_logs", i, 60)
        end

        -- ---- makeShafts: knife on the logs ----
        for i = 1, 6 do
            local _, logs = t.inv.count("achey_tree_logs")
            if (logs or 0) == 0 then break end
            t.exec("makeShafts." .. i, t.player.use_item_on_item, "knife", "achey_tree_logs")
            t.ticks(3)
        end
        local _, shafts = t.inv.count("ogre_arrow_shaft")
        t.check("makeShafts.count", (shafts or 0) >= 10, "ogre_arrow_shaft=" .. tostring(shafts))

        -- ---- useChiselOnBones: the wolf bones become arrow tips ----
        for i = 1, 5 do
            local _, bones = t.inv.count("wolf_bones")
            if (bones or 0) == 0 then break end
            t.exec("useChiselOnBones." .. i, t.player.use_item_on_item, "chisel", "wolf_bones")
            t.ticks(3)
        end
        local _, tips = t.inv.count("wolfbone_arrowheads")
        t.check("useChiselOnBones.count", (tips or 0) >= 12, "wolfbone_arrowheads=" .. tostring(tips))

        -- ---- useFeathersOnShafts, then useTipsOnShafts ----
        for i = 1, 8 do
            local _, bare = t.inv.count("ogre_arrow_shaft")
            if (bare or 0) == 0 then break end
            t.exec("useFeathersOnShafts." .. i, t.player.use_item_on_item, "feather", "ogre_arrow_shaft")
            t.ticks(3)
        end
        for i = 1, 8 do
            local _, flighted = t.inv.count("ogre_headless_arrow")
            local _, spare_tips = t.inv.count("wolfbone_arrowheads")
            if (flighted or 0) == 0 or (spare_tips or 0) == 0 then break end
            t.exec("useTipsOnShafts." .. i, t.player.use_item_on_item, "wolfbone_arrowheads", "ogre_headless_arrow")
            t.ticks(3)
        end
        local _, arrows = t.inv.count("ogre_arrow")
        t.check("useTipsOnShafts.count", (arrows or 0) >= 8, "ogre_arrow=" .. tostring(arrows))
        local _, kills_bits = t.var.server("varp6192_chompybird_kills")
        t.check("made_arrows.flag", kills_bits ~= nil and kills_bits % 2 == 1, "chompybird_kills=" .. tostring(kills_bits) .. " bit0 = made_arrows")

        -- ---- useArrowsOnRantz: ogre arrows used on Rantz ----
        t.exec("goto-rantz2", t.player.goto_tile, 2630, 2984, 0)
        local rantz = t.player.by_symbol("npc", "rantz")
        t.exec("useArrowsOnRantz", t.player.use_on, "ogre_arrow", rantz)
        t.exec("useArrowsOnRantz.dialog", t.chat.play, {
            "*",
            "npc:Hey you creature..you made da stabbers",
            "player:Well, yes actually",
            "*",
            "npc:Ahh, der creature has dem",
            "npc:But da chompy not coming",
            "choose:What are 'fatsy toadies'?",
            "player:What are 'fatsy toadies'?",
            "npc:Fatsy toadies are da chompy burds",
            "choose:Where do we put the 'fatsy toadies'?",
            "player:Where do we put",
            "npc:Over der!",
            "mesbox:The ogre points to a small clearing",
            "npc:Ok creature? You got dat?",
            "choose:What do you mean 'sneaky..sneaky, stick da chompy?'",
            "player:What do you mean",
            "npc:Duh! You creature is a bit stoopid",
            "choose:How do we make the chompys come?",
            "player:How do we make the chompys come?",
            "npc:Chompys love da fatsy toadies.",
            "npc:Dey's fussie eaters",
            "choose:Ok, thanks.",
            "player:Ok, thanks.",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.kids_play_with_toad", t.quest.expect_stage("kids_play_with_toad"))
        local _, arrows_left = t.inv.count("ogre_arrow")
        t.check("useArrowsOnRantz.six_taken", (arrows_left or 0) == (arrows or 0) - 6, "ogre_arrow " .. tostring(arrows) .. " -> " .. tostring(arrows_left))

        -- ---- enterCave, getBellow (the chest sticks: a strength roll) ----
        -- The cave is entered by its op and left by its op on every visit (door rule): the entrance
        -- rantzogrecaveentrance 2629,2998 (op1 Enter) p_teleports to 2647,9379,0 and the exit
        -- rantzogrecaveexitl 2647,9377 (op1 Walk through) to 2630,2997,0 (chompy_caves.rs2:9-18) -- the
        -- same level in another map frame (z // 6400), so each is a graded climb, and the in-cave hops
        -- depart from the climb's landing (one cave passage: reach.py REACH closed-doors).
        t.exec("goto-cave", t.player.goto_tile, 2630, 2997, 0)
        t.exec("enterCave", t.player.climb, { loc = "rantzogrecaveentrance", op = 1, op_name = "Enter",
            at = { 2629, 2998, 0 }, dest = { 2647, 9379, 0 } })
        t.exec("goto-chest", t.player.goto_tile, 2638, 9396, 0)
        local rock_attempt = 0
        for i = 1, 12 do
            local cr, cd = t.player.click_loc("chompybird_chest", 1)
            t.note("getBellow.rock press " .. i .. ": click_loc(chompybird_chest) -> " .. tostring(cr) .. " " .. tostring(cd))
            t.ticks(6)
            t.chat.close()
            local _, st = t.var.server("varp293_chompybird")
            if st == 20 then rock_attempt = i; break end
        end
        t.check("getBellow.rock", rock_attempt > 0, "rock lifted on attempt " .. rock_attempt)
        t.expect("quest.stage.removed_rock_from_chest", t.quest.expect_stage("removed_rock_from_chest"))
        t.exec("getBellow", t.player.click_loc, "chompybird_chest_open", 1)
        t.exec("getBellow.inv", t.inv.await, "empty_ogre_bellows", 1, 10)
        t.chat.close()

        -- ---- leaveCave ----
        t.exec("goto-caveexit", t.player.goto_tile, 2647, 9379, 0)
        t.exec("leaveCave", t.player.climb, { loc = "rantzogrecaveexitl", op = 1, op_name = "Walk through",
            at = { 2647, 9377, 0 }, dest = { 2630, 2997, 0 } })

        -- ---- fillBellows on the swamp bubbles, inflateToad x3 ----
        -- The swamp (swampbubbles 2595-2601 x 2963-2967) is worked from its WEST edge: the
        -- aggressive level-64 wolves (m40_46.spawn: 2605,2963 / 2607,2967 / 2610,2958-2965,
        -- huntrange 5) stand on its east edge, and the old east tile 2603,2967 had them take the
        -- player from 60 hp to 14 inside one toad catch (b68 scratch probe).
        t.exec("goto-bubbles", t.player.goto_tile, SWAMP_X, SWAMP_Z, 0)
        local bubbles = t.player.by_symbol("loc", "swampbubbles")
        t.exec("fillBellows", t.player.use_on, "empty_ogre_bellows", bubbles)
        t.exec("fillBellows.inv", t.inv.await, "filled_ogre_bellow3", 1, 10)
        t.exec("fillBellows.mes", t.msg.expect, "You collect some gas from the swamp.")
        vitals("fillBellows")
        for i = 1, 3 do
            local toad = t.player.by_symbol("npc", "toad")
            local bellow = ({ "filled_ogre_bellow3", "filled_ogre_bellow2", "filled_ogre_bellow1" })[i]
            t.exec("inflateToad." .. i, t.player.use_on, bellow, toad)
            t.exec("inflateToad.inv." .. i, t.inv.await, "bloated_toad", i, 15)
            t.exec("inflateToad.mes." .. i, t.msg.expect, "You manage to catch the toad and inflate it with the swamp gas.")
            vitals("inflateToad." .. i)
        end

        -- ---- talkToRantzWithToad ----
        t.exec("goto-rantz3", t.player.goto_tile, 2630, 2984, 0)
        t.exec("talkToRantzWithToad", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantzWithToad.dialog", t.chat.play, {
            "npc:Hey you creature, you still here?",
            "npc:Da chompy still not coming!",
            "player:Yes, I have a 'fatsy toady'",
            "*",
            "npc:Dat's a good fatsy toady",
            "player:Where should I put the 'fatsy toadies'?",
            "npc:Over 'dere creature",
            "end",
        })
        -- Rantz points the camera at the toad clearing on "Where should I put..." (rantz.rs2:315-322,
        -- LostCity's own two ops): cam_lookat the clearing, cam_moveto above it; queue(cr_queue) resets it
        -- once the dialogue closes (keg_of_beer.rs2 [queue,cr_queue]).
        t.exec("talkToRantzWithToad.cutscene", t.cutscene.await, "talkToRantzWithToad", { expect = {
            { op = "lookat", coord = "0_41_46_12_22", height = 25 },
            { op = "moveto", coord = "0_41_46_15_12", height = 900 },
            { op = "reset" },
        } })
        t.ticks(4)
        t.expect("quest.stage.shown_toad", t.quest.expect_stage("shown_toad"))

        -- ---- dropToad / waitForChompy: bait until the chompy spawns and Rantz misses ----
        local spawned = false
        for round = 1, 8 do
            local _, held = t.inv.count("bloated_toad")
            if (held or 0) == 0 then
                t.exec("fillBellows.goto." .. round, t.player.goto_tile, SWAMP_X, SWAMP_Z, 0)
                local swamp = t.player.by_symbol("loc", "swampbubbles")
                t.exec("fillBellows." .. round, t.player.use_on, "empty_ogre_bellows", swamp)
                t.exec("fillBellows.inv." .. round, t.inv.await, "filled_ogre_bellow3", 1, 10)
                vitals("fillBellows." .. round)
                for i = 1, 3 do
                    local toad = t.player.by_symbol("npc", "toad")
                    local bellow = ({ "filled_ogre_bellow3", "filled_ogre_bellow2", "filled_ogre_bellow1" })[i]
                    t.exec("inflateToad." .. round .. "." .. i, t.player.use_on, bellow, toad)
                    t.exec("inflateToad.inv." .. round .. "." .. i, t.inv.await, "bloated_toad", i, 15)
                    vitals("inflateToad." .. round .. "." .. i)
                end
            end
            t.exec("dropToad.goto." .. round, t.player.goto_tile, 2634, 2965, 0)
            local _, before = t.inv.count("bloated_toad")
            t.exec("dropToad." .. round, t.player.inv_op, "bloated_toad", 1)
            t.ticks(3)
            t.exec("dropToad.mes." .. round, t.msg.expect, "You carefully place the bloated toad bait.")
            local _, after = t.inv.count("bloated_toad")
            t.check("dropToad.consumed." .. round, (after or 0) == (before or 0) - 1, "bloated_toad " .. tostring(before) .. " -> " .. tostring(after))
            t.exec("dropToad.stepoff." .. round, t.player.goto_tile, 2632, 2968, 0)
            if round == 1 then
                local _, st1 = t.var.server("varp293_chompybird")
                if st1 < 40 then
                    t.exec("goto-rantz4", t.player.goto_tile, 2630, 2984, 0)
                    local _, st2 = t.var.server("varp293_chompybird")
                    if st2 < 40 then
                        t.exec("dropToad.tellRantz", t.player.talk_to, "rantz", 1)
                        t.exec("dropToad.tellRantz.dialog", t.chat.play, {
                            "player:There you go, I've placed the bait.",
                            "npc:Goodz, me now waits for da chompy!",
                            "player:Yes, I know... stick da chompy!",
                            "npc:Hey, you's creature, is da fatsy toady still dere?",
                            "player:What? I have to get more bait",
                            "end",
                        })
                    end
                end
            end
            for wait = 1, 26 do
                local _, st = t.var.server("varp293_chompybird")
                if st >= 40 then spawned = true; break end
                t.ticks(5)
                vitals("waitForChompy." .. round)
            end
            t.note("waitForChompy.round." .. round .. ": spawned=" .. tostring(spawned))
            if spawned then break end
        end
        if not spawned then
            t.blocked("no chompy spawned in eight baits (waitForChompy)")
            return
        end
        t.expect("quest.stage.rantz_tried_to_shoot_chompy", t.quest.expect_stage("rantz_tried_to_shoot_chompy"))
        t.exec("waitForChompy.mes", t.msg.expect, "Rantz keeps missing the chompy bird...")

        -- ---- talkToRantzForBow: refuse, refuse harder, then lend the bow ----
        t.exec("goto-rantz5", t.player.goto_tile, 2630, 2984, 0)
        t.exec("talkToRantzForBow.try1", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantzForBow.try1.dialog", t.chat.play, {
            "player:Hey there, you keep missing the chompy bird.",
            "npc:I knows, I keeps missing",
            "choose:Oh, keep trying then... you might hit one through pure luck.",
            "player:Oh, keep trying then",
            "npc:Grrrr... You lookin' like a chompy!",
            "end",
        })
        t.exec("talkToRantzForBow.try2", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantzForBow.try2.dialog", t.chat.play, {
            "player:Hey there, you keep missing the chompy bird.",
            "npc:I knows, I keeps missing",
            "choose:Come on, let me have a go...",
            "player:Come on, let me have a go...",
            "npc:No, is Rantz stabby thrower",
            "choose:Oh suit yourself, you'll just have to go hungry.",
            "player:Oh suit yourself",
            "npc:Or I eat you instead!",
            "end",
        })
        t.expect("quest.stage.still_tried_to_shoot", t.quest.expect_stage("rantz_tried_to_shoot_chompy"))
        t.exec("talkToRantzForBow", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantzForBow.dialog", t.chat.play, {
            "player:Hey there, you keep missing the chompy bird.",
            "npc:I knows, I keeps missing",
            "choose:Come on, let me have a go...",
            "player:Come on, let me have a go...",
            "npc:No, is Rantz stabby thrower",
            "choose:I'm actually quite strong... please let me try.",
            "player:I'm actually quite strong",
            "npc:Oh, ok...I lend you other stabby thrower",
            "*",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.rantz_gave_player_bow", t.quest.expect_stage("rantz_gave_player_bow"))
        t.exec("talkToRantzForBow.inv", t.inv.await, "ogre_bow", 1, 5)

        -- ---- placeAnotherToad, killChompy ----
        local present = false
        for round = 1, 10 do
            local _, held = t.inv.count("bloated_toad")
            if (held or 0) == 0 then
                t.exec("placeAnotherToad.fill.goto." .. round, t.player.goto_tile, SWAMP_X, SWAMP_Z, 0)
                local swamp = t.player.by_symbol("loc", "swampbubbles")
                t.exec("placeAnotherToad.fill." .. round, t.player.use_on, "empty_ogre_bellows", swamp)
                t.exec("placeAnotherToad.fill.inv." .. round, t.inv.await, "filled_ogre_bellow3", 1, 10)
                vitals("placeAnotherToad.fill." .. round)
                for i = 1, 3 do
                    local toad = t.player.by_symbol("npc", "toad")
                    local bellow = ({ "filled_ogre_bellow3", "filled_ogre_bellow2", "filled_ogre_bellow1" })[i]
                    t.exec("placeAnotherToad.inflate." .. round .. "." .. i, t.player.use_on, bellow, toad)
                    t.exec("placeAnotherToad.inflate.inv." .. round .. "." .. i, t.inv.await, "bloated_toad", i, 15)
                    vitals("placeAnotherToad.inflate." .. round .. "." .. i)
                end
            end
            t.exec("placeAnotherToad.goto." .. round, t.player.goto_tile, 2634, 2965, 0)
            t.exec("placeAnotherToad." .. round, t.player.inv_op, "bloated_toad", 1)
            t.ticks(3)
            t.exec("placeAnotherToad.stepoff." .. round, t.player.goto_tile, 2632, 2968, 0)
            for wait = 1, 26 do
                local r = t.npc.await_present("chompybird", 20, 5)
                if r == "ok" then present = true; break end
                vitals("placeAnotherToad.wait." .. round)
            end
            t.note("placeAnotherToad.round." .. round .. ": chompy present=" .. tostring(present))
            if present then break end
        end
        if not present then
            t.blocked("no chompy spawned in ten baits (placeAnotherToad)")
            return
        end

        -- The hunt's margin (wolves, exploding toads): the lowest hp sampled after every hunt row is at
        -- least a quarter of the maximum, AND food is left.
        vitals("placeAnotherToad")
        local hunt_food_result, hunt_food_left = t.inv.count("lobster")
        t.check("hunt.margin", hunt_low ~= nil and hunt_low * 4 >= 60 and hunt_food_result == "ok" and (hunt_food_left or 0) >= 1,
            "lowest hp sampled through the toad hunt " .. tostring(hunt_low) .. "/60, lobsters eaten " .. hunt_eaten
                .. ", left " .. tostring(hunt_food_left) .. " (margin: lowest hp >= 15, a quarter of 60, AND food left)")

        -- refusals on the way to a real ranged kill
        -- Each refused press is an attempt (a note); the refusal's own message is the graded outcome.
        local function refused_press(name)
            local ar, ad = t.player.attack("chompybird", 5, 1)
            t.note(name .. " press: attack(chompybird, op5) -> " .. tostring(ar) .. " " .. tostring(ad))
        end
        refused_press("killChompy.unarmed")
        t.exec("killChompy.unarmed", t.msg.expect, "You'll need a weapon to try and attack this beast.")
        t.exec("killChompy.equip.axe", t.player.equip, "bronze_axe")
        refused_press("killChompy.melee")
        t.exec("killChompy.melee", t.msg.expect, "The Chompy Bird is too quick for your melee weapon.")
        t.exec("killChompy.equip.ogrebow", t.player.equip, "ogre_bow")
        refused_press("killChompy.noammo")
        t.exec("killChompy.noammo", t.msg.expect, "There is no ammo left in your quiver")
        t.exec("killChompy.equip.ogrearrow", t.player.equip, "ogre_arrow")
        local hp_read, hp_before = t.skill.read("hitpoints")
        local hp_at_attack = (hp_read == "ok" and type(hp_before) == "table") and hp_before.level or nil
        local attack_result, attack_detail
        for _ = 1, 4 do
            attack_result, attack_detail = t.player.attack("chompybird", 5, 15, { eat = { item = "lobster", below = 40 } })
            if attack_result == "ok" then break end
            t.ticks(2)
        end
        t.check("killChompy.attack", attack_result == "ok", tostring(attack_result) .. " " .. tostring(attack_detail))
        local _, chompy_dead_detail = t.exec("killChompy", t.npc.await_dead_engaged, 120, 40, { eat = { item = "lobster", below = 40 } })
        -- Margin (chompybird: quest_chompybird.npc:5, hitpoints=10, a real block): the lowest hp the
        -- kill wait's eater read, and the read before the attack, is at least a quarter of the
        -- maximum, AND food is left.
        local low_text, base_text = string.match(tostring(chompy_dead_detail), "lowest hp (%d+)/(%d+)")
        local low, base = tonumber(low_text), tonumber(base_text)
        if low ~= nil and hp_at_attack ~= nil and hp_at_attack < low then
            low = hp_at_attack
        end
        local food_result, food_left = t.inv.count("lobster")
        t.check("killChompy.margin",
            low ~= nil and base ~= nil and low * 4 >= base and food_result == "ok" and (food_left or 0) >= 1,
            "lowest hp " .. tostring(low) .. "/" .. tostring(base) .. " (hp before the attack " .. tostring(hp_at_attack)
                .. "), lobsters left " .. tostring(food_left) .. " of 10 (margin: lowest hp >= a quarter of max AND food left)")
        t.ticks(3)
        t.exec("killChompy.corpse", t.npc.await_present, "chompybird_dead", 10, 3)
        t.expect("quest.stage.player_killed_chompy", t.quest.expect_stage("player_killed_chompy"))

        -- ---- pluckCarcass ----
        local _, feathers_before = t.inv.count("feather")
        t.exec("pluckCarcass", t.player.press, "chompybird_dead", 4, 8)
        t.exec("pluckCarcass.bones", t.player.click_obj, "bones", 3)
        t.exec("pluckCarcass.raw", t.player.click_obj, "raw_chompy", 3)
        t.exec("pluckCarcass.raw.inv", t.inv.await, "raw_chompy", 1, 10)
        local _, feathers_after = t.inv.count("feather")
        local plucked = (feathers_after or 0) - (feathers_before or 0)
        t.check("pluckCarcass.feathers", plucked >= 10 and plucked <= 30, "feathers " .. tostring(feathers_before) .. " -> " .. tostring(feathers_after))

        -- ---- talkToRantzWithChompy ----
        t.exec("goto-rantz6", t.player.goto_tile, 2630, 2984, 0)
        t.exec("talkToRantzWithChompy", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantzWithChompy.dialog", t.chat.play, {
            "npc:Hey You! Got da chompy yet?",
            "player:Yep, here's your chompy",
            "*",
            "npc:Dat's a great chompy",
            "npc:Okay's now you's needs to cook",
            "npc:But's we's particular",
            "player:What! Now I've got the chompy",
            "npc:Yep, da spit's over der",
            "player:HUH!",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.told_to_cook_chompy", t.quest.expect_stage("told_to_cook_chompy"))
        local _, kv0 = t.var.server("varp6192_chompybird_kills")
        local rantz_flavour = math.floor((kv0 or 0) / 2) % 2

        -- ---- enterCaveAgain, talkToBugs, talkToFycie, leaveCaveAgain ----
        t.exec("goto-cave2", t.player.goto_tile, 2630, 2997, 0)
        t.exec("enterCaveAgain", t.player.climb, { loc = "rantzogrecaveentrance", op = 1, op_name = "Enter",
            at = { 2629, 2998, 0 }, dest = { 2647, 9379, 0 } })
        t.exec("goto-bugs", t.player.goto_tile, 2641, 9389, 0)
        t.exec("talkToBugs", t.player.talk_to, "bugs", 1)
        t.exec("talkToBugs.dialog", t.chat.play, { "npc:Dad say's you's making da chompy", "end" })
        t.exec("goto-fycie", t.player.goto_tile, 2649, 9391, 0)
        t.exec("talkToFycie", t.player.talk_to, "fycie", 1)
        t.exec("talkToFycie.dialog", t.chat.play, { "npc:Dad say's you's roastling", "end" })
        local _, kv1 = t.var.server("varp6192_chompybird_kills")
        local bugs_flavour = math.floor((kv1 or 0) / 4) % 4
        local fycie_flavour = math.floor((kv1 or 0) / 16) % 4
        t.check("flavour.rolled", bugs_flavour >= 1 and bugs_flavour <= 2 and fycie_flavour >= 1 and fycie_flavour <= 2,
            "kills=" .. tostring(kv1) .. " rantz=" .. rantz_flavour .. " bugs=" .. bugs_flavour .. " fycie=" .. fycie_flavour)
        t.exec("goto-caveexit2", t.player.goto_tile, 2647, 9379, 0)
        t.exec("leaveCaveAgain", t.player.climb, { loc = "rantzogrecaveexitl", op = 1, op_name = "Walk through",
            at = { 2647, 9377, 0 }, dest = { 2630, 2997, 0 } })

        -- ---- getIngredients: the three flavours, by clicking ----
        if rantz_flavour == 0 then
            t.exec("goto-potato", t.player.goto_tile, 2643, 2960, 0)
            t.exec("getPotato", t.player.click_loc, "potato", 2)
            t.exec("getPotato.inv", t.inv.await, "potato", 1, 10)
        else
            t.exec("goto-onion", t.player.goto_tile, 2584, 2964, 0)
            t.exec("getOnion", t.player.click_loc, "onion", 2)
            t.exec("getOnion.inv", t.inv.await, "onion", 1, 10)
        end
        if bugs_flavour == 1 then
            t.exec("goto-equa", t.player.goto_tile, 2648, 2962, 0)
            t.exec("getEqua", t.player.click_obj, "equa_leaves", 3)
            t.exec("getEqua.inv", t.inv.await, "equa_leaves", 1, 10)
        else
            t.exec("goto-cabbage", t.player.goto_tile, 2573, 2966, 0)
            t.exec("getCabbage", t.player.click_loc, "cabbage", 2)
            t.exec("getCabbage.inv", t.inv.await, "cabbage", 1, 10)
        end
        if fycie_flavour == 1 then
            t.exec("goto-tomato", t.player.goto_tile, 2585, 2966, 0)
            t.exec("getTomato", t.player.click_obj, "tomato", 3)
            t.exec("getTomato.inv", t.inv.await, "tomato", 1, 10)
        else
            t.exec("goto-doogle", t.player.goto_tile, 2565, 2971, 0)
            t.exec("getDoogle", t.player.click_obj, "doogleleaves", 3)
            t.exec("getDoogle.inv", t.inv.await, "doogleleaves", 1, 10)
        end

        -- ---- cookChompy on the spit-roast ----
        t.exec("goto-spit", t.player.goto_tile, 2631, 2983, 0)
        local spit = t.player.by_symbol("loc", "chompybird_spitroast_empty")
        t.exec("cookChompy", t.player.use_on, "raw_chompy", spit)
        t.exec("cookChompy.inv", t.inv.await, "cooked_s_chompy", 1, 30)
        t.ticks(3)
        t.chat.close()
        t.expect("quest.stage.chompy_cooked", t.quest.expect_stage("chompy_cooked"))
        local loose = rantz_flavour == 0 and "potato" or "onion"
        local _, loose_left = t.inv.count(loose)
        t.check("cookChompy.ingredients_used", (loose_left or 0) == 0, loose .. " left " .. tostring(loose_left))

        -- ---- giveRantzSeasonedChompy ----
        local snapshot_result, xp_before = t.skill.snapshot()
        t.step("reward.snapshot", snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot -> " .. tostring(snapshot_result))
        t.exec("goto-rantz7", t.player.goto_tile, 2630, 2985, 0)
        t.exec("giveRantzSeasonedChompy", t.player.talk_to, "rantz", 1)
        t.exec("giveRantzSeasonedChompy.dialog", t.chat.play, {
            "npc:Hey creature, did you's get da cooked chompy",
            "player:Yes, here you go",
            "*",
            "npc:Hey hey! We got da delicious chompy",
            "npc:Tank's very much for da chompy",
            "player:It's my pleasure",
            "end",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.check("reward.fletching", t.skill.expect_gain("fletching", 262, xp_before))
        t.check("reward.cooking", t.skill.expect_gain("cooking", 1470, xp_before))
        t.check("reward.ranged", t.skill.expect_gain("ranged", 735, xp_before))
        -- the ogre bow is the one Rantz lent and the player keeps (worn): take it off and read the backpack
        t.exec("reward.ogre_bow.off", t.player.unequip, "ogre_bow")
        local bow_result, bow_detail = t.inv.expect_has("ogre_bow", 1)
        t.check("reward.ogre_bow", bow_result == "ok", "ogre_bow in backpack after unequip -> " .. tostring(bow_result) .. " " .. tostring(bow_detail))
        local _, kv2 = t.var.server("varp6192_chompybird_kills")
        t.check("kills.reset", kv2 == 0, "chompybird_kills=" .. tostring(kv2))
        t.finish(0)
        return
    end,
}
