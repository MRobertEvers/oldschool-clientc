return {
    id = "itgronigen",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::give woodplank 3",
        "::give bronze_bar 1",
        "::give molten_glass 1",
        "::give steel_scimitar 1",
        "::give lobster 12",
        "::setlevel crafting 10",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 30",
        "::setlevel hitpoints 50",
        "::wield steel_scimitar",
    },
    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp112_itgronigen",
            constants = {
                not_started = 0, started = 1, given_planks = 2, given_bronze = 3, given_glass = 4,
                given_mould = 5, sent_telescope = 6, complete = 7, claimed_wine = 8,
            },
            row = "quest_observatoryquest",
            display = "Observatory Quest",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------------------------
        -- Crossings (door rule, owner 2026-10-03). Every one is pressed or
        -- walked on every visit; no goto lands in, or leaves, a closed space.
        --
        -- The professor's reception (maps/m38_49.jl2: qip_obs_office_wall,
        -- x 2438-2445 z 3184-3191) has ONE door, qip_obs_reception_door on
        -- the north edge of 2442,3183 (doors.loc pairs it with
        -- qip_obs_reception_door_open). The professor (2439,3186) and his
        -- assistant (2443,3189) both stand inside (m38_49.spawn).
        -- ------------------------------------------------------------------
        local function in_reception(tile)
            return tile.level == 0 and tile.x >= 2438 and tile.x <= 2445 and tile.z >= 3184 and tile.z <= 3191
        end
        local function outside_reception(tile)
            return tile.level == 0 and tile.z <= 3183 and not in_reception(tile)
        end
        local RECEPTION_DOOR = { 2442, 3183, 0 }
        local function reception_in(name)
            t.exec(name, t.player.pass_door, { closed = "qip_obs_reception_door", open = "qip_obs_reception_door_open",
                at = RECEPTION_DOOR, near = { 2442, 3182 }, far = { 2442, 3184 }, far_ok = in_reception,
                far_desc = "inside the reception, x 2438-2445 z 3184-3191" })
        end
        local function reception_out(name)
            t.exec(name, t.player.pass_door, { closed = "qip_obs_reception_door", open = "qip_obs_reception_door_open",
                at = RECEPTION_DOOR, near = { 2442, 3184 }, far = { 2442, 3182 }, far_ok = outside_reception,
                far_desc = "outside the reception, z <= 3183" })
        end
        -- The dungeon stairs (qip_obs_vstairs2 2458,3186) stand in the ruined
        -- goblin-village walls east of the reception, open to the south (no
        -- door loc). The static flood (reach.py, doors closed, op locs
        -- blocked) walks 57 tiles round them: these are its waypoints.
        local ROUTE_TO_VSTAIRS = { { 2442, 3182 }, { 2447, 3179 }, { 2450, 3174 }, { 2454, 3170 }, { 2458, 3166 },
            { 2459, 3173 }, { 2460, 3180 }, { 2458, 3184 }, { 2458, 3185 } }
        local ROUTE_FROM_VSTAIRS = { { 2458, 3185 }, { 2461, 3180 }, { 2459, 3174 }, { 2459, 3166 }, { 2453, 3168 },
            { 2449, 3172 }, { 2447, 3178 }, { 2442, 3181 }, { 2442, 3182 } }
        -- Down: maplink.dbrow maplink_0_38_49_26_49_down keys the row on the
        -- player's tile 2458,3185 -> 2355,9394 (map frame 0 -> 1, level 0).
        local function vstairs_down(name)
            t.exec(name, t.player.climb, { loc = "qip_obs_vstairs2", op = 1, op_name = "Climb-down",
                at = { 2458, 3186, 0 }, src = { 2458, 3185 }, dest = { 2355, 9394, 0 } })
        end
        -- Up: observatory_stairs.rs2:26 p_teleports the copy at z > 9370 to
        -- 2458,3185 (the cache spells the op "Climb up").
        local function dungeon_exit_up(name)
            t.exec(name, t.player.climb, { loc = "qip_obs_stairs1_dungeon", op = 1, op_name = "Climb up",
                at = { 2355, 9395, 0 }, src = { 2355, 9394 }, dest = { 2458, 3185, 0 } })
        end

        -- ------------------------------------------------------------------
        -- Start. From the fixture's Lumbridge street a members' gate is the
        -- only way on foot west into Kandarin (reach.py 3206,3233 ->
        -- 2442,3182: UNREACHABLE at margins 30/80/160/250 with every door
        -- closed, NEEDS-DOOR via membergater@2933,3320 at 400; sampler ruling
        -- b59 (a)). So the run's first goto stops on open ground outside
        -- Taverley's east members' gate (gates.rs2 walk-through), the gate is
        -- pressed on foot, and the overland hop to the street south of the
        -- reception door departs from open Taverley ground (reach.py
        -- 2933,3450 -> 2442,3182: REACH closed-doors len 1011, margin 160).
        -- ------------------------------------------------------------------
        t.exec("goto-talkToProfessor.memberGate", t.player.goto_tile, 2938, 3450, 0)
        t.exec("talkToProfessor.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
            near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
            far_desc = "inside Taverley, x <= 2935" })
        -- the walk-through lands on the gate tile; step one more onto Taverley's ground
        t.exec("talkToProfessor.intoTaverley", t.player.walk_to, 2933, 3450)
        t.exec("goto-talkToProfessor", t.player.goto_tile, 2442, 3182, 0)
        reception_in("talkToProfessor.receptionIn")
        t.exec("talkToProfessor", t.player.talk_to, "observatory_professor", 1)
        t.exec("talkToProfessor.dialog", t.chat.play, {
            "npc:Hello adventurer.",
            "choose:I'd like to have a look through that telescope.",
            "player:I'd like to have a look through that telescope.",
            "npc:So would I!",
            "npc:The trouble is",
            "player:What do you mean?",
            "npc:Did you see those houses outside?",
            "player:Up on the hill?",
            "npc:It's a family of goblins.",
            "npc:Since they moved here",
            "npc:Last week, my telescope",
            "npc:Now, parts need replacing",
            "npc:Err, I don't suppose",
            "choose:Sounds interesting, what can I do for you?",
            "player:Sounds interesting",
            "npc:Oh, thanks so much.",
            "npc:I need three new parts",
            "npc:I need wood",
            "npc:My assistant will help",
            "npc:Go talk to him",
            "player:Okay what do I need to do?",
            "npc:First I need three planks",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- assistant hint at stage 1 (LostCity observatory_assistant.rs2 started branch)
        t.exec("assistant.hint", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.hint.dialog", t.chat.play, {
            "npc:How can I help you?",
            "choose:I can't find any planks!",
            "player:I can't find any planks!",
            "npc:I understand planks can be found",
            "end",
        })

        -- planks, bronze, glass: three hand-ins in the same room (talk_to
        -- walks to the professor inside it; nothing is crossed)
        t.exec("giveProfessorPlanks", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorPlanks.dialog", t.chat.play, {
            "npc:It's my helping hand",
            "npc:Do you have the planks yet?",
            "player:Yes, I've got them",
            "npc:Well done",
            "npc:Now, the bronze",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_planks", t.quest.expect_stage("given_planks"))
        t.expect("planks.gone", t.inv.expect_absent("woodplank"))

        t.exec("giveProfessorBar", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorBar.dialog", t.chat.play, {
            "npc:Hello again, do you have the bronze",
            "player:Yes, I have it.",
            "npc:Great. Now all I need",
            "npc:Please get me some molten glass",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_bronze", t.quest.expect_stage("given_bronze"))
        t.expect("bar.gone", t.inv.expect_absent("bronze_bar"))

        t.exec("giveProfessorGlass", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorGlass.dialog", t.chat.play, {
            "npc:How are you getting on finding me some glass?",
            "player:Here it is!",
            "npc:Excellent!",
            "npc:Oh no, I can't use this glass",
            "player:What do you mean, lens mould?",
            "npc:I need my lens mould",
            "npc:I'll have to ask you",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_glass", t.quest.expect_stage("given_glass"))
        t.expect("glass.taken", t.inv.expect_absent("molten_glass"))

        -- ------------------------------------------------------------------
        -- Dungeon: out by the door, walk to the stairs, climb down.
        -- ------------------------------------------------------------------
        reception_out("enterDungeon.receptionOut")
        t.exec("enterDungeon.walk", t.player.walk_route, ROUTE_TO_VSTAIRS)
        vstairs_down("enterDungeon")
        t.ticks(2)
        local _, choice = t.var.server("varb3827_observatory_chestchoice")
        t.check("chestchoice", choice == 0 or choice == 1 or choice == 2,
            "chest choice = " .. tostring(choice) .. " (observatory_dungeon.rs2: 0, 1 or 2, drawn at quest start)")
        local closed = { [0] = "qip_obs_dungeon_chest_closed", [1] = "qip_obs_dungeon_chest_closed2", [2] = "qip_obs_dungeon_chest_closed3" }
        local open = { [0] = "qip_obs_dungeon_chest_open", [1] = "qip_obs_dungeon_chest_open2", [2] = "qip_obs_dungeon_chest_open3" }
        -- An open floor tile beside each chest (reach.py from the stairs'
        -- landing 2355,9394, doors closed: REACH 253 / 53 / 66). The dungeon
        -- is one passage; these hops are travel inside it.
        local beside = { [0] = { 2364, 9354 }, [1] = { 2359, 9365 }, [2] = { 2351, 9360 } }
        local wrong = ((choice or 0) + 1) % 3
        t.exec("goto-wrongchest", t.player.goto_tile, beside[wrong][1], beside[wrong][2], 0)
        t.exec("searchChests.wrong.open", t.player.click_loc, closed[wrong], 1)
        t.ticks(2)
        t.exec("searchChests.wrong.search", t.player.click_loc, open[wrong], 1)
        t.expect("wrongchest.empty", t.msg.expect("The chest is empty."))
        t.expect("wrongchest.no_key", t.inv.expect_absent("keep_key"))
        t.exec("goto-keychest", t.player.goto_tile, beside[choice][1], beside[choice][2], 0)
        t.exec("searchChests.key.open", t.player.click_loc, closed[choice], 1)
        t.ticks(2)
        t.exec("searchChests.key.search", t.player.click_loc, open[choice], 1)
        t.exec("keychest.dialog", t.chat.play, { "mesbox:You find a kitchen key.", "end" })
        t.expect("key.held", t.inv.expect_has("keep_key", 1))

        -- ------------------------------------------------------------------
        -- Kitchen gate (keepgate_closed, a selfstage door on the north edge
        -- of 2327,9393: doors_selfstage.loc) + the sleeping guard north of
        -- it (m36_146.spawn 2327,9394); the stove is south, in the kitchen.
        -- ------------------------------------------------------------------
        t.exec("goto-gate", t.player.goto_tile, 2327, 9395, 0)
        -- An attempt, not a crossing: with the guard asleep beside it the
        -- gate refuses (observatory_dungeon.rs2 observatory_open_kitchen_gate).
        -- The outcome is the warning line below and the gate still shut.
        local gr, gdetail = t.player.click_loc("keepgate_closed", 1, { at = { 2327, 9393, 0 } })
        t.note("gate.click (attempt): " .. tostring(gr) .. " " .. tostring(gdetail))
        t.expect("gate.warn", t.msg.expect("the guard will hear you"))
        t.expect("gate.still_shut", t.world.loc_near("keepgate_closed", 4, { at = { 2327, 9393, 0 } }))
        t.exec("prodGuard", t.player.press, "qip_obs_goblin_guard", 1)
        t.ticks(1)
        t.exec("guard.attack", t.player.attack, "goblin_guard", 2)
        local _, guard_detail = t.exec("guard.dead", t.npc.await_dead_engaged, 240, 40, { eat = { item = "lobster", below = 25 } })
        -- Fight margin: lowest hp at least a quarter of the 50 staged (13)
        -- AND food left. The guard is quest_itgronigen.npc [goblin_guard]
        -- (hitpoints 43, attack 32, defence 37), a real block.
        local guard_low = type(guard_detail) == "string" and tonumber(guard_detail:match("lowest hp (%d+)/")) or nil
        local lr, lobsters = t.inv.count("lobster")
        t.check("guard.margin", guard_low ~= nil and guard_low >= 13 and lr == "ok" and type(lobsters) == "number" and lobsters >= 1,
            "goblin guard: lowest hp " .. tostring(guard_low) .. "/50, lobsters staged 12, left " .. tostring(lobsters)
                .. " (" .. tostring(lr) .. ") (margin: lowest hp >= 13, a quarter of 50, AND food left)")
        t.ticks(6)
        t.expect("guard.bones", t.world.obj_near("bones", 6))

        -- The gate now opens with the key (the guard is gone): through it
        -- south, the stove, and back north through it.
        t.exec("gate.open", t.player.pass_door, { closed = "keepgate_closed", open = "keepgate_closed",
            at = { 2327, 9393, 0 }, near = { 2327, 9394 }, far = { 2327, 9391 },
            far_ok = function(tile) return tile.level == 0 and tile.z <= 9393 and tile.z >= 9385 end,
            far_desc = "in the kitchen, south of the gate (z <= 9393)" })
        t.exec("inspectStove", t.player.click_loc, "qip_obs_dungeon_stove_top_multi", 1)
        t.exec("inspectStove.dialog", t.chat.play, {
            "mesbox:The goblins appear to have been using the lens mould",
            "mesbox:You shake out its contents",
            "end",
        })
        t.expect("mould.held", t.inv.expect_has("lens_mould", 1))
        t.exec("gate.back_north", t.player.pass_door, { closed = "keepgate_closed", open = "keepgate_closed",
            at = { 2327, 9393, 0 }, near = { 2327, 9392 }, far = { 2327, 9395 },
            far_ok = function(tile) return tile.level == 0 and tile.z >= 9394 end,
            far_desc = "north of the kitchen gate (z >= 9394)" })

        -- out by the east stair (2355,9395), walk to the door, in
        t.exec("goto-exit", t.player.goto_tile, 2355, 9394, 0)
        dungeon_exit_up("leaveDungeon")
        t.exec("giveProfessorMould.walk", t.player.walk_route, ROUTE_FROM_VSTAIRS)
        reception_in("giveProfessorMould.receptionIn")

        -- professor takes the mould, hands the glass back (stage 5)
        t.exec("giveProfessorMould", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorMould.dialog", t.chat.play, {
            "npc:Did you bring me the mould?",
            "player:Yes, I've managed to find it.",
            "npc:At last, you've brought all",
            "npc:Oh no! I can't do this.",
            "player:What do you mean?",
            "npc:My crafting skill",
            "npc:You can use the mould with molten glass",
            "npc:As long as you have practiced",
            "mesbox:The professor gives you back the molten glass.",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_mould", t.quest.expect_stage("given_mould"))
        t.expect("glass.returned", t.inv.expect_has("molten_glass", 1))
        t.expect("mould.still_held", t.inv.expect_has("lens_mould", 1))

        -- craft the lens: mould + glass, Crafting 10 (mould is NOT consumed)
        t.exec("useGlassOnMould", t.player.use_item_on_item, "lens_mould", "molten_glass")
        t.exec("useGlassOnMould.dialog", t.chat.play, {
            "mesbox:You pour the molten glass into the mould",
            "mesbox:It has produced a small, convex glass disc.",
            "end",
        })
        t.expect("lens.held", t.inv.expect_has("lens", 1))
        t.expect("glass.spent", t.inv.expect_absent("molten_glass"))
        t.expect("mould.kept_after_cast", t.inv.expect_has("lens_mould", 1))

        t.exec("giveProfessorLensAndMould", t.player.talk_to, "observatory_professor", 1)
        t.exec("giveProfessorLensAndMould.dialog", t.chat.play, {
            "npc:Is the lens finished?",
            "player:Yes, here it is.",
            "npc:Wonderful, at last I can fix the telescope.",
            "npc:I'll take back that mould",
            "npc:Meet me at the Observatory later",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.sent_telescope", t.quest.expect_stage("sent_telescope"))
        t.expect("lens.gone", t.inv.expect_absent("lens"))
        t.expect("mould.taken_back", t.inv.expect_absent("lens_mould"))

        -- assistant at stage 6 (LostCity sent_telescope branch)
        t.exec("assistant.repairman", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.repairman.dialog", t.chat.play, {
            "player:Hello again.",
            "npc:Ah, it's the telescope repairman!",
            "end",
        })

        -- ------------------------------------------------------------------
        -- To the Observatory: the dome's doors (qip_obs_door_left/right) have
        -- no binding, so the way in is the dungeon's south-west stair
        -- (guide: "Follow the dungeon around anti-clockwise").
        -- ------------------------------------------------------------------
        reception_out("enterDungeonAgain.receptionOut")
        t.exec("enterDungeonAgain.walk", t.player.walk_route, ROUTE_TO_VSTAIRS)
        vstairs_down("enterDungeonAgain")
        -- dungeon travel (reach.py 2355,9394 -> 2335,9350: REACH 216, doors closed)
        t.exec("goto-dome-stair", t.player.goto_tile, 2335, 9350, 0)
        -- observatory_stairs.rs2:26: the copy at z <= 9370 p_teleports to 2439,3164
        t.exec("enterObservatory", t.player.climb, { loc = "qip_obs_stairs1_dungeon", op = 1, op_name = "Climb up",
            at = { 2335, 9351, 0 }, dest = { 2439, 3164, 0 } })
        -- qip_obs_stairs1 has no maplink row: ladders.rs2 [proc,climb] lifts
        -- the player one plane on the tile the press was taken from.
        local function in_dome(tile)
            return tile.x >= 2438 and tile.x <= 2445 and tile.z >= 3157 and tile.z <= 3165
        end
        t.exec("goToF2Observatory", t.player.climb, { loc = "qip_obs_stairs1", op = 1, op_name = "Climb-up",
            at = { 2444, 3159, 0 }, dest = { 2443, 3160, 1 }, slack = 2, landed_ok = in_dome,
            landed_desc = "the Observatory's upper floor, x 2438-2445 z 3157-3165" })

        -- the cutscene approximation (dialogue) then the telescope
        t.exec("professor.cutscene", t.player.talk_to, "observatory_professor_multi2", 1)
        t.exec("professor.cutscene.dialog", t.chat.play, {
            "npc:Oh, hi there.",
            "player:Okay, don't let me interrupt.",
            "npc:Thank you.",
            "npc:Welcome back.",
            "player:Hi, this really is impressive.",
            "npc:Certainly is.",
            "end",
        })
        t.exec("viewTelescope", t.player.click_loc, "qip_obs_tele_gear_upper_multi", 1)
        t.expect("viewTelescope.looked", t.msg.expect("You look through the telescope."))
        t.ticks(3)
        t.key("escape")
        t.ticks(3)
        local _, sign = t.var.server("varb3828_observatory_starsign")
        t.check("telescope.sign", type(sign) == "number" and sign >= 0 and sign <= 11,
            "starsign = " .. tostring(sign) .. " (observatory_telescope.rs2: random(12), 0 = Aquarius .. 11 = Pisces)")
        sign = type(sign) == "number" and sign or 0
        local names = { [0] = "Aquarius", "Capricorn", "Sagittarius", "Scorpio", "Libra", "Virgo", "Leo", "Cancer", "Gemini", "Taurus", "Aries", "Pisces" }
        local pages = { [0] = 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3 }
        local wrong_sign = (sign + 5) % 12
        t.exec("professor.wrong", t.player.talk_to, "observatory_professor_multi2", 1)
        local wl = { "npc:Welcome back.", "player:I've had a look through the telescope.", "npc:What did you see?", "player:It was..." }
        for _ = 1, pages[wrong_sign] do wl[#wl + 1] = "choose:~ next ~" end
        wl[#wl + 1] = "choose:" .. names[wrong_sign]
        wl[#wl + 1] = "player:" .. names[wrong_sign] .. "!"
        wl[#wl + 1] = "npc:I'm afraid not."
        wl[#wl + 1] = "end"
        t.exec("professor.wrong.dialog", t.chat.play, wl)
        t.expect("quest.stage.sent_telescope.still", t.quest.expect_stage("sent_telescope"))

        -- Each sign's own reward, literal (observatory_telescope.rs2
        -- observatory_grant_constellation_reward, from the wiki's Rewards
        -- table): stat_advance is in tenths, so 8750 is 875 xp. Every sign
        -- also grants 2,250 Crafting xp (22500) and one uncut sapphire.
        local SIGN_REWARD = {
            [0] = { obj = "waterrune", n = 25 },
            [1] = { skill = "strength", n = 875 },
            [2] = { obj = "maple_longbow", n = 1 },
            [3] = { obj = "weapon_poison", n = 1 },
            [4] = { obj = "lawrune", n = 3 },
            [5] = { skill = "defence", n = 875 },
            [6] = { skill = "hitpoints", n = 875 },
            [7] = { obj = "amulet_of_defence", n = 1 },
            [8] = { obj = "black_2h_sword", n = 1 },
            [9] = { obj = "1dose2strength", n = 1 },
            [10] = { skill = "attack", n = 875 },
            [11] = { obj = "tuna", n = 3 },
        }
        local reward = SIGN_REWARD[sign]
        local function count(obj)
            local r, n = t.inv.count(obj)
            return r == "ok" and type(n) == "number" and n or nil
        end
        local sapphire_before = count("uncut_sapphire")
        local obj_before = reward.obj and count(reward.obj) or nil
        local _, snap = t.skill.snapshot()
        t.exec("tellProfessorConstellation", t.player.talk_to, "observatory_professor_multi2", 1)
        local rl = { "npc:Welcome back.", "player:I've had a look through the telescope.", "npc:What did you see?", "player:It was..." }
        for _ = 1, pages[sign] do rl[#rl + 1] = "choose:~ next ~" end
        rl[#rl + 1] = "choose:" .. names[sign]
        rl[#rl + 1] = "player:" .. names[sign] .. "!"
        rl[#rl + 1] = "npc:That's exactly it!"
        rl[#rl + 1] = "player:Yes! Woo hoo!"
        rl[#rl + 1] = "*"
        rl[#rl + 1] = "*"
        rl[#rl + 1] = "npc:By Saradomin's earlobes!"
        rl[#rl + 1] = "npc:Look in your backpack"
        t.exec("tellProfessorConstellation.dialog", t.chat.play, rl)
        t.ticks(3)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.expect("reward.crafting", t.skill.expect_gain("crafting", 2250, snap))
        local sapphire_after = count("uncut_sapphire")
        t.check("reward.sapphire", sapphire_before ~= nil and sapphire_after ~= nil and sapphire_after - sapphire_before == 1,
            "uncut_sapphire " .. tostring(sapphire_before) .. " -> " .. tostring(sapphire_after) .. " (want +1)")
        if reward.skill then
            t.expect("reward.sign." .. names[sign], t.skill.expect_gain(reward.skill, reward.n, snap))
        else
            local obj_after = count(reward.obj)
            t.check("reward.sign." .. names[sign], obj_before ~= nil and obj_after ~= nil and obj_after - obj_before == reward.n,
                names[sign] .. ": " .. reward.obj .. " " .. tostring(obj_before) .. " -> " .. tostring(obj_after)
                    .. " (want +" .. reward.n .. ")")
        end
        local rr, rd = t.scroll.rewards()
        t.step("reward.lines", rr == "ok" and "PASS" or "FAIL", tostring(rr) .. " " .. tostring(type(rd) == "table" and table.concat(rd.lines or {}, " / ") or rd))
        t.expect("quest.complete", t.quest.expect_complete())

        -- ------------------------------------------------------------------
        -- Back to the assistant: down the dome's stairs, down into the
        -- dungeon, round to the east stair, up, and in by the door.
        -- maplink_1_38_49_11_24_down: 2443,3160,1 -> 2444,3162,0;
        -- maplink_0_38_49_7_28_down: 2439,3164,0 -> 2335,9350,0.
        -- ------------------------------------------------------------------
        t.exec("leaveObservatory.stairsDown", t.player.climb, { loc = "qip_obs_stairs2_down", op = 1, op_name = "Climb-down",
            at = { 2443, 3159, 1 }, src = { 2443, 3160 }, dest = { 2444, 3162, 0 } })
        t.exec("leaveObservatory.dungeonDown", t.player.climb, { loc = "qip_obs_stairs_to_dungeon2", op = 1,
            op_name = "Climb-down", at = { 2438, 3164, 0 }, src = { 2439, 3164 }, dest = { 2335, 9350, 0 } })
        -- dungeon travel (reach.py 2335,9350 -> 2355,9394: REACH, doors closed)
        t.exec("goto-exit2", t.player.goto_tile, 2355, 9394, 0)
        dungeon_exit_up("leaveDungeonAgain")
        t.exec("assistant.wine.walk", t.player.walk_route, ROUTE_FROM_VSTAIRS)
        reception_in("assistant.wine.receptionIn")

        -- assistant hands over the wine (stage 8)
        t.exec("assistant.wine", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.wine.dialog", t.chat.play, {
            "npc:Well, hello again.",
            "npc:You've made my life much easier!",
            "mesbox:The assistant gives you some wine.",
            "player:Thanks very much.",
            "end",
        })
        t.expect("quest.stage.claimed_wine", t.quest.expect_stage("claimed_wine"))
        t.expect("wine.held", t.inv.expect_has("jug_wine", 1))
    end,
}
