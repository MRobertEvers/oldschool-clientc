-- Biohazard, driven the way RSProx index 4372 played it (rev 241, 2026-10-24):
-- Elena -> Jerico -> his cupboard's bird feed -> a cage of pigeons from
-- behind his house -> the feed thrown at the watchtower, the pigeons let go
-- -> Omart's rope ladder (Kilron on the far side) -> the fence into the
-- mourners' yard, a rotten apple in the stew -> the nurse's cupboard and the
-- medical gown -> the Mourner Headquarters' door (turned away, then let in
-- with the gown on) -> the sick mourner upstairs killed for the key -> the
-- gate and the crate's distillator -> Kilron's ladder back -> Elena's vials
-- -> the Chemist in Rimmington (lamps first, then the quest) -> each errand
-- boy his own vial -> the Varrock gate's search -> the vials back at the
-- Dancing Donkey -> Asyff's priest gown -> Julie at Guidor's door, then in
-- the priest's clothes -> Guidor -> Elena -> King Lathas.
--
-- Pages are matched by their opening words against the recording's
-- (docs/rsprox-analysis/quests/biohazard.md); the wire audit reads this
-- run's tick log (t.ticklog, one mark per section).

return {
    id = "biohazard",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- the quest crosses the map four times on foot
    setup = {
        "::clearinv",
        "::biohazardreset",
        "::give gasmask 1", -- worn into West Ardougne (Quest Helper's requirement)
        "::give priest_robe 1", -- the priest's bottom half is the player's own: Asyff gives only the gown (4372 [1183])
        "::complete quest_plaguecity", -- Biohazard's only prerequisite
        "::give rune_scimitar 1",
        "::give lobster 4", -- food for the sick mourner's fight
        "::setlevel hitpoints 99",
        "::setlevel attack 70",
        "::setlevel strength 70",
        "::setlevel defence 70",
        "::setlevel magic 51",
        "::give ardougnescroll 1",
        "::give lawrune 4",
        "::give waterrune 4",
    },

    run = function(t)
        t.ticklog.start()
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp68_biohazard",
            constants = {
                not_started = 0,
                started = 1,
                spoken_jerico = 2,
                used_birdfeed = 3,
                released_pigeons = 4,
                climbed_ladder = 5,
                poisoned_stew = 6,
                found_distillator = 7,
                given_distillator = 10,
                spoken_chemist = 12,
                found_secret = 14,
                reported_elena = 15,
                complete = 16,
            },
            row = "quest_biohazard",
            display = "Biohazard",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equipGasmask", t.player.equip, "gasmask")

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
            end
            return tostring(r)
        end

        -- Read the tile and grade it against a predicate.
        local function check_tile(name, ok_fn, want)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and ok_fn(tt), "world.tile() -> " .. tile_text(r, tt) .. " (want " .. want .. ")")
            return r, tt
        end

        -- Wait for a teleport or walk-through a click queued to land.
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end

        -- Cross a door that opens in place (a next_loc_stage pair: elenadoor2,
        -- poordoor, poshdoor, elfdoor, fai_varrock_castle_door,
        -- w_ardougnedoubledoorl). Walk to the near side and check the tile;
        -- press the CLOSED leaf on the exact door tile AND level (opts.at --
        -- t.world.loc_near returns the first copy on any floor, so it is not
        -- used to find the closed copy); a press that finds no closed copy
        -- there (`no_row`: an earlier press left it open, doors swing back
        -- after 500 ticks) presses nothing, and then the OPEN leaf must stand
        -- within a tile of the door on this level -- a row that fails when
        -- neither leaf is there. Then walk through and check the far tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, level, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 40)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.level == level and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1 and not far_ok(nt),
                "walked to " .. near_x .. "," .. near_z .. "," .. level .. " on the near side of " .. closed_sym .. " " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z, level } })
            local how = "pressed the closed leaf: click_loc(" .. closed_sym .. " at " .. door_x .. "," .. door_z .. "," .. level .. ") -> ok " .. tostring(cd)
            if cr == "ok" then
                t.ticks(1)
            else
                how = "not pressed: it stood open"
                local orr, od = t.world.loc_near(open_sym, 2)
                t.check(prefix .. ".doorStandsOpen",
                    orr == "ok" and od.level == level and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    "no closed " .. closed_sym .. " to press at " .. door_x .. "," .. door_z .. "," .. level .. " (" .. tostring(cr) .. " " .. tostring(cd) .. "); "
                        .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                        .. " (want the open leaf within 1 of the door on level " .. level .. ": an earlier press left it open, so it is walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 40)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and ft.level == level and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. "); " .. how)
        end

        -- A walk-through door or gate (west_ardy_walk_door: mournerstewdoor,
        -- mournerstewdoorup, mournerquaters_gatel, guidordoor; the Varrock
        -- east gate): content teleports the player across, nothing stays
        -- open, so it is pressed on every crossing. A one-tile telejump is a
        -- hop too short for click_loc's teleport arm and may answer `timeout
        -- settle_after_click` on a crossing that landed (start-and-travel: "A
        -- short hop (stiles) does not trip it"), so the row is graded on the
        -- tiles: before NOT on the far side, after ON it, with the click's
        -- answer in the detail.
        local function cross(name, sym, x, z, level, far_ok, far_desc)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { x, z, level } })
            await_tile(far_ok, 10, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and not far_ok(bt) and wr == "ok" and far_ok(wt),
                "from " .. tile_text(br, bt) .. " click_loc(" .. sym .. " at " .. x .. "," .. z .. "," .. level .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; world.tile -> " .. tile_text(wr, wt) .. " (want " .. far_desc .. ")")
        end

        -- A staircase with no maplink row for this direction climbs +/-1
        -- plane on the tile the player stands on ([proc,climb],
        -- ladders_stairs/scripts/ladders.rs2:69-78), so walk to the stand
        -- tile first; the click can answer before the climb lands, so wait
        -- for the plane, then grade the landing.
        local function climb(name, sym, x, z, from_level, stand_x, stand_z, want_level, land_ok, land_desc)
            t.player.walk_to(stand_x, stand_z, 30)
            local sr, st = t.world.tile()
            t.check(name .. ".atStairs", sr == "ok" and st.level == from_level and st.x == stand_x and st.z == stand_z,
                "walked to " .. stand_x .. "," .. stand_z .. "," .. from_level .. " beside " .. sym .. " " .. x .. "," .. z .. " -> " .. tile_text(sr, st))
            local cr, cd = t.player.click_loc(sym, 1, { at = { x, z, from_level } })
            await_tile(function(tt) return tt.level == want_level end, 10, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and wr == "ok" and wt.level == want_level and land_ok(wt),
                "click_loc(" .. sym .. " at " .. x .. "," .. z .. "," .. from_level .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(wr, wt) .. " (want level " .. want_level .. ", " .. land_desc .. ")")
        end

        -- Elena's house (elena2 m40_52.spawn:26, 2592,3336,0) is walled in,
        -- x 2588-2594 z 3334-3338, door elenadoor2 on the south edge of
        -- 2592,3339 (maps/m40_52.jl2); the street is 2592,3340.
        local function in_elena(tt) return tt.x >= 2588 and tt.x <= 2594 and tt.z >= 3334 and tt.z <= 3338 end
        -- Elena's door is walked through (the Plague City door shape, 4372 [743..750])
        local function elena_in(prefix)
            t.exec("goto-" .. prefix, t.player.goto_tile, 2592, 3340, 0)
            cross(prefix .. ".doorIn", "elenadoor2", 2592, 3339, 0,
                function(tt) return tt.z <= 3338 end, "inside Elena's house, z <= 3338")
        end
        local function elena_out(prefix)
            t.player.walk_to(2592, 3338, 20)
            cross(prefix .. ".doorOut", "elenadoor2", 2592, 3339, 0,
                function(tt) return tt.z >= 3339 end, "out on the street, z >= 3339")
        end

        -- ==== Elena, East Ardougne (2592,3336,0; elena2_vis, [opnpc1,elena2]) ====
        -- switch_int(%biohazard), case ^biohazard_not_started: with Plague
        -- City complete, ~biohazard_plague_city_done is true so the "still
        -- have so much work to do" refusal branch is skipped. Page 1 is
        -- the PLAYER's own greeting text with <nc_name(elena2)> spliced
        -- in (trap 18); it is skipped here with the '*' wildcard rather
        -- than guessed, since the interpolated name is not literal source
        -- text (chat.play matches plain substrings, elena.rs2:97).
        -- Lumbridge to Ardougne has no walk on foot that skips a members'
        -- gate, so the trip is a real Ardougne Teleport (owner 2026-10-05:
        -- the first goto obeys the door rule). Read Plague City's scroll
        -- first: elena_teleport_scroll.rs2 [opheld1,ardougnescroll] at
        -- %varp165_elenaquest = ^elena_complete (29, ::complete
        -- quest_plaguecity) writes ^elena_complete_read_scroll (30).
        t.exec("readArdougneScroll", t.player.inv_op, "ardougnescroll", 1)
        t.exec("readArdougneScroll-dialog", t.chat.play, { "objbox:You memorise what is written on the scroll.", "end" })
        t.ticks(2)
        t.exec("goto-eastArdougne", t.player.goto_tile, 2661, 3301, 0)

        -- [190..229]
        t.ticklog.mark("leg.elena_start")
        elena_in("talkToElena")
        -- [186..218]: the first time the recorder said no
        t.exec("talkToElena.no", t.player.talk_to, "elena2_vis", 1)
        t.exec("talkToElena.no-dialog", t.chat.play, {
            "player:Good day to you, Elena.",
            "npc:You too, thanks for freeing me.",
            "npc:It's just a shame the mourners confiscated my equipment.",
            "player:What did they take?",
            "npc:My distillator. I can't test any plague samples",
            "npc:I must somehow retrieve that distillator",
            "options", "choose:No.",
            "player:I'm busy at the moment, I'm afraid.",
            "npc:Fair enough.",
            "end",
        })
        t.expect("quest.stage.still_not_started", t.quest.expect_stage("not_started"))
        t.exec("talkToElena", t.player.talk_to, "elena2_vis", 1)
        t.exec("talkToElena-dialog", t.chat.play, {
            "player:Good day to you, Elena.",
            "npc:You too, thanks for freeing me.",
            "npc:It's just a shame the mourners confiscated my equipment.",
            "player:What did they take?",
            "npc:My distillator. I can't test any plague samples",
            "npc:I must somehow retrieve that distillator",
            "options", "choose:Yes.",
            "player:Of course I can help. Where do we start?",
            "npc:I was hoping you would say that.",
            "player:Any ideas?",
            "npc:My father's friend Jerico is in communication",
            "end",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        elena_out("talkToElena")

        -- [289..311]
        t.ticklog.mark("leg.jerico")
        local function in_jerico(tt) return tt.x >= 2611 and tt.x <= 2617 and tt.z >= 3322 and tt.z <= 3326 end
        t.exec("goto-talkToJerico", t.player.goto_tile, 2609, 3324, 0)
        pass_door("talkToJerico.doorIn", "poordoor", "poordooropen", 2610, 3324, 0, 2609, 3324, 2611, 3325,
            in_jerico, "inside Jerico's house, x 2611-2617 z 3322-3326")
        t.exec("talkToJerico", t.player.talk_to, "jerico", 1)
        t.exec("talkToJerico-dialog", t.chat.play, {
            "player:Hello Jerico.",
            "npc:Hello, I've been expecting you.",
            "player:That's right.",
            "npc:My messenger pigeons help me communicate",
            "npc:I have arranged for two of them to aid you",
            "npc:But be careful, if the mourners catch you, the punishment will be severe.",
            "player:Thanks Jerico.",
            "end",
        })
        t.expect("quest.stage.spoken_jerico", t.quest.expect_stage("spoken_jerico"))
        t.exec("openJericoCupboard", t.player.click_loc, "jericoscupboardshut", 1)
        t.ticks(3)
        t.exec("searchJericoCupboard", t.player.click_loc, "jericoscupboardopen", 1)
        t.exec("searchJericoCupboard-dialog", t.chat.play, {
            "objbox:The cupboard is full of bird feed.",
            "player:Mmm, bird feed! Now what could I do with that?",
            "end",
        })
        t.exec("birdfeed.in-pack", t.inv.await, "birdfeed", 1, 10)
        pass_door("getBirdFeed.doorOut", "poordoor", "poordooropen", 2610, 3324, 0, 2611, 3324, 2608, 3324,
            function(tt) return tt.x <= 2610 end, "out on the street west of the house, x <= 2610")
        t.exec("goto-pigeons", t.player.goto_tile, 2618, 3324, 0)
        t.exec("pickupPigeons", t.player.click_obj, "pigeons", 3)
        t.exec("pigeons.in-pack", t.inv.await, "pigeons", 1, 10)

        -- [385..403]
        t.ticklog.mark("leg.watchtower")
        t.exec("goto-watchtower", t.player.goto_tile, 2563, 3301, 0)
        t.exec("throwBirdfeed", t.player.click_loc, "biowatchtower_op", 1)
        t.exec("throwBirdfeed-dialog", t.chat.play, { "mesbox:You throw a handful of seeds onto the watchtower.", "end" })
        t.expect("quest.stage.used_birdfeed", t.quest.expect_stage("used_birdfeed"))
        t.exec("birdfeed.gone", t.inv.expect_absent, "birdfeed")
        t.exec("releasePigeons", t.player.inv_op, "pigeons", 1)
        t.ticks(6) -- [396..401]: the box with nothing to click, the same box to click five ticks on
        t.exec("releasePigeons-dialog", t.chat.play, { "mesbox:The pigeons fly towards the watchtower.", "end" })
        t.exec("pigeoncage.in-pack", t.inv.await, "pigeoncage", 1, 10)
        t.expect("quest.stage.released_pigeons", t.quest.expect_stage("released_pigeons"))

        -- [419..442]
        t.ticklog.mark("leg.omart")
        t.exec("goto-talkToOmart", t.player.goto_tile, 2559, 3267, 0)
        t.exec("talkToOmart", t.player.talk_to, "omart", 1)
        t.exec("talkToOmart-dialog", t.chat.play, {
            "npc:Well done, the guards are having real trouble",
            "options", "choose:Okay, lets do it.",
            "player:Okay, lets do it.",
        })
        t.ticks(7) -- [426..432]: two boxes with nothing to click, the ladder, the far side
        t.exec("talkToOmart-dialog2", t.chat.play, {
            "mesbox:...and drop down on the other side.",
            "npc:Welcome to West Ardougne friend. I'm Kilron.",
            "player:Thanks Kilron. Do you know where to find",
            "npc:It's the most north eastern building in the city.",
            "end",
        })
        check_tile("talkToOmart.overTheWall", function(tt) return tt.x == 2556 and tt.z == 3267 and tt.level == 0 end,
            "2556,3267,0, the recorded landing west of the wall (4372 [430])")
        t.expect("quest.stage.climbed_ladder", t.quest.expect_stage("climbed_ladder"))

        -- [478..504]
        t.ticklog.mark("leg.stew")
        t.exec("goto-mournerFenceApproach", t.player.goto_tile, 2539, 3331, 0) -- [478]: walked onto the fence's row, then the op
        t.exec("enterBackyard", t.player.click_loc, "mournerstewfence", 1)
        await_tile(function(tt) return tt.x > 2541 end, 10, "fence.in")
        check_tile("enterBackyard.crossed", function(tt) return tt.x == 2542 and tt.z == 3331 end, "2542,3331 in the yard")
        t.player.walk_to(2548, 3331, 20)
        t.exec("takeRottenApple", t.player.click_obj, "rottenapples", 3)
        t.exec("rottenapple.in-pack", t.inv.await, "rottenapples", 1, 10)
        t.exec("poisonCauldron", t.player.use_on, "rottenapples", (t.player.by_symbol("loc", "mournercauldron_op")))
        t.exec("poisonCauldron-dialog", t.chat.play, {
            "mesbox:You place the rotten apple in the pot. It quickly dissolves into the stew.",
            "player:Anyone who eats this stew is going to have a bad time now.",
            "end",
        })
        t.expect("quest.stage.poisoned_stew", t.quest.expect_stage("poisoned_stew"))
        t.player.walk_to(2542, 3331, 10)
        t.exec("exitBackyard", t.player.click_loc, "mournerstewfence", 1)
        await_tile(function(tt) return tt.x <= 2541 end, 10, "fence.out")
        t.ticks(4)

        -- [526..545]
        t.ticklog.mark("leg.nurse")
        t.exec("goto-nurseHut", t.player.goto_tile, 2521, 3275, 0)
        pass_door("nurseHut.doorIn", "poordoor", "poordooropen", 2519, 3275, 0, 2520, 3275, 2517, 3275,
            function(tt) return tt.x <= 2518 end, "inside the nurse's hut, x <= 2518")
        t.exec("openNurseCupboard", t.player.click_loc, "bionursescupboardshut", 1)
        t.ticks(3)
        t.exec("searchNurseCupboard", t.player.click_loc, "bionursescupboardopen", 1)
        t.exec("searchNurseCupboard-dialog", t.chat.play, { "objbox:You find a medical gown in the cupboard.", "end" })
        t.exec("doctor_gown.in-pack", t.inv.await, "doctor_gown", 1, 10)
        pass_door("nurseHut.doorOut", "poordoor", "poordooropen", 2519, 3275, 0, 2518, 3275, 2521, 3275,
            function(tt) return tt.x >= 2519 end, "out on the street east of the hut, x >= 2519")

        -- [567..582]: turned away without the gown, let in with it
        t.ticklog.mark("leg.hq_door")
        t.exec("goto-mournerHqDoor", t.player.goto_tile, 2551, 3318, 0)
        t.exec("mournerHqDoor.noGown", t.player.click_loc, "mournerstewdoor", 1, { at = { 2551, 3320, 0 } })
        t.exec("mournerHqDoor.noGown-dialog", t.chat.play, {
            "npc:Back away citizen. Several mourners are ill with food poisoning.",
            "player:Is there a doctor nearby?",
            "npc:There are no doctors in West Ardougne",
            "end",
        })
        t.exec("equipDoctorGown", t.player.equip, "doctor_gown")
        t.exec("mournerHqDoor.gown", t.player.click_loc, "mournerstewdoor", 1, { at = { 2551, 3320, 0 } })
        t.exec("mournerHqDoor.gown-dialog", t.chat.play, {
            "npc:A doctor? I didn't think there were any left around here.",
            "player:I heard there was some trouble with food poisoning here.",
            "npc:You heard right. Head on in and see what you can do.",
            "end",
        })
        await_tile(function(tt) return tt.z >= 3321 end, 10, "mournerHq.frontDoor")
        check_tile("enterMournerHeadquarters.crossed", function(tt) return tt.x == 2551 and tt.z == 3321 and tt.level == 0 end,
            "2551,3321 inside the front door (4372 [580])")

        -- [583..641]
        t.ticklog.mark("leg.upstairs")
        pass_door("upstairs.innerDoor", "poordoor", "poordooropen", 2546, 3325, 0, 2547, 3325, 2545, 3325,
            function(tt) return tt.x <= 2546 end, "in the stair room, x <= 2546")
        climb("upstairs.stairs", "spiralstairs", 2542, 3324, 0, 2544, 3325, 1,
            function(tt) return tt.x >= 2543 and tt.x <= 2546 and tt.z >= 3323 and tt.z <= 3327 end, "the stair landing")
        cross("upstairs.doorUp", "mournerstewdoorup", 2547, 3325, 1,
            function(tt) return tt.x >= 2547 and tt.level == 1 end, "in the sick mourner's room, x >= 2547 level 1")
        t.exec("equipRuneScimitar", t.player.equip, "rune_scimitar")
        -- [594]: the recorder attacked him outright
        local attack_result, attack_detail = t.player.attack("mournerstew2", 2, 50)
        local dead_result, dead_detail = t.npc.await_dead_engaged(90, 6, { eat = { item = "lobster", below = 50 } })
        t.check("killMourner", (attack_result == "ok" or attack_result == "timeout") and dead_result == "ok",
            "attack(mournerstew2) -> " .. tostring(attack_result) .. " " .. tostring(attack_detail)
                .. "; await_dead_engaged -> " .. tostring(dead_result) .. " " .. tostring(dead_detail))
        t.ticks(3) -- [616..618]: the box two ticks after the death seq
        t.exec("mournerKey-dialog", t.chat.play, { "objbox:The mourner drops a key as he dies.", "end" })
        t.exec("mournerkeytw.in-pack", t.inv.await, "mournerkeytw", 1, 10)
        t.player.walk_to(2551, 3326, 10)
        t.exec("gateIn", t.player.click_loc, "mournerquaters_gatel", 1, { at = { 2551, 3326, 1 } })
        t.exec("gateIn-dialog", t.chat.play, { "mesbox:You use the key to open the gate.", "end" })
        check_tile("gateIn.crossed", function(tt) return tt.x == 2552 and tt.level == 1 end, "2552,z,1 one tile through the gate (4372 [623])")
        t.exec("searchCrate", t.player.click_loc, "mournercrateup", 1, { at = { 2554, 3327, 1 } })
        t.exec("searchCrate-dialog", t.chat.play, { "objbox:You search the crate and find Elena's distillator.", "end" })
        t.exec("distillator.in-pack", t.inv.await, "distillator", 1, 10)
        t.expect("quest.stage.found_distillator", t.quest.expect_stage("found_distillator"))
        cross("gateOut", "mournerquaters_gatel", 2551, 3326, 1,
            function(tt) return tt.x <= 2551 and tt.level == 1 end, "back through the gate, x <= 2551 level 1")
        cross("downstairs.doorUp", "mournerstewdoorup", 2547, 3325, 1,
            function(tt) return tt.x <= 2546 and tt.level == 1 end, "on the stair landing, x <= 2546 level 1")
        local down_result, down_detail = t.player.click_loc("spiralstairstop", 1, { at = { 2543, 3325, 1 } })
        await_tile(function(tt) return tt.level == 0 end, 10, "mournerHq.stairsDown")
        check_tile("downstairs", function(tt) return tt.level == 0 and tt.x >= 2542 and tt.x <= 2546 end,
            "level 0 in the stair room; click_loc -> " .. tostring(down_result) .. " " .. tostring(down_detail))
        pass_door("downstairs.innerDoor", "poordoor", "poordooropen", 2546, 3325, 0, 2545, 3325, 2548, 3324,
            function(tt) return tt.x >= 2547 end, "back in the HQ hall, x >= 2547")
        t.player.walk_to(2551, 3321, 20)
        cross("leaveMournerHeadquarters", "mournerstewdoor", 2551, 3320, 0,
            function(tt) return tt.z <= 3320 and tt.level == 0 end, "on the street south of the HQ, z <= 3320")

        -- [687..700]
        t.ticklog.mark("leg.kilron")
        t.exec("goto-talkToKilron", t.player.goto_tile, 2556, 3267, 0)
        t.exec("talkToKilron", t.player.talk_to, "kilron", 1)
        t.exec("talkToKilron-dialog", t.chat.play, {
            "player:Hello Kilron.",
            "npc:Hello traveller. Do you need to go back over?",
            "options", "choose:Yes I do.",
            "player:Yes I do.",
            "npc:Okay, quickly now!",
        })
        t.ticks(7) -- [693..699]
        t.exec("talkToKilron-dialog2", t.chat.play, {
            "mesbox:...and drop down on the other side.",
            "end",
        })
        check_tile("talkToKilron.overTheWall", function(tt) return tt.x == 2559 and tt.z == 3267 and tt.level == 0 end,
            "2559,3267,0, the recorded landing east of the wall (4372 [697])")

        -- [755..768]
        t.ticklog.mark("leg.elena_distillator")
        elena_in("elenaDistillator")
        t.exec("talkToElenaDistillator", t.player.talk_to, "elena2_vis", 1)
        t.exec("elenaDistillator-dialog", t.chat.play, {
            "npc:You're back, did you find the distillator?",
            "player:Yes, here it is!",
            "npc:Great! Now I can finally run these tests.",
            "mesbox:Elena uses the distillator to test some samples.",
            "npc:I don't understand... the touch paper hasn't changed colour",
            "player:What does that mean?",
            "npc:I'm not sure. My old mentor in Varrock",
            "objbox:Elena gives you a plague sample along with three vials.",
            "npc:Now, last time I went to see Guidor",
            "npc:Ironically, the guards won't be smart enough",
            "player:So what do I do?",
            "npc:Well I know the chemist has still been managing",
            "player:Great, I'll see if he can help. See you soon.",
            "end",
        })
        t.exec("vials.in-pack", t.inv.await_all, { liquid_honey = 1, ethenea = 1, sulphuric_broline = 1, plaguesample = 1 }, 10)
        t.exec("distillator.gone", t.inv.expect_absent, "distillator")
        t.expect("quest.stage.given_distillator", t.quest.expect_stage("given_distillator"))
        elena_out("elenaDistillator")

        -- [878..913]
        t.ticklog.mark("leg.chemist")
        t.exec("goto-chemist.memberGate", t.player.goto_tile, 2934, 3322, 0)
        t.exec("chemist.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3321 }, far_ok = function(tile) return tile.z <= 3319 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "south of the members' gate, z <= 3319", far = { 2934, 3318 } })
        local function in_chemist(tt) return tt.x >= 2929 and tt.x <= 2939 and tt.z >= 3207 and tt.z <= 3213 end
        t.exec("goto-chemist", t.player.goto_tile, 2932, 3215, 0)
        pass_door("chemist.doorIn", "poshdoor", "poshdooropen", 2932, 3214, 0, 2932, 3215, 2932, 3212,
            in_chemist, "inside the chemist's house, x 2929-2939 z 3207-3213")
        t.exec("talkToChemist.lamps", t.player.talk_to, "chemist", 1)
        t.exec("chemist.lamps-dialog", t.chat.play, {
            "options", "choose:Lamps.",
            "player:Hi, I need fuel for a lamp.",
            "npc:Hello there, the fuel you need is lamp oil",
            "options", "choose:Yes please.",
            "player:Yes please.",
            "npc:It's really quite simple.",
            "npc:Just put ordinary swamp tar in",
            "player:Thanks.",
            "end",
        })
        t.exec("talkToChemist.quest", t.player.talk_to, "chemist", 1)
        t.exec("chemist.quest-dialog", t.chat.play, {
            "options", "choose:Your quest.",
            "npc:Sorry, I'm afraid we're just closing now.",
            "player:But I was sent by Elena from Ardougne.",
            "npc:Oh, well that's different then.",
            "player:She asked me to pick up some touch paper",
            "npc:Guidor? This one's on me then",
            "npc:I should warn you though",
            "player:Oh right... so am I going to be okay",
            "npc:With touch paper as well?",
            "player:Well that's no good. How do I stop them",
            "npc:Just make sure you give them something",
            "objbox:The chemist gives you some touch paper.",
            "npc:Now don't stand around here gassing.",
            "player:Okay, thanks for your help.",
            "end",
        })
        t.exec("touch_paper.in-pack", t.inv.await, "touch_paper", 1, 10)
        t.expect("quest.stage.spoken_chemist", t.quest.expect_stage("spoken_chemist"))
        pass_door("chemist.doorOut", "poshdoor", "poshdooropen", 2932, 3214, 0, 2932, 3213, 2932, 3216,
            function(tt) return tt.z >= 3214 end, "out on the green north of the house, z >= 3214")

        -- [947..989]
        t.ticklog.mark("leg.errand_rimmington")
        t.player.walk_to(2931, 3218, 15)
        t.exec("hops.useVial", t.player.use_on, "sulphuric_broline", (t.player.by_symbol("npc", "drunk1")))
        t.exec("hops-dialog", t.chat.play, {
            "player:Hi, I've got something for you to take to Varrock.",
            "npc:Sounds like pretty thirsty work.",
            "player:Well, there's an Inn in Varrock",
            "npc:Don't worry, I'm a pretty resourceful fellow",
            "options", "choose:You give him the vial of sulphuric broline...",
            "objbox:You give him the vial of sulphuric broline.",
            "player:Okay, I'll see you in Varrock.",
            "npc:Sure, I'm a regular at the Dancing Donkey Inn",
            "end",
        })
        t.player.walk_to(2929, 3221, 15)
        t.exec("talkToChancy", t.player.talk_to, "gambler1", 1)
        t.exec("chancy-dialog", t.chat.play, {
            "player:Hello, I've got a vial for you to take to Varrock.",
            "npc:Tssch... that chemist asks for a lot",
            "player:Maybe you should ask him for more money.",
            "npc:Nah... I just use my initiative",
            "options", "choose:You give him the vial of liquid honey...",
            "objbox:You give him the vial of liquid honey.",
            "npc:Right. I'll meet you at the Dancing Donkey Inn",
            "end",
        })
        t.player.walk_to(2929, 3217, 15)
        t.exec("talkToDaVinci", t.player.talk_to, "artist1", 1)
        t.exec("davinci-dialog", t.chat.play, {
            "player:Hello, I hear you're an errand boy for the chemist.",
            "npc:Well that's my job yes.",
            "player:Good for you. Now can you take a vial",
            "npc:Go on then.",
            "options", "choose:You give him the vial of ethenea...",
            "objbox:You give him the vial of ethenea.",
            "npc:Okay, I'll meet you at the Dancing Donkey in Varrock.",
            "end",
        })
        for _, vial in ipairs({ "ethenea", "liquid_honey", "sulphuric_broline" }) do
            t.exec("errand.given." .. vial, t.inv.expect_absent, vial)
        end

        -- [1055..1066]
        t.ticklog.mark("leg.varrock_gate")
        t.exec("goto-varrockGate", t.player.goto_tile, 3263, 3405, 0)
        t.exec("varrockGate", t.player.click_loc, "guidorgatelclosed", 1, { at = { 3264, 3405, 0 } })
        t.exec("varrockGate-dialog", t.chat.play, {
            "npc:Halt. I need to conduct a search on you.",
        })
        t.ticks(5) -- [1058..1062]: the search, nothing to click
        t.exec("varrockGate-dialog2", t.chat.play, {
            "npc:You may now pass.",
            "end",
        })
        await_tile(function(tt) return tt.x >= 3264 end, 10, "varrock_gate.in")
        check_tile("varrockGate.through", function(tt) return tt.x == 3264 and tt.z == 3405 and tt.level == 0 end,
            "3264,3405, one tile through (4372 [1064])")

        -- [1073..1124]
        t.ticklog.mark("leg.errand_varrock")
        t.player.walk_to(3270, 3388, 40)
        t.exec("talkToChancy2", t.player.talk_to, "gambler2", 1)
        t.exec("chancy2-dialog", t.chat.play, {
            "player:Hi, thanks for doing that.",
            "npc:No problem.",
            "objbox:He gives you the vial of liquid honey.",
            "npc:Next time give me something more valuable",
            "player:That was the idea.",
            "end",
        })
        t.player.walk_to(3272, 3390, 10)
        t.exec("talkToDaVinci2", t.player.talk_to, "artist2", 1)
        t.exec("davinci2-dialog", t.chat.play, {
            "npc:Hello again. I hope your journey",
            "player:Well, as they say, it's always sunny in Gielinor.",
            "npc:Okay, here it is.",
            "objbox:He gives you the vial of ethenea.",
            "player:Thanks, you've been a big help.",
            "end",
        })
        t.player.walk_to(3268, 3390, 10)
        t.exec("talkToHops2", t.player.talk_to, "drunk2", 1)
        t.exec("hops2-dialog", t.chat.play, {
            "player:Hello, how was your journey?",
            "npc:Pretty thirst-inducing actually...",
            "player:Please tell me that you haven't drunk the contents...",
            "npc:Oh the gods no! What do you take me for?",
            "npc:Here's your vial anyway.",
            "objbox:He gives you the vial of sulphuric broline.",
            "player:Thanks, I'll let you get your drink now.",
            "end",
        })
        t.exec("vials.back", t.inv.await_all, { liquid_honey = 1, ethenea = 1, sulphuric_broline = 1 }, 10)

        -- [1130..1185]
        t.ticklog.mark("leg.asyff")
        t.player.walk_to(3279, 3398, 30)
        t.exec("talkToAsyff", t.player.talk_to, "tailorp", 1)
        t.exec("asyff-dialog", t.chat.play, {
            "npc:Now you look like someone who goes to a lot of fancy dress parties.",
            "player:Errr... what are you saying exactly?",
            "npc:I'm just saying that perhaps you would like",
            "npc:Or, if that doesn't interest you",
            "options", "choose:Do you have a spare Priest Gown?",
            "player:Do you have a spare Priest Gown?",
            "npc:Well I do sell them.",
            "player:Please! It's really important.",
            "npc:Well I suppose you can have this old one.",
            "objbox:You are given a Priest Gown.",
            "player:Thank you.",
            "end",
        })
        t.exec("priest_gown.in-pack", t.inv.await, "priest_gown", 1, 10)

        -- [1204..1244]
        t.ticklog.mark("leg.guidor")
        pass_door("guidor.frontDoorIn", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3278, 3382, 0, 3277, 3382, 3280, 3382,
            function(tt) return tt.x >= 3279 and tt.x <= 3282 end, "in Guidor's front room, x 3279-3282")
        t.exec("guidorDoor.julie", t.player.click_loc, "guidordoor", 1, { at = { 3282, 3382, 0 } })
        t.exec("guidorDoor.julie-dialog", t.chat.play, {
            "npc:Please leave my husband alone.",
            "player:I'm sorry to hear that. Is there anything I can do?",
            "npc:Thank you, but I just want him to see a priest.",
            "player:A priest? Hmmm...",
            "end",
        })
        t.exec("equipPriestRobe", t.player.equip, "priest_robe")
        t.exec("equipPriestGown", t.player.equip, "priest_gown")
        cross("guidor.bedroomDoorIn", "guidordoor", 3282, 3382, 0,
            function(tt) return tt.x >= 3283 and tt.level == 0 end, "in Guidor's bedroom, x >= 3283 (priest set worn)")
        t.exec("talkToGuidor", t.player.talk_to, "guidor", 1)
        t.exec("guidor-dialog", t.chat.play, {
            "player:Hello, you must be Guidor.",
            "npc:Is my wife asking priests to visit me now?",
            "player:She made out it was something more serious.",
            "npc:Well it's not killed me yet.",
            "options", "choose:I've come to ask your assistance in stopping a plague.",
            "player:I've come to ask your assistance in stopping a plague.",
            "npc:You mean the plague of West Ardougne?",
            "player:That's the one.",
            "npc:Elena eh?",
            "player:Yes, she wants you to analyse it.",
            "npc:Right then, sounds like we'd better get to work!",
            "player:I have the plague sample.",
            "npc:Now I'll be needing some liquid honey",
            "player:... some ethenea?",
            "npc:Indeed!",
            "npc:Now I'll just apply these to the sample",
            "player:That's why Elena wanted you to do it",
            "npc:Well that's just it, nothing has happened.",
            "player:So what about the plague?",
            "npc:This result can only mean one thing",
            "player:Well this is worrying.",
            "end",
        })
        t.exec("guidor.sampleGone", t.inv.expect_absent, "plaguesample")
        t.expect("quest.stage.found_secret", t.quest.expect_stage("found_secret"))
        cross("guidor.bedroomDoorOut", "guidordoor", 3282, 3382, 0,
            function(tt) return tt.x <= 3282 and tt.level == 0 end, "back in the front room, x <= 3282")

        -- [1318..1339]
        t.ticklog.mark("leg.elena_report")
        t.exec("goto-eastArdougne.report", t.player.goto_tile, 2661, 3301, 0)
        elena_in("elenaReport")
        t.exec("talkToElenaReport", t.player.talk_to, "elena2_vis", 1)
        t.exec("elenaReport-dialog", t.chat.play, {
            "npc:You're back! So what did Guidor say?",
            "player:Nothing.",
            "npc:What?",
            "player:He ran his tests and discovered nothing.",
            "npc:Doesn't exist? But I don't understand... why?",
            "player:I don't know, but I think we're about to uncover",
            "npc:Well I think there's only one thing for it.",
            "player:The king?",
            "npc:Yes. King Lathas of East Ardougne.",
            "player:Well I'd better go and speak to the king then.",
            "npc:Be quick. You should find him in the castle.",
            "end",
        })
        t.expect("quest.stage.reported_elena", t.quest.expect_stage("reported_elena"))
        elena_out("elenaReport")
        local thieving_snapshot_result, thieving_snapshot = t.skill.snapshot()
        t.step("skillSnapshotBeforeReward", thieving_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot() -> " .. tostring(thieving_snapshot_result))

        -- [1377..1437]
        t.ticklog.mark("leg.lathas")
        t.exec("goto-kingLathas", t.player.goto_tile, 2577, 3298, 0)
        pass_door("lathas.castleDoor", "w_ardougnedoubledoorl", "w_ardougnedoubledoorlopen", 2576, 3298, 0, 2577, 3298, 2573, 3298,
            function(tt) return tt.x <= 2575 end, "in the castle hall, x <= 2575")
        climb("lathas.stairs", "stairs", 2571, 3295, 0, 2571, 3298, 1,
            function(tt) return tt.x == 2571 and tt.z == 3294 end, "2571,3294,1 at the stair top")
        pass_door("lathas.kingsDoor", "elfdoor", "elfdooropen", 2575, 3293, 1, 2574, 3293, 2576, 3293,
            function(tt) return tt.x >= 2575 and tt.x <= 2579 and tt.z >= 3292 and tt.z <= 3294 end, "in King Lathas's room")
        t.exec("talkToKingLathas", t.player.talk_to, "kinglathas", 1)
        t.exec("kingLathas-dialog", t.chat.play, {
            "player:I assume that you are King Lathas of East Ardougne?",
            "npc:You assume correctly",
            "player:I get it from finding out that the plague is a hoax.",
            "npc:A hoax? I've never heard such a ridiculous thing",
            "player:I have evidence, from Guidor of Varrock.",
            "npc:Ah... I see.",
            "player:When is it ever good to lie to people like that?",
            "npc:When it protects them from a far greater danger",
            "player:I don't understand...",
            "npc:When my father was king",
            "npc:I became king of East Ardougne",
            "npc:Rather than looking after his people",
            "npc:The Dark Lord agreed to spare his life",
            "player:So what happened?",
            "npc:The chalice corrupted him.",
            "npc:And so I erected the wall",
            "npc:I knew people would never believe the truth",
            "player:I see. Well at least I know now.",
            "npc:While my brother is still at large",
            "player:But how do we stop him?",
            "npc:You're willing to help? Gods be praised!",
            "npc:My brother is currently gathering strength",
            "npc:Return to me when you are ready to face this peril.",
            "npc:I'll also let the mourners know",
        })
        t.quest.expect_complete()
        t.expect("skill.thievingReward", t.skill.expect_gain("thieving", 1250, thieving_snapshot))
        t.exec("mournerkeytw.gone", t.inv.expect_absent, "mournerkeytw")
        -- [1420..1437]: on the scroll's close, the Underground Pass
        t.exec("lathas.afterScroll-dialog", t.chat.play, {
            "npc:Do you need anything else adventurer?",
            "options", "choose:Why don't I get started right away?",
            "player:Why don't I get started right away?",
            "npc:Your enthusiasm is to be commended.",
            "npc:As we've previously discussed",
            "npc:Unfortunately, the pass is a treacherous place.",
            "npc:To make matters worse",
            "player:Well that doesn't sound good.",
            "npc:If my brother is to be stopped",
            "mesbox:Before starting this quest, be aware that",
            "options", "choose:Yes.",
            "player:There's only one way to find out. Where do I start?",
            "npc:You'll find the entrance to the Underground Pass",
            "npc:Together, you are to find a way through the pass.",
            "player:I'll do my best.",
            "npc:Remember, the Underground Pass is lethal.",
            "end",
        })
        local met_result, met = t.var.server("varb9125_upass_lathas_met")
        t.check("upass.started", met_result == "ok" and met == 1, "varb9125_upass_lathas_met -> " .. tostring(met) .. " (" .. tostring(met_result) .. ")")
        t.finish(0)
    end,
}
