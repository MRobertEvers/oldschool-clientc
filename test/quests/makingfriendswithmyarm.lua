-- Making Friends with My Arm (b73). Written against the port in
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_makingfriendswithmyarm/
-- (makingfriendswithmyarm.rs2, mf_cliff.rs2, mf_sneak.rs2, mf_mother.rs2,
-- mf_wom.rs2, mf_prison.rs2, mf_finish.rs2) and Quest Helper's
-- MakingFriendsWithMyArm.java. Every row is named after the guide step it
-- drives.
--
-- Travel (door rule): every closed space is walked in and out.
--   * Lumbridge -> Trollheim: Trollheim Teleport cast from the spellbook (the
--     summit is a pocket on foot; Quest Helper recommends Trollheim teleports).
--     Inside the summit component the hop to the stronghold door is overland.
--   * The stronghold: the door, the south stairs down to the kitchen and back
--     up, the troll ladder to the roof and back down, the top exit.
--   * Summit -> Rellekka: Camelot Teleport, then the overland hop to Larry's
--     jetty (reach.py: REACH closed-doors len=410).
--   * Rellekka <-> Weiss: Larry's boat both times. On the coast every cliff
--     obstacle is crossed by its own op; the town is entered by the broken
--     fence (first visit) and the cleared cave mouth (second visit).
--   * Weiss -> Draynor: Lumbridge Teleport (Weiss is walled and off the map on
--     foot). The Wise Old Man's house door is pressed in and out; the
--     Apothecary's door stands open (map) and is walked through both ways.
--   * The prison and the throne room are joined by the mine steps (maplink
--     0_44_161_28_47 -> 0_44_61_53_37), climbed.
--
-- Stats: the quest's own requirements (Firemaking 66, Mining 72, Construction
-- 35, Agility 68) and the guide's "combat gear" for Don't Know What (163) and
-- Mother (198): Ranged 99 + rune crossbow, Defence 70 + black d'hide,
-- Hitpoints 99, Prayer 90 for Protect from Missiles (the guide's prayer), and
-- Magic 61 for the three teleports. No dialogue on the route branches on the
-- combat level (grep of the route's .rs2 for combat_level: none). Agility is
-- staged at exactly 68: the rockslides can fail (mf_cliff.rs2
-- ~mf_climb_rockslide) and are retried by cross_trap.
return {
    id = "makingfriendswithmyarm",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_eadgarsruse", -- My Arm's Big Adventure's own prerequisite chain
        "::complete quest_myarmsbigadventure", -- wiki Details: required
        "::complete quest_swansong", -- required (the Wise Old Man's favour)
        "::complete quest_coldwar", -- required (Larry's boat; coldwar_larry.rs2 hands Larry to this quest past ^coldwar_complete)
        "::complete quest_romeoandjuliet", -- required (the Apothecary's cadava potion)
        "::setlevel firemaking 66", -- start requirement (Wolfbone, makingfriendswithmyarm.rs2 ~mf_burntmeat_talk)
        "::setlevel mining 72", -- the cave exit (mf_sneak.rs2)
        "::setlevel construction 35", -- the coffin (mf_wom.rs2)
        "::setlevel agility 68", -- the cliff (mf_cliff.rs2 ~mf_agility_ok)
        "::setlevel magic 61", -- Trollheim Teleport (magic_spells.dbrow: 61), Camelot 45, Lumbridge 31
        "::setlevel hitpoints 99", -- combat margin: the stronghold's trolls, the rockslides (up to 15), two bosses
        "::setlevel ranged 99", -- the guide's "combat gear, preferably ranged or melee"
        "::setlevel defence 70", -- black d'hide body (70 Ranged, 40 Defence)
        "::setlevel prayer 90", -- Protect from Missiles (40), the guide's prayer for both fights (about 300 ticks of it, plus Mother's drain)
        "::give xbows_crossbow_runite 1", -- a rune crossbow (Ranged 61)
        "::give xbows_crossbow_bolts_runite 300",
        "::give black_dragonhide_body 1",
        "::give black_dragonhide_chaps 1",
        "::give lawrune 5", -- Trollheim 2 fire + 2 law; Camelot x2 5 air + 1 law; Lumbridge 3 air + 1 earth + 1 law
        "::give firerune 2",
        "::give airrune 13",
        "::give earthrune 1",
        "::give hammer 1", -- guide items (buildCoffin)
        "::give poh_saw 1",
        "::give plank_mahogany 5",
        "::give cloth 1",
        "::give cadavaberries 1", -- talkToApoth
        "::give shark 11", -- food: guide "food and potions" for Matricide
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb6528_my2arm_status",
            constants = {
                not_started = 0, wolfbone = 1, started = 5, roof = 10, roof_spoken = 15,
                larry = 20, larry2 = 25, boat = 30, weiss = 35, rope = 40, boulder = 45,
                sneak = 50, sneaking = 55, hiding = 60, caves = 65, stones = 70, mother = 75,
                mother_chat = 80, arm_weiss = 85, arm_mushroom = 90, arm_flashback = 100,
                wom = 110, coffin = 120, coffin_built = 122, potion_made = 125, wom2 = 132,
                deliver = 135, given = 140, prison = 145, mushroom_dead = 150, dkw_fight = 155,
                dkw_dead = 160, mother_fight = 165, fire_out = 170, after_fight = 175,
                wom_weiss = 178, snowflake = 180, dung = 185, dung_given = 190, notes = 195,
                finish = 196, complete = 200,
            },
            row = "quest_makingfriendswithmyarm",
            display = "Making Friends with My Arm",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- -----------------------------------------------------------------
        -- Helpers
        -- -----------------------------------------------------------------
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
            end
            return tostring(r)
        end
        local function stage(name)
            t.ticks(2)
            t.expect("quest.stage." .. name, t.quest.expect_stage(name))
        end
        -- The same, with no wait: where a troll aims at anyone standing still.
        local function stage_now(name)
            t.expect("quest.stage." .. name, t.quest.expect_stage(name))
        end

        -- Hitpoints sampled after walks and crossings; a shark below EAT_BELOW.
        local EAT_BELOW = 55
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    if t.player.inv_op("shark", 1) == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(1)
                end
            end
        end
        local function margin_row(name, what)
            vitals()
            local fr, food = t.inv.count("shark")
            local hr, hp = t.skill.read("hitpoints")
            t.check(name, hp_low ~= nil and hp_low >= 25 and fr == "ok" and food >= 1,
                what .. ": lowest hp " .. tostring(hp_low) .. "/99, hp now "
                    .. tostring(hr == "ok" and hp.level or hr) .. ", sharks eaten " .. hp_eaten
                    .. ", left " .. tostring(food) .. " (margin: lowest hp >= 25 AND food left)")
            hp_low = nil
        end

        local function walk_check(name, x, z, level, ticks, tol)
            tol = tol or 1
            local wr, wd = t.player.walk_to(x, z, ticks)
            vitals()
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.level == level and math.abs(tt.x - x) <= tol and math.abs(tt.z - z) <= tol,
                "walk_to(" .. x .. "," .. z .. ") -> " .. tostring(wr) .. " " .. tostring(wd) .. "; tile " .. tile_text(r, tt)
                    .. " (want within " .. tol .. " of " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- Drive a conversation the npc has already opened: for each choice,
        -- click through to the menu and pick it; then click through to the end.
        local function converse(name, choices)
            for i, choice in ipairs(choices) do
                t.exec(name .. "-menu" .. i, t.chat.drain, { stop_at = "options" })
                t.exec(name .. "-choose" .. i, t.chat.choose, choice)
            end
            t.exec(name .. "-end", t.chat.drain, {})
        end

        local function at_row(name, ok_fn, desc)
            t.ticks(1)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and ok_fn(tt), "standing at " .. tile_text(r, tt) .. " (want " .. desc .. ")")
        end

        local function has_row(name, item, n, why)
            t.ticks(1)
            local r, c = t.inv.count(item)
            t.check(name, r == "ok" and c == n, item .. " " .. tostring(c) .. " (" .. tostring(r) .. ", want " .. n .. ") -- " .. why)
        end

        local VITALS = { eat = "shark", below = EAT_BELOW }

        -- Wear the combat gear now: the backpack needs the room.
        t.exec("equipCrossbow", t.player.equip, "xbows_crossbow_runite")
        t.exec("equipBolts", t.player.equip, "xbows_crossbow_bolts_runite")
        t.exec("equipBody", t.player.equip, "black_dragonhide_body")
        t.exec("equipChaps", t.player.equip, "black_dragonhide_chaps")

        -- =================================================================
        -- Starting off: Burntmeat and My Arm in the Troll Stronghold
        -- =================================================================
        t.player.teleport_cast("trollheim_teleport", { 2890, 3679, 0 }, { name = "castTrollheimTeleport",
            runes = { { "firerune", 2 }, { "lawrune", 2 } }, where = "the Trollheim summit" })
        t.exec("goto-enterStronghold", t.player.goto_tile, 2840, 3690, 0) -- inside the summit's one walkable component
        t.exec("enterStronghold", t.player.climb, { loc = "troll_stronghold_door", at = { 2839, 3689, 0 },
            dest = { 2837, 10090, 2 } })
        t.ticks(2)
        walk_check("walk-goDownToBurntmeat", 2843, 10051, 2, 60)
        t.exec("goDownToBurntmeat", t.player.climb, { loc = "troll_stronghold_stairstop", at = { 2843, 10051, 2 },
            dest = { 2841, 10051, 1 } })
        walk_check("walk-talkToBurntmeat", 2844, 10057, 1, 20, 2)
        margin_row("toKitchen.margin", "the walk from the stronghold door down to the kitchen")
        t.exec("talkToBurntmeat", t.player.talk_to, "eadgar_troll_chief_cook", 1)
        t.exec("talkToBurntmeat-open", t.chat.play, { "npc:Always humans running through" })
        converse("talkToBurntmeat", { "Yes, I'll take your quest.", "Why in the heck would you choose My Arm?" })
        stage("roof")

        walk_check("walk-goUpFromF1ToMyArm", 2841, 10051, 1, 20)
        t.exec("goUpFromF1ToMyArm", t.player.climb, { loc = "troll_stronghold_stairs", at = { 2842, 10051, 1 },
            dest = { 2845, 10051, 2 } })
        walk_check("walk-goUpToMyArmAfterStart", 2831, 10076, 2, 80)
        t.exec("goUpToMyArmAfterStart", t.player.climb, { loc = "myarm_ladder", at = { 2831, 10077, 2 },
            dest = { 2831, 3676, 0 } })
        t.ticks(2)
        t.exec("talkToMyArmUpstairs", t.player.talk_to, "myarm_fixed", 1)
        converse("talkToMyArmUpstairs", { "I'm doing another quest for Burntmeat.", "Wolfbone said we should go by sea." })
        stage("larry")
        margin_row("roof.margin", "the stronghold walk up to the roof")

        walk_check("walk-leaveRoof", 2831, 3676, 0, 40, 0)
        t.exec("leaveRoof", t.player.climb, { loc = "myarm_exit", at = { 2831, 3677, 0 }, dest = { 2831, 10076, 2 } })
        walk_check("walk-leaveStronghold", 2837, 10090, 2, 80)
        t.exec("leaveStronghold", t.player.climb, { loc = "troll_stronghold_top_exit_mid", at = { 2838, 10090, 2 },
            dest = { 2840, 3690, 0 } })
        margin_row("leaveStronghold.margin", "the walk from the roof out of the stronghold")

        -- =================================================================
        -- Getting to Weiss: Larry's boat
        -- =================================================================
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "castCamelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-talkToLarry", t.player.goto_tile, 2706, 3731, 0) -- overland from Camelot (reach.py REACH)
        t.exec("talkToLarry", t.player.talk_to, "peng_larry_rell", 1)
        converse("talkToLarry", { "Can I transport My Arm in your boat?", "My Arm is a troll." })
        stage("larry2")
        t.exec("talkToLarryAgain", t.player.talk_to, "peng_larry_rell", 1)
        converse("talkToLarryAgain", { "Can I transport My Arm in your boat?" })
        stage("boat")
        t.exec("boardBoat", t.player.click_loc, "peng_boat_rell", 1)
        converse("boardBoat", { "Travel to Weiss." })
        t.ticks(3)
        at_row("boardBoat.landed", function(tt) return tt.level == 0 and tt.x >= 2844 and tt.x <= 2861 and tt.z >= 3958 and tt.z <= 3972 end,
            "the coast below Weiss (Quest Helper weissArrivalArea 2844-2861,3958-3972)")
        stage("weiss")

        -- =================================================================
        -- Troll diplomacy: the cliff
        -- =================================================================
        walk_check("walk-attemptToMine", 2859, 3968, 0, 20)
        t.exec("attemptToMine", t.player.click_loc, "my2arm_cliffbottom_caveentrance", 1)
        t.exec("attemptToMine-dialog", t.chat.play, { "npc:My Arm not gonna be able to get", "player:I can't see whether it's safe", "player:Maybe if I got into the cave" })
        stage("rope")
        walk_check("walk-searchBoatForRopeAndPickaxe", 2856, 3969, 0, 20)
        t.exec("searchBoatForRopeAndPickaxe", t.player.click_loc, "my2arm_shipwreck", 1)
        t.exec("searchBoatForRopeAndPickaxe-take", t.chat.play, { "choose:Take rope" })
        has_row("searchBoatForRopeAndPickaxe.rope", "rope", 1, "the wreck's rope")
        t.exec("searchBoatForPickaxe", t.player.click_loc, "my2arm_shipwreck", 1)
        t.exec("searchBoatForPickaxe-take", t.chat.play, { "choose:Take pickaxe" })
        has_row("searchBoatForPickaxe.pickaxe", "bronze_pickaxe", 1, "the wreck's pickaxe")

        walk_check("walk-climbRocks", 2852, 3966, 0, 20, 0)
        t.exec("climbRocks", t.player.cross_trap, { loc = "my2arm_cliff_shortcut_1", op_name = "Climb", at = { 2852, 3965, 0 },
            src = { 2852, 3966 }, dest = { 2852, 3964 }, vitals = VITALS })
        t.exec("climbRocks2", t.player.cross_trap, { loc = "my2arm_cliff_shortcut_2", op_name = "Climb", at = { 2853, 3964, 0 },
            src = { 2852, 3964 }, dest = { 2855, 3964 }, vitals = VITALS })
        local tree = t.player.by_symbol("loc", "my2arm_cliff_shortcut_3")
        t.exec("useRope", t.player.use_on, "rope", tree)
        stage("boulder")
        has_row("useRope.ropeTied", "rope", 0, "the rope is tied to the tree")
        t.exec("climbRope", t.player.cross_trap, { loc = "my2arm_cliff_shortcut_3_ropetrail_multi", op_name = "Climb", at = { 2854, 3962, 0 },
            src = { 2855, 3964 }, dest = { 2853, 3961 } })
        t.exec("crossLedge", t.player.cross_trap, { loc = "my2arm_cliff_shortcut_4", op_name = "Cross", at = { 2854, 3961, 0 },
            src = { 2853, 3961 }, dest = { 2857, 3961 } })
        walk_check("walk-climbRocks3", 2859, 3962, 0, 10, 0)
        t.exec("climbRocks3", t.player.cross_trap, { loc = "my2arm_cliff_shortcut_5", op_name = "Climb", at = { 2859, 3961, 0 },
            src = { 2859, 3962 }, dest = { 2859, 3960 }, vitals = VITALS })
        walk_check("walk-passTree", 2857, 3957, 0, 10, 0)
        t.exec("passTree", t.player.cross_trap, { loc = "my2arm_cliff_shortcut_6", op_name = "Pass", at = { 2857, 3955, 0 },
            src = { 2857, 3957 }, dest = { 2857, 3954 } })
        t.exec("passTree-dialog", t.chat.play, { "player:A cave! Maybe I can go down" })
        margin_row("cliff.margin", "the cliff")

        walk_check("walk-talkToBoulder", 2865, 3949, 0, 20, 0)
        t.exec("talkToBoulder", t.player.talk_to, "my2arm_sentry_boulder", 1)
        converse("talkToBoulder", { "Wolfbone's sent My Arm from the Troll Stronghold.", "You only need to let My Arm in, not me.", "I'll be back!" })
        stage("sneak")

        -- =================================================================
        -- Infiltrating Weiss: the broken fence and the throwing trolls
        -- =================================================================
        -- A troll aims at anyone who stands still outside a safe spot
        -- (mf_sneak.rs2): the route walks from Quest Helper's sneak point to
        -- sneak point and never stops between them.
        walk_check("walk-crossFence", 2890, 3949, 0, 40, 0)
        t.exec("crossFence", t.player.cross_gate, { loc = "my2arm_town_fence_broken", at = { 2890, 3948, 0 },
            near = { 2890, 3949 }, far_ok = function(tt) return tt.z <= 3948 end, far_desc = "inside Weiss, z <= 3948" })
        walk_check("goSouthSneak", 2887, 3935, 0, 30, 0)
        stage("sneaking") -- on Quest Helper's first sneak point, out of the trolls' sight
        walk_check("goWestSneak1", 2879, 3922, 0, 40, 0)
        walk_check("goWestSneak2", 2865, 3928, 0, 40, 0)
        walk_check("goWestSneak3", 2856, 3923, 0, 40, 0)
        walk_check("goNorth", 2859, 3939, 0, 40, 0)
        walk_check("walk-enterHole", 2854, 3941, 0, 20, 0)
        t.exec("enterHole", t.player.climb, { loc = "my2arm_town_hole_a", at = { 2853, 3943, 0 }, dest = { 2703, 5804, 0 },
            same_level = "mf_sneak.rs2 [label,mf_descend_hole] p_teleport(^mf_cave_ladder_coord)",
            chat = { "npc:Human intruder!", "mesbox:Several trolls follow you", "mesbox:The trolls are coming" } })
        -- "Once descended, quickly go south and pass through the narrow gap."
        t.player.walk_to(2704, 5795, 20)
        t.exec("enterNarrowHole", t.player.cross_trap, { loc = "my2arm_cave_squeeze", op_name = "Pass", at = { 2703, 5794, 0 },
            src = { 2704, 5795 }, dest = { 2704, 5793 } })
        t.exec("enterNarrowHole-dialog", t.chat.play, { "player:It's a good thing I got in here" })
        stage("caves")

        -- The first pool.
        walk_check("walk-enterWater", 2709, 5782, 0, 30, 0)
        t.exec("enterWater", t.player.cross_trap, { loc = "my2arm_cave_waterline", op_name = "Cross", at = { 2710, 5782, 0 },
            src = { 2709, 5782 }, dest = { 2710, 5782 } })
        walk_check("waterSpot1", 2717, 5780, 0, 20, 0)
        walk_check("walk-leaveWater1", 2730, 5781, 0, 30, 0)
        t.exec("leaveWater1", t.player.cross_trap, { loc = "my2arm_cave_waterline", op_name = "Cross", at = { 2730, 5781, 0 },
            src = { 2730, 5781 }, dest = { 2731, 5781 } })

        -- The north pool and the stepping stones.
        walk_check("walk-enterWater2", 2734, 5791, 0, 20, 0)
        t.exec("enterWater2", t.player.cross_trap, { loc = "my2arm_cave_waterline_forhint", op_name = "Cross", at = { 2734, 5792, 0 },
            src = { 2734, 5791 }, dest = { 2734, 5792 } })
        t.exec("enterWater2-dialog", t.chat.play, { "player:Well, there's the cave exit" })
        t.exec("enterWater2-dialogEnd", t.chat.drain, {})
        stage_now("stones")

        -- placeRocks: stand on each square of the x=2738 column from the
        -- north shore out (z 5808 -> 5804), wait until a troll's rock is in
        -- the air aimed at that square, and step one square west so it lands
        -- on the square just left (wiki quick guide: "Click one tile toward
        -- the cave exit as soon as the throwing animation starts").
        -- A stone is read off the client's loc pool on its square
        -- (my2arm_cave_steppingstone_0..5 are locs 33240..33245).
        local function stone_on(x, z)
            local hr, h = t.world.hazard_at(x, z, 0)
            if hr == "ok" and type(h) == "table" then
                for _, row in ipairs(h.locs) do
                    if row.loc_id >= 33240 and row.loc_id <= 33245 then
                        return true
                    end
                end
            end
            return false
        end
        local function in_pool()
            local r, tt = t.world.tile()
            return r == "ok" and tt.x >= 2731 and tt.x <= 2741 and tt.z >= 5792 and tt.z <= 5808, tile_text(r, tt)
        end
        -- A rock that lands on the player washes him back to the south shore
        -- (mf_sneak.rs2 ~mf_knockout_pool): close its message and swim back.
        local function back_into_pool(notes)
            if in_pool() then
                return
            end
            t.chat.drain({})
            local _, where = in_pool()
            t.player.walk_to(2734, 5791, 20)
            local cr = t.player.cross_trap({ loc = "my2arm_cave_waterline_forhint", op_name = "Cross", at = { 2734, 5792, 0 },
                src = { 2734, 5791 }, dest = { 2734, 5792 } })
            notes[#notes + 1] = "washed back to " .. where .. ", swam back in (" .. tostring(cr) .. ")"
        end
        local function place_stone(z)
            local notes = {}
            for attempt = 1, 4 do
                back_into_pool(notes)
                if stone_on(2738, z) then
                    return true, table.concat(notes, "; ")
                end
                t.player.walk_to(2738, z, 20)
                local _, stood = in_pool()
                local seen = "no rock seen"
                for i = 1, 10 do
                    t.ticks(1)
                    local hr, h = t.world.hazard_at(2738, z, 0)
                    if hr == "ok" and type(h) == "table" and #h.projectiles > 0 then
                        seen = "rock seen after " .. i .. " tick(s)"
                        break
                    end
                end
                t.player.walk_to(2737, z, 10)
                local _, stepped = in_pool()
                local landed = false
                for i = 1, 7 do
                    t.ticks(1)
                    if stone_on(2738, z) then
                        landed = true
                        break
                    end
                end
                notes[#notes + 1] = "attempt " .. attempt .. ": stood " .. stood .. ", " .. seen .. ", stepped to " .. stepped
                    .. (landed and ", stone landed" or ", no stone")
            end
            return stone_on(2738, z), table.concat(notes, "; ")
        end
        for z = 5808, 5804, -1 do
            local ok, how = place_stone(z)
            t.check("placeRocks." .. z, ok, "stepping stone at 2738," .. z .. ": " .. tostring(ok) .. " (" .. how .. ")")
        end
        t.exec("placeRocks-dialog", t.chat.play, { "player:I reckon those stepping stones" })
        walk_check("walk-caveExitShore", 2736, 5808, 0, 20, 0)
        t.exec("crossToCaveExit", t.player.cross_trap, { loc = "my2arm_cave_waterline_forhint", op_name = "Cross", at = { 2736, 5808, 0 },
            src = { 2736, 5808 }, dest = { 2736, 5809 } })
        walk_check("walk-mineCave", 2737, 5816, 0, 20, 0)
        t.exec("mineCave", t.player.click_loc, "my2arm_cave_exit_blocked", 1)
        t.exec("mineCave-dialog", t.chat.play, { "npc:T'anks", "mesbox:My Arm uses the stepping stones", "npc:Stop chuckin' rocks" })
        stage("mother")
        at_row("mineCave.throneRoom", function(tt) return tt.level == 0 and tt.x >= 2866 and tt.x <= 2879 and tt.z >= 3929 and tt.z <= 3944 end,
            "Mother's throne room")

        -- =================================================================
        -- Mother, and the plan
        -- =================================================================
        t.exec("talkToMother", t.player.talk_to, "my2arm_mother_enthroned", 1)
        converse("talkToMother", { "Let's move on with the chat.", "Tell him goutweed is delicious.", "Tell him how tough Stronghold trolls are.",
            "Tell him to show respect to his daughter.", "Tell him you love his daughter!" })
        stage("arm_weiss")
        t.exec("talkToMyArmAfterMeeting", t.player.talk_to, "myarm_fixed", 1)
        converse("talkToMyArmAfterMeeting", { "Odd Mushroom, why are you here with us?", "Does Mother respect anything except fighting?",
            "I did some fishing and fetched some stuff." })
        stage("wom")

        -- =================================================================
        -- The Wise "Dead" Man
        -- =================================================================
        t.player.teleport_cast("lumbridge_teleport", { 3222, 3218, 0 }, { name = "castLumbridgeTeleport",
            runes = { { "airrune", 3 }, { "earthrune", 1 }, { "lawrune", 1 } }, where = "Lumbridge" })
        t.exec("goto-talkToWom", t.player.goto_tile, 3088, 3249, 0) -- the lane south of his door
        t.exec("womHouse.doorIn1", t.player.pass_door, { closed = "poordoor", open = "poordooropen", at = { 3088, 3251, 0 },
            near = { 3088, 3250 }, far = { 3088, 3252 } })
        t.exec("talkToWom", t.player.talk_to, "wise_old_man", 1)
        converse("talkToWom", { "Ask about My Arm.", "Can you pretend you're dead?", "You owe me a favour after the Fishing Colony quest." })
        stage("coffin")
        t.exec("buildCoffin", t.player.click_loc, "my2arm_coffin_multi", 5)
        stage("coffin_built")
        has_row("buildCoffin.planks", "plank_mahogany", 0, "the five mahogany planks went into the coffin")
        t.exec("womHouse.doorOut1", t.player.pass_door, { closed = "poordoor", open = "poordooropen", at = { 3088, 3251, 0 },
            near = { 3088, 3252 }, far = { 3088, 3249 } })
        t.exec("goto-talkToApoth", t.player.goto_tile, 3190, 3403, 0) -- the street west of his open door
        t.exec("apothShop.doorIn", t.player.pass_door, { closed = "fai_varrock_door", open = "fai_varrock_door_open", at = { 3192, 3403, 0 },
            near = { 3191, 3403 }, far = { 3193, 3403 } })
        t.exec("talkToApoth", t.player.talk_to, "apothecary", 1)
        converse("talkToApoth", { "Talk about Making Friends with My Arm." })
        stage("wom2")
        has_row("talkToApoth.potion", "my2arm_potion", 1, "the reduced cadava potion")
        t.exec("apothShop.doorOut", t.player.pass_door, { closed = "fai_varrock_door", open = "fai_varrock_door_open", at = { 3192, 3403, 0 },
            near = { 3193, 3403 }, far = { 3190, 3403 } })
        t.exec("goto-talkToWomAfterPrep", t.player.goto_tile, 3088, 3249, 0)
        t.exec("womHouse.doorIn2", t.player.pass_door, { closed = "poordoor", open = "poordooropen", at = { 3088, 3251, 0 },
            near = { 3088, 3250 }, far = { 3088, 3252 } })
        t.exec("talkToWomAfterPrep", t.player.talk_to, "wise_old_man", 1)
        converse("talkToWomAfterPrep", { "Ask about My Arm." })
        stage("deliver")
        t.exec("pickUpCoffin", t.player.click_loc, "my2arm_coffin_multi", 1)
        has_row("pickUpCoffin.coffin", "my2arm_coffin", 1, "the Wise Old Man in his coffin")
        t.exec("womHouse.doorOut2", t.player.pass_door, { closed = "poordoor", open = "poordooropen", at = { 3088, 3251, 0 },
            near = { 3088, 3252 }, far = { 3088, 3249 } })

        -- =================================================================
        -- Rising up
        -- =================================================================
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "castCamelotTeleport2",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
        t.exec("goto-takeBoatWithWom", t.player.goto_tile, 2706, 3731, 0)
        t.exec("takeBoatWithWom", t.player.click_loc, "peng_boat_rell", 1)
        converse("takeBoatWithWom", { "Travel to Weiss." })
        t.ticks(3)
        at_row("takeBoatWithWom.landed", function(tt) return tt.level == 0 and tt.x >= 2844 and tt.x <= 2861 and tt.z >= 3958 and tt.z <= 3972 end,
            "the coast below Weiss")
        walk_check("walk-enterCaveWithWom", 2859, 3968, 0, 20)
        t.exec("enterCaveWithWom", t.player.click_loc, "my2arm_cliffbottom_caveentrance", 1)
        t.ticks(3)
        at_row("enterCaveWithWom.inWeiss", function(tt) return tt.level == 0 and tt.x >= 2835 and tt.x <= 2893 and tt.z >= 3915 and tt.z <= 3948 end,
            "inside Weiss (Quest Helper weiss zone)")
        walk_check("walk-talkToMyArmWithWom", 2876, 3946, 0, 120)
        t.exec("talkToMyArmWithWom", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmWithWom-end", t.chat.drain, {})
        stage("given")
        has_row("talkToMyArmWithWom.coffinGiven", "my2arm_coffin", 0, "My Arm holds the coffin")
        t.exec("talkToMyArmAfterGivingWom", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterGivingWom-end", t.chat.drain, {})
        stage("prison")
        at_row("talkToMyArmAfterGivingWom.prison", function(tt) return tt.level == 0 and tt.x >= 2830 and tt.x <= 2856 and tt.z >= 10326 and tt.z <= 10351 end,
            "the prison below Weiss (Quest Helper weissPrison)")

        t.exec("talkToOddMushroom", t.player.talk_to, "my2arm_mushroom_dying", 1)
        converse("talkToOddMushroom", { "I'll never leave you." })
        stage("mushroom_dead")

        -- Protect from Missiles for both fights (the guide's prayer).
        t.ui.tab("prayer")
        t.ticks(1)
        local _, pw = t.ui.widget("prayerbook:prayer14")
        t.ui.invoke(pw, 1)
        t.ticks(2)
        local pr, pv = t.var.varbit("varb4117_prayer_protectfrommissiles")
        t.check("prayer.protectFromMissiles", pr == "ok" and pv == 1, "Protect from Missiles varbit " .. tostring(pv) .. " (" .. tostring(pr) .. ")")
        t.ui.tab("inventory")

        t.exec("talkToSnowflake", t.player.talk_to, "my2arm_snowflake", 1)
        converse("talkToSnowflake", { "So I can't teleport, and I may lose stuff? Okay." })
        stage("dkw_fight")
        t.exec("killDontKnowWhat", t.player.attack, "my2arm_dontknowwhat_battle", 2, 20)
        t.exec("killDontKnowWhat.dead", t.npc.await_dead_engaged, 400, 12, { eat = { item = "shark", below = EAT_BELOW } })
        t.ticks(6)
        stage("dkw_dead")
        t.exec("killDontKnowWhat-scene", t.chat.drain, {})
        margin_row("killDontKnowWhat.margin", "the fight with Don't Know What")

        walk_check("walk-mineSteps", 2845, 10351, 0, 60)
        t.exec("climbMineSteps", t.player.climb, { loc = "my2arm_mine_steps", at = { 2844, 10352, 0 }, dest = { 2869, 3941, 0 } })
        stage("mother_fight")
        t.exec("pickUpBucket", t.player.click_loc, "my2arm_throne_room_buckets", 1)
        has_row("pickUpBucket.bucket", "bucket_empty", 1, "a bucket from the pile")
        local barrel = t.player.by_symbol("loc", "my2arm_throne_room_water")
        t.exec("useBucketOnWater", t.player.use_on, "bucket_empty", barrel)
        has_row("useBucketOnWater.full", "bucket_water", 1, "the bucket filled at the barrel")
        local fire = t.player.by_symbol("loc", "my2arm_fire_throne_room")
        t.exec("useBucketOnFire", t.player.use_on, "bucket_water", fire)
        stage("fire_out")
        t.exec("killMother", t.player.attack, "my2arm_mother_battle_magic", 2, 20)
        t.exec("killMother.dead", t.npc.await_dead_engaged, 500, 12, { eat = { item = "shark", below = EAT_BELOW } })
        t.ticks(6)
        stage("after_fight")
        margin_row("killMother.margin", "the fight with Mother")

        -- =================================================================
        -- Finishing off
        -- =================================================================
        t.exec("talkToMyArmAfterFight", t.player.talk_to, "myarm_fixed", 1)
        t.exec("talkToMyArmAfterFight-end", t.chat.drain, {})
        stage("wom_weiss")
        t.exec("talkToWomAfterFight", t.player.talk_to, "wom_unarmed", 1)
        t.exec("talkToWomAfterFight-end", t.chat.drain, {})
        stage("snowflake")
        t.exec("talkToSnowflakeAfterFight", t.player.talk_to, "my2arm_snowflake", 1)
        converse("talkToSnowflakeAfterFight", { "Okay, I'll be back." })
        stage("dung")
        walk_check("walk-pickUpGoatDung", 2887, 3944, 0, 60)
        t.exec("pickUpGoatDung", t.player.click_loc, "my2arm_goatdung", 1)
        has_row("pickUpGoatDung.dung", "my2arm_goatpoo", 1, "a bucket of goat dung")
        walk_check("walk-bringDungToSnowflake", 2872, 3936, 0, 60)
        t.exec("bringDungToSnowflake", t.player.talk_to, "my2arm_snowflake", 1)
        t.exec("bringDungToSnowflake-end", t.chat.drain, {})
        stage("notes")
        has_row("bringDungToSnowflake.book", "my2arm_book", 1, "Odd Mushroom's notes")
        t.exec("readNotes", t.player.inv_op, "my2arm_book", 1)
        t.ticks(3)
        t.key("escape")
        t.ticks(2)
        stage("finish")

        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        t.exec("talkToSnowflakeToFinish", t.player.talk_to, "my2arm_snowflake", 1)
        t.exec("talkToSnowflakeToFinish-end", t.chat.drain, {})
        t.ticks(3)
        t.quest.expect_complete()

        t.check("reward.construction", t.skill.expect_gain("construction", 10000, reward_before))
        t.check("reward.firemaking", t.skill.expect_gain("firemaking", 40000, reward_before))
        t.check("reward.mining", t.skill.expect_gain("mining", 50000, reward_before))
        t.check("reward.agility", t.skill.expect_gain("agility", 50000, reward_before))

        t.finish(0)
    end,
}
