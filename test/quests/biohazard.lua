-- Biohazard. Scaffolded by tools/quest_gate/new_quest.py from Quest
-- Helper's helpers/quests/biohazard/, then hand-corrected against the
-- pack's own scripts (OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_biohazard/, areas/area_ardougne_east/scripts/elena.rs2,
-- areas/area_combat_training/configs/combat_training.constant) and its
-- own audit doc (docs/quests/biohazard.md). Tier (quest_inventory.tsv): 1.
--
-- RETRY 2026-09-21 (queue: "the ground spawn in m40_51.spawn is now the
-- full cage 'pigeons' -- rewrite pickupPigeonCage to expect item pigeons
-- and drive on"): confirmed live --
-- areas/world/configs/m40_51.spawn:116-118 now spawns THREE ground rows of
-- item 'pigeons' (all.obj:5609, "It's full of pigeons.", ifop1=Open)
-- behind Jerico's house, not the empty 'pigeoncage' the previous content_bug
-- row (864a752cf) was written against. [opheld1,pigeons]
-- (quest_biohazard_locs.rs2:69-79) is therefore reachable by a real click
-- chain now: pick up the ground 'pigeons' item, press its own op1 while
-- standing in the release zone. The content_bug this file previously ended
-- on is gone -- rewritten below (pickupPigeons/releasePigeons) and the
-- quest driven the rest of the way to completion using every remaining
-- .rs2 in quest_biohazard/, areas/area_ardougne_east/{elena,king_lathas}.rs2,
-- areas/area_ardougne_west/{mourner,doors}.rs2, areas/varrock/east_gate.rs2
-- and quests/quest_eaglepeak/asyff.rs2 (Biohazard's own priest-gown arm),
-- plus docs/quests/biohazard.md section 11's shipped route and its
-- selftest PASS trigger list, and coordinates decoded from
-- quest_biohazard.constant's ^biohazard_west_wall_dest/^east_wall_dest/
-- ^biohazard_crate_coord_a/b plus `questhelper_extract.py biohazard --check`'s
-- WorldPoint table (npc/loc placements have no *.spawn-style text source of
-- their own past the ones grepped below -- trap 20 -- so click_loc/use_on's
-- own three-rule resolve is trusted for the mourner-HQ locs the same way
-- watchtower/cauldron leaves were before).
--
-- The mournerstew2 encounter (Quest Helper step "infiltrateMourners") is a
-- REAL fight, not the doc's "cheat-skip only" debugproc
-- (biohazard_pass_mourner exists purely as a selftest shortcut, per
-- quest_biohazard.rs2:85-91's own comment and docs/quests/biohazard.md
-- section 11's "mournerstew2 has no authored combat block (named
-- cheat-skip only)" -- misleading: mourner.rs2:47-78's [opnpc1,mournerstew2]
-- and its ai_queue3 key-drop queue are real, driven here with
-- player.attack/npc.await_dead_engaged, not the debug cheat); combat gear
-- is a prerequisite (trap 16/26), staged in setup like mortton.lua's shades.
--
-- Fixes to the generator's first guess (kept from the green/content_bug
-- history):
--   * setup's completed prerequisite is quest_plaguecity, not quest_elena
--     -- quest_elena is the quest's own SCRIPT directory name, but the
--     dbrow ::complete needs is quest_plaguecity (all.dbrow:6045; the
--     cheat ladder keys on it at quest_cheat.rs2:947-951, which calls
--     ~quest_elena_set_progress(^elena_complete) and that proc's own
--     stage>=freed_elena branch is what flips %plaguecity_elena_at_home
--     to 1, making elena2_vis the live multinpc leaf at 2592,3336,0).
--   * quest.bind's constants were wrong: the generator copied
--     quest_biohazard.constant's %bioerrand BIT constants (chancy/hops/
--     devinci correct/given/wrong, 1-9) onto %biohazard's own primary
--     ladder. %biohazard's real states 0/1/2/3/4/5/6/7/10/12/14/15/16
--     live in areas/area_combat_training/configs/combat_training.constant
--     (that file's own header explains the odd ownership: "Owned here
--     because the Combat Training Camp gate compiled first").
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge castle courtyard, open). RE-DRIVEN b58 (door rule, owner
-- 2026-10-03): every goto_tile departs from and lands on an open street
-- tile; every door, gate, stair and wall crossing between the street and
-- an npc or loc is clicked on the way in AND on the way out -- Elena's
-- door (three visits), Jerico's door, the nurse's hut door, the Mourner HQ
-- front door, its inner door, spiral stairs, the upstairs door and the
-- key gate, Kilron's rope ladder back over the wall, the chemist's door,
-- the guarded Varrock east gate (guidorgatel: every route into the inn
-- quarter crosses it), Guidor's front door and bedroom door, and Ardougne
-- castle's double door, stairs and King Lathas's door. The goto audit is
-- in build/orchestrator/fix_b58/biohazard.progress.md.
-- RE-DRIVEN b64 (gate_crossings, and the owner's 2026-10-05 ruling that
-- the first goto obeys the door rule): no goto crosses a members' gate.
-- Lumbridge -> Ardougne and Varrock -> Ardougne are real Ardougne
-- Teleports (Plague City's scroll read by click); Ardougne -> Rimmington
-- is walked by choice, through the members' gate south of Taverley
-- (membergatel 2934,3320) by its verb. (OSRS keeps the plague sample
-- across a teleport since 25 July 2019: wiki Biohazard oldid 15256425;
-- docs/quests/biohazard.md:58-64.)
-- Audit: build/orchestrator/fix_b64/biohazard.progress.md.

return {
    id = "biohazard",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- every door and stair is walked now: the quest crosses the map four times on foot between them
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so gasmask fits
        "::biohazardreset",
        "::give gasmask 1", -- required equipment (Quest Helper's getItemRequirements()); worn at Omart, never the quest's own deliverable
        "::complete quest_plaguecity", -- Biohazard's only prerequisite (all.dbrow:6045 quest_biohazard's requirement_quests=36 -> quest_plaguecity)
        -- Combat gear prerequisite for the real mournerstew2 fight
        -- (mourner.rs2:47-78/79-83; recommended combat level 10, a level 13
        -- mourner) -- same idiom as mortton.lua's Loar shades, trap 26.
        "::give rune_scimitar 1",
        "::give lobster 4", -- food for the mournerstew2 fight (eaten by await_dead_engaged's eat opts; the margin row needs food LEFT)
        "::setlevel hitpoints 99",
        "::setlevel attack 70",
        "::setlevel strength 70",
        "::setlevel defence 70",
        -- Two Ardougne Teleports (Lumbridge -> Ardougne at the start, Varrock ->
        -- Ardougne after Guidor): magic_spells.dbrow [magic_spell_teleport_ardougne]
        -- levelrequired 51, waterrune 2 + lawrune 2 each. The spell is gated on
        -- Plague City's scroll being READ (teleport.rs2:16-22); the scroll is
        -- Plague City's own reward (edmond.rs2), staged here and read by click.
        "::setlevel magic 51",
        "::give ardougnescroll 1",
        "::give lawrune 4",
        "::give waterrune 4",
    },

    run = function(t)
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
            display = "Biohazard", -- all.dbrow:466 quest_biohazard's own displayname
            points = 3, -- all.dbrow:485 questpoints=3
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- setup's cheats are server-side; let the client see biohazard=0 before reading it
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Worn before anything else -- Omart's check is on worn state at
        -- the moment of crossing, not on when the mask was donned.
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
        local function elena_in(prefix)
            t.exec("goto-" .. prefix, t.player.goto_tile, 2592, 3340, 0)
            pass_door(prefix .. ".doorIn", "elenadoor2", "elenadoor2open", 2592, 3339, 0, 2592, 3340, 2592, 3337,
                in_elena, "inside Elena's house, x 2588-2594 z 3334-3338")
        end
        local function elena_out(prefix)
            pass_door(prefix .. ".doorOut", "elenadoor2", "elenadoor2open", 2592, 3339, 0, 2592, 3337, 2592, 3340,
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
        local scroll_before_result, scroll_before = t.var.server("varp165_elenaquest")
        t.exec("talkToElena.readArdougneScroll", t.player.inv_op, "ardougnescroll", 1)
        t.exec("talkToElena.readArdougneScroll-dialog", t.chat.play, {
            "mesbox:You memorise what is written on the scroll.",
            "mesbox:You can now cast the Ardougne Teleport spell",
            "end",
        })
        t.ticks(2)
        local scroll_after_result, scroll_after = t.var.server("varp165_elenaquest")
        local scroll_left_result, scroll_left = t.inv.count("ardougnescroll")
        t.check("talkToElena.scrollRead",
            scroll_before_result == "ok" and scroll_before == 29 and scroll_after_result == "ok" and scroll_after == 30
                and scroll_left_result == "ok" and scroll_left == 0,
            "varp165_elenaquest " .. tostring(scroll_before) .. " (" .. tostring(scroll_before_result) .. ") -> " .. tostring(scroll_after)
                .. " (" .. tostring(scroll_after_result) .. "), want 29 = ^elena_complete -> 30 = ^elena_complete_read_scroll; ardougnescroll left "
                .. tostring(scroll_left) .. " (" .. tostring(scroll_left_result) .. "), want 0 (inv_del)")
        -- tele_coord 0_41_51_37_37 = 2661,3301 (East Ardougne market)
        t.player.teleport_cast("ardougne_teleport", { 2661, 3301, 0 }, { name = "talkToElena.ardougneTeleport",
            runes = { { "waterrune", 2 }, { "lawrune", 2 } }, where = "East Ardougne market" })
        elena_in("talkToElena") -- an overland hop on East Ardougne's streets from the teleport landing (reach.py REACH 138, doors shut)
        t.exec("talkToElena", t.player.talk_to, "elena2_vis", 1)
        t.exec("talkToElena-dialog", t.chat.play, {
            "*", -- page 1: player "Good day to you <nc_name(elena2)>." (elena.rs2:96)
            "npc:You too, thanks for freeing me.",
            "npc:It's just a shame the mourners confiscated my equipment.",
            "player:What did they take?",
            "npc:My distillator. I can't test any plague samples without it.",
            "npc:I must somehow retrieve that distillator",
            "choose:I'll try to retrieve it for you.",
            "player:I'll try to retrieve it for you.",
            "npc:I was hoping you would say that.",
            "player:Any ideas?",
            "npc:My father's friend Jerico is in communication with West Ardougne.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started")) -- elena.rs2:117 writes ^biohazard_started on choice 1
        elena_out("talkToElena")

        -- ==== Jerico, next to the chapel (2612,3324,0; [opnpc1,jerico]) ====
        -- His house is walled in, x 2611-2617 z 3322-3326, door poordoor on
        -- the east edge of 2610,3324 (maps/m40_51.jl2; reach.py: the door
        -- tile itself is the street side). The cupboard is in the same room,
        -- so the bird feed is fetched on this one visit.
        local function in_jerico(tt) return tt.x >= 2611 and tt.x <= 2617 and tt.z >= 3322 and tt.z <= 3326 end
        t.exec("goto-talkToJerico", t.player.goto_tile, 2609, 3324, 0)
        pass_door("talkToJerico.doorIn", "poordoor", "poordooropen", 2610, 3324, 0, 2609, 3324, 2611, 3325,
            in_jerico, "inside Jerico's house, x 2611-2617 z 3322-3326")
        -- switch_int(%biohazard), case ^biohazard_started: every line here
        -- is literal (no interpolation), spelled verbatim from jerico.rs2:44-51.
        t.exec("talkToJerico", t.player.talk_to, "jerico", 1)
        t.exec("talkToJerico-dialog", t.chat.play, {
            "player:Hello Jerico.",
            "npc:Hello, I've been expecting you.",
            "player:That's right.",
            "npc:My messenger pigeons help me communicate with friends over the wall.",
            "npc:I have arranged for two friends",
            "npc:But be careful, if the mourners catch you the punishment will be severe.",
            "player:Thanks Jerico.",
        })
        t.expect("quest.stage.spoken_jerico", t.quest.expect_stage("spoken_jerico")) -- jerico.rs2:48

        -- Get bird feed from Jerico's cupboard (2611,3326,0): op1 opens it
        -- (jericoscupboardshut->jericoscupboardopen, 500), op1 again
        -- searches the open form and grants birdfeed (quest_biohazard_locs.rs2:20-47)
        -- now that %biohazard >= ^biohazard_spoken_jerico.
        t.exec("openJericoCupboard", t.player.click_loc, "jericoscupboardshut", 1)
        t.ticks(3) -- let the loc_change land before the second click sees the open form (QUEST_AUTHORING.md section 8)
        t.exec("searchJericoCupboard", t.player.click_loc, "jericoscupboardopen", 1)
        -- The search opens a real ~mesbox (measured run 1: inv_add never
        -- fired, birdfeed stayed absent) that gates the rest of the label --
        -- quest_biohazard_locs.rs2:29-47 runs its guard chain and the grant
        -- only after this page and the follow-up player line are dismissed.
        t.exec("searchJericoCupboard-dialog", t.chat.play, {
            "mesbox:The cupboard is full of birdfeed.",
            "player:Mmm, birdfeed!",
        })
        -- Poll, not a bare read: the grant's inv_add lands a tick or two
        -- behind the dialogue's own last page (QUEST_AUTHORING.md section 8).
        local birdfeed_await_result = t.inv.await("birdfeed", 1, 10)
        local birdfeed_count_result, birdfeed_count = t.inv.count("birdfeed")
        t.step("expectBirdfeed",
            (birdfeed_await_result == "ok" and birdfeed_count_result == "ok" and (birdfeed_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(birdfeed,1,10) -> %s; inv.count(birdfeed) -> %s (%s)",
                tostring(birdfeed_await_result), tostring(birdfeed_count_result), tostring(birdfeed_count)))
        pass_door("getBirdFeed.doorOut", "poordoor", "poordooropen", 2610, 3324, 0, 2611, 3324, 2608, 3324,
            function(tt) return tt.x <= 2610 end, "out on the street west of the house, x <= 2610")

        -- ==== The watchtower (biowatchtower walls 2559-2563,3301-3304) ====
        -- 2562,3300 is the open tile in front of its south wall, inside the
        -- pigeon release zone (inzone(0_39_51_63_35, 0_40_51_5_43) = worldX
        -- 2559-2565, worldZ 3299-3307). Investigate it (leaf
        -- biowatchtower_op per the multiloc resolve, trap 20; the wrapper
        -- "biowatchtower" is never the click target). It shows the
        -- mourner exchange when mournerwatchtower is within 5
        -- (quest_biohazard_locs.rs2:51-56) -- MEASURED b58 run 1: he is, and
        -- the page left open swallowed the bird-feed use's stage write, so
        -- the exchange is played through.
        t.exec("goto-investigateWatchtower", t.player.goto_tile, 2562, 3300, 0)
        t.exec("investigateWatchtower", t.player.click_loc, "biowatchtower_op", 1)
        t.exec("investigateWatchtower-dialog", t.chat.play, {
            "npc:Keep away civilian.",
            "player:What's it to you?",
            "npc:This tower's here for your protection.",
            "end",
        })

        -- Use the bird feed on the watchtower leaf (oplocu,biowatchtower_op,
        -- quest_biohazard_locs.rs2:58-66): needs %biohazard = spoken_jerico
        -- and last_useitem = birdfeed, consumes it, writes ^biohazard_used_birdfeed.
        -- use_on walks into range itself (no goto onto the loc).
        t.exec("useBirdfeedOnWatchtower", t.player.use_on, "birdfeed", (t.player.by_symbol("loc", "biowatchtower_op")))
        -- the stage write sits after the label's p_delay(2) (quest_biohazard_locs.rs2:59-63)
        local feed_settle_result, feed_settle_detail = t.await({
            level = function()
                local stage_result, stage_value = t.quest.stage()
                return stage_result == "ok" and stage_value == 3
            end,
            note = "watchtower.birdfeed_settle",
        }, 10)
        t.step("useBirdfeedOnWatchtower.settle", feed_settle_result == "ok" and "PASS" or "FAIL",
            "await(quest.stage()==used_birdfeed) after the label's p_delay(2) -> " .. tostring(feed_settle_result) .. " " .. tostring(feed_settle_detail))
        local birdfeed_left_result, birdfeed_left = t.inv.count("birdfeed")
        t.check("birdfeed.consumed", birdfeed_left_result == "ok" and birdfeed_left == 0,
            string.format("inv.count(birdfeed) -> %s (%s), expected 0 -- consumed by [oplocu,biowatchtower_op]",
                tostring(birdfeed_left_result), tostring(birdfeed_left)))
        t.expect("quest.stage.used_birdfeed", t.quest.expect_stage("used_birdfeed"))

        -- Pick up the pigeons -- a ground OBJ behind Jerico's house, outside
        -- its walls (areas/world/configs/m40_51.spawn:116-118: item
        -- 'pigeons', all.obj:5609, ifop1=Open), never handed over in
        -- dialogue. [opheld1,pigeons] (quest_biohazard_locs.rs2:69-79) is
        -- the trigger that both releases them AND is the only writer of
        -- ^biohazard_released_pigeons. click_obj answers `ok` with a nil
        -- detail (trap 12's hollow shape), so called directly with the
        -- before/after count as the real detail.
        t.exec("goto-pigeons", t.player.goto_tile, 2618, 3324, 0)
        local pigeons_before_result, pigeons_before = t.inv.count("pigeons")
        local pigeons_pickup_result, pigeons_pickup_detail = t.player.click_obj("pigeons")
        local pigeons_after_result, pigeons_after = t.inv.count("pigeons")
        t.step("pickupPigeons",
            (pigeons_before_result == "ok" and pigeons_pickup_result == "ok" and pigeons_after_result == "ok" and (pigeons_after or 0) > (pigeons_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj(pigeons) -> %s (%s); pigeons %s -> %s",
                tostring(pigeons_pickup_result), tostring(pigeons_pickup_detail), tostring(pigeons_before), tostring(pigeons_after)))

        -- Release them in the authored zone by pressing the held item's own
        -- op1 -- [opheld1,pigeons] itself, not a second click on the
        -- watchtower.
        t.exec("goto-releasePigeons", t.player.goto_tile, 2562, 3300, 0)
        t.exec("releasePigeons", t.player.inv_op, "pigeons", 1)
        t.ticks(2)
        local pigeoncage_result, pigeoncage_count = t.inv.count("pigeoncage")
        t.check("pigeoncage.returned", pigeoncage_result == "ok" and (pigeoncage_count or 0) >= 1,
            string.format("inv.count(pigeoncage) -> %s (%s), expected >=1 -- [opheld1,pigeons] hands back the empty cage",
                tostring(pigeoncage_result), tostring(pigeoncage_count)))
        t.expect("quest.stage.released_pigeons", t.quest.expect_stage("released_pigeons"))

        -- ==== Omart, the wall crossing (2559,3266,0; [opnpc1,omart]) ====
        -- At ^biohazard_released_pigeons, choosing to cross (gasmask worn,
        -- already equipped above) runs @biohazard_climb_ladder itself
        -- (quest_biohazard_locs.rs2:83-97): the rope ladder is Omart's, the
        -- teleport lands on ^biohazard_west_wall_dest 0_39_51_58_3 =
        -- 2554,3267,0 (quest_biohazard.constant:17).
        t.exec("goto-talkToOmart", t.player.goto_tile, 2559, 3266, 0)
        t.exec("talkToOmart", t.player.talk_to, "omart", 1)
        t.exec("talkToOmart-dialog", t.chat.play, {
            "npc:Well done, the guards are having real trouble",
            "options",
            "choose:Okay, let's do it.",
            "player:Okay, let's do it.",
            "end",
        })
        await_tile(function(tt) return tt.x == 2554 and tt.z == 3267 end, 10, "omart.ladder")
        check_tile("talkToOmart.overTheWall", function(tt) return tt.x == 2554 and tt.z == 3267 and tt.level == 0 end,
            "^biohazard_west_wall_dest 2554,3267,0 in West Ardougne")
        t.expect("quest.stage.climbed_ladder", t.quest.expect_stage("climbed_ladder"))

        -- Guide step enterBackyardOfHeadquarters ("Squeeze through the fence
        -- to enter the Mourner's Headquarters yard"): mournerstewfence at
        -- 2541,3331,0 (OSRS-Content/osrs239-content/maps/m39_52.jl2:2269).
        -- [oploc1,mournerstewfence] (general_use/scripts/fence.rs2:5-32) is
        -- unconditional -- an agility_exactmove squeeze-through with no chat
        -- and no mes(). Approach from the street west of the fence.
        t.exec("goto-mournerFenceApproach", t.player.goto_tile, 2538, 3331, 0)
        t.exec("enterBackyardOfHeadquarters", t.player.click_loc, "mournerstewfence", 1)
        -- MEASURED run 2: a bare world.tile() read right after the click
        -- caught the squeeze mid-animation; poll instead.
        local fence_settle_result, fence_settle_detail = await_tile(function(tt) return tt.x > 2541 end, 10, "fence.crossed_settle")
        t.step("enterBackyardOfHeadquarters.crossed", fence_settle_result == "ok" and "PASS" or "FAIL",
            "await(world.tile().x > 2541) after the fence squeeze -> "
                .. tostring(fence_settle_result) .. " " .. tostring(fence_settle_detail))

        -- Get a rotten apple off the ground in the yard
        -- (areas/world/configs/m39_52.spawn:56, 2549,3332,0) -- a walk inside
        -- the yard -- and poison the mourners' stew with it
        -- (oplocu,mournercauldron_op, quest_biohazard_locs.rs2:103-121; the
        -- visible leaf, trap 20). use_on walks to the cauldron itself.
        t.player.walk_to(2549, 3332, 20)
        local apple_before_result, apple_before = t.inv.count("rottenapples")
        local apple_pickup_result, apple_pickup_detail = t.player.click_obj("rottenapples")
        local apple_after_result, apple_after = t.inv.count("rottenapples")
        t.step("takeRottenApple",
            (apple_before_result == "ok" and apple_pickup_result == "ok" and apple_after_result == "ok" and (apple_after or 0) > (apple_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj(rottenapples) -> %s (%s); rottenapples %s -> %s",
                tostring(apple_pickup_result), tostring(apple_pickup_detail), tostring(apple_before), tostring(apple_after)))

        t.exec("poisonCauldron", t.player.use_on, "rottenapples", (t.player.by_symbol("loc", "mournercauldron_op")))
        -- MEASURED run 2: the success branch's own %biohazard write sits
        -- AFTER an internal p_delay(3) (quest_biohazard_locs.rs2:115-118), so
        -- poll for the real transition instead of a fixed wait.
        local poison_settle_result, poison_settle_detail = t.await({
            level = function()
                local stage_result, stage_value = t.quest.stage()
                return stage_result == "ok" and stage_value == 6
            end,
            note = "cauldron.poisoned_settle",
        }, 15)
        t.step("cauldron.poisonedSettle", poison_settle_result == "ok" and "PASS" or "FAIL",
            "await(quest.stage()==poisoned_stew) after use_on's internal p_delay(3) -> "
                .. tostring(poison_settle_result) .. " " .. tostring(poison_settle_detail))
        local apple_left_result, apple_left = t.inv.count("rottenapples")
        t.check("rottenApple.consumed", apple_left_result == "ok" and apple_left == 0,
            string.format("inv.count(rottenapples) -> %s (%s), expected 0 -- inv_del in [oplocu,mournercauldron_op] (quest_biohazard_locs.rs2:115)",
                tostring(apple_left_result), tostring(apple_left)))
        t.expect("quest.stage.poisoned_stew", t.quest.expect_stage("poisoned_stew"))

        -- Guide step exitBackyardOfHeadquarters: the yard is left the way
        -- it was entered, through the same fence; fence.rs2 swaps
        -- $start/$end when ~check_axis says the player is on the loc's far
        -- side, so the squeeze runs east-to-west and ends on the fence's
        -- own loc_coord, x = 2541.
        t.exec("exitBackyardOfHeadquarters", t.player.click_loc, "mournerstewfence", 1)
        local fence_exit_result, fence_exit_detail = await_tile(function(tt) return tt.x <= 2541 end, 10, "fence.exited_settle")
        -- MEASURED seam31 run 1: the client tile reads x <= 2541 while the
        -- exactmove is still in flight, and fence.rs2's p_delay(2) +
        -- p_teleport($end) then lands after the next move; let the script
        -- finish, then read where it left the player.
        t.ticks(5)
        local fence_rest_result, fence_rest = t.world.tile()
        t.step("exitBackyardOfHeadquarters.crossed",
            (fence_exit_result == "ok" and fence_rest_result == "ok" and fence_rest.x == 2541 and fence_rest.z == 3331) and "PASS" or "FAIL",
            "await(world.tile().x <= 2541) after the squeeze back out -> "
                .. tostring(fence_exit_result) .. " " .. tostring(fence_exit_detail)
                .. "; 5 ticks later at " .. tile_text(fence_rest_result, fence_rest) .. " (fence loc_coord 2541,3331, the west side)")
        t.player.walk_to(2538, 3331, 10)
        check_tile("exitBackyardOfHeadquarters.street", function(tt) return tt.x <= 2540 and tt.level == 0 end,
            "on the street west of the fence, x <= 2540")

        -- ==== The nurse's hut (Sarah's cupboard) ====
        -- Walled in, x 2515-2518 z 3270-3276, door poordoor on the west edge
        -- of 2519,3275 (maps/m39_51.jl2; the door tile is the street side).
        -- The cupboard (2517,3276) only fills once %biohazard >=
        -- ^biohazard_poisoned_stew (quest_biohazard_locs.rs2:123-142).
        local function in_hut(tt) return tt.x >= 2515 and tt.x <= 2518 and tt.z >= 3270 and tt.z <= 3276 end
        t.exec("goto-nurseHut", t.player.goto_tile, 2521, 3275, 0)
        pass_door("nurseHut.doorIn", "poordoor", "poordooropen", 2519, 3275, 0, 2520, 3275, 2517, 3275,
            in_hut, "inside the nurse's hut, x 2515-2518 z 3270-3276")
        t.exec("openNurseCupboard", t.player.click_loc, "bionursescupboardshut", 1)
        t.ticks(3)
        t.exec("searchNurseCupboard", t.player.click_loc, "bionursescupboardopen", 1)
        local gown_await_result = t.inv.await("doctor_gown", 1, 10)
        local gown_count_result, gown_count = t.inv.count("doctor_gown")
        t.step("expectDoctorGown",
            (gown_await_result == "ok" and gown_count_result == "ok" and (gown_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(doctor_gown,1,10) -> %s; inv.count(doctor_gown) -> %s (%s)",
                tostring(gown_await_result), tostring(gown_count_result), tostring(gown_count)))
        t.exec("equipDoctorGown", t.player.equip, "doctor_gown")
        pass_door("nurseHut.doorOut", "poordoor", "poordooropen", 2519, 3275, 0, 2518, 3275, 2521, 3275,
            function(tt) return tt.x >= 2519 end, "out on the street east of the hut, x >= 2519")

        -- ==== Mourner Headquarters (maps/m39_51.jl2) ====
        -- Ground floor: front door mournerstewdoor on the north edge of
        -- 2551,3320 (the door tile is the street); inside, poordoor on the
        -- east edge of 2546,3325 leads west to spiralstairs (2542-2543,
        -- 3324-3325, no maplink row: +1 plane on the stand tile). Upstairs:
        -- mournerstewdoorup on the west edge of 2547,3325,1 (gown or refused,
        -- doors.rs2:12-17) into mournerstew2's room, and the key gate
        -- mournerquaters_gatel on the east edge of 2551,3326,1 into the
        -- crate room x 2552-2554 z 3325-3327.
        --
        -- The front door, at %biohazard=poisoned_stew|found_distillator with
        -- the gown worn (doors.rs2:19-24 -> 72-91): the guard binds
        -- mourner_armed_guard and opens a real chatnpc page ("In you go
        -- doc."), then if_close + ~west_ardy_walk_door.
        t.exec("goto-mournerHqDoor", t.player.goto_tile, 2551, 3318, 0)
        t.exec("enterMournerHeadquarters", t.player.click_loc, "mournerstewdoor", 1, { at = { 2551, 3320, 0 } })
        t.exec("mournerHqDoor-dialog", t.chat.play, {
            "npc:In you go doc.",
            "end",
        })
        await_tile(function(tt) return tt.z >= 3321 end, 10, "mournerHq.frontDoor")
        check_tile("enterMournerHeadquarters.crossed", function(tt) return tt.z >= 3321 and tt.z <= 3327 and tt.level == 0 end,
            "inside the HQ past the front door, z 3321-3327 level 0")

        pass_door("goUpstairsInMournerBuilding.innerDoor", "poordoor", "poordooropen", 2546, 3325, 0, 2547, 3325, 2545, 3325,
            function(tt) return tt.x <= 2546 end, "in the stair room, x <= 2546")
        climb("goUpstairsInMournerBuilding", "spiralstairs", 2542, 3324, 0, 2544, 3325, 1,
            function(tt) return tt.x >= 2543 and tt.x <= 2546 and tt.z >= 3323 and tt.z <= 3327 end, "the stair landing west of the upstairs door")
        cross("goUpstairsInMournerBuilding.doorUp", "mournerstewdoorup", 2547, 3325, 1,
            function(tt) return tt.x >= 2547 and tt.level == 1 end, "in mournerstew2's room, x >= 2547 level 1 (gown worn)")

        -- The sick mourner (mournerstew2, m39_51.spawn:83, 2551,3327,1).
        -- Any of the three answers ends in ~npc_retaliate(0) (mourner.rs2:
        -- 47-73); the shortest branch (option 1) is driven here.
        t.exec("equipRuneScimitar", t.player.equip, "rune_scimitar")
        t.exec("talkToMournerstew2", t.player.talk_to, "mournerstew2", 1)
        t.exec("mournerstew2-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:You're here at last!",
            "player:Hmm... interesting, sounds like food poisoning.",
            "npc:Yes, I'd figured that out already.",
            "options",
            "choose:Just hold your breath and count to ten.",
            "player:Just hold your breath and count to ten.",
            "npc:What? How will that help?",
            "player:Erm... I'm new, I just started.",
            "npc:You're no doctor!",
            "end",
        })
        -- The fight. t.player.attack presses the Attack row and stamps the
        -- slot await_dead_engaged follows; MEASURED runs 3/5/6 its own
        -- hit-settle read `timeout` while the same fight was won a few
        -- ticks later, so the press is not graded on its own: the row is
        -- the kill (await_dead_engaged ok), with the press's answer in the
        -- detail. Lobsters are eaten below 50 hp.
        local food_before_result, food_before = t.inv.count("lobster")
        local attack_result, attack_detail = t.player.attack("mournerstew2", 2, 50)
        local dead_result, dead_detail = t.npc.await_dead_engaged(90, 6, { eat = { item = "lobster", below = 50 } })
        t.check("killMourner",
            (attack_result == "ok" or attack_result == "timeout") and dead_result == "ok",
            "attack(mournerstew2,2,50) -> " .. tostring(attack_result) .. " " .. tostring(attack_detail)
                .. "; await_dead_engaged(90) -> " .. tostring(dead_result) .. " " .. tostring(dead_detail))
        t.expect("player.aliveAfterMourner", t.player.alive())
        local fight_lowest = tonumber(string.match(tostring(dead_detail), "lowest hp (%d+)/"))
        local food_after_result, food_after = t.inv.count("lobster")
        t.check("killMourner.margin",
            fight_lowest ~= nil and fight_lowest >= 25 and food_after_result == "ok" and (food_after or 0) >= 1,
            "lowest hp " .. tostring(fight_lowest) .. "/99 (await_dead_engaged's eat reading), lobsters staged 4, before the fight "
                .. tostring(food_before) .. " (" .. tostring(food_before_result) .. "), left " .. tostring(food_after)
                .. " (" .. tostring(food_after_result) .. ") (margin: lowest hp >= 25 = a quarter of 99 AND food left)")

        -- ai_queue3,mournerstew2 (mourner.rs2:79-97) fires
        -- defeat_biohazard_mourner once its hero-npc queue settles and
        -- grants the key (~biohazard_give(mournerkeytw)).
        local key_await_result = t.inv.await("mournerkeytw", 1, 15)
        local key_count_result, key_count = t.inv.count("mournerkeytw")
        t.step("expectMournerKey",
            (key_await_result == "ok" and key_count_result == "ok" and (key_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(mournerkeytw,1,15) -> %s; inv.count(mournerkeytw) -> %s (%s)",
                tostring(key_await_result), tostring(key_count_result), tostring(key_count)))

        -- The key gate (quest_biohazard_locs.rs2:144-171): entering from
        -- mournerstew2's room with mournerkeytw carried walks through;
        -- without it "The gate is locked."
        cross("searchCrateForDistillator.gateIn", "mournerquaters_gatel", 2551, 3326, 1,
            function(tt) return tt.x >= 2552 and tt.x <= 2554 and tt.level == 1 end, "in the crate room, x 2552-2554 level 1 (key carried)")
        -- The third-from-left crate (^biohazard_crate_coord_a = 2554,3327,1,
        -- quest_biohazard.constant) holds Elena's distillator
        -- (quest_biohazard_locs.rs2:249-281); still poisoned_stew, gown worn.
        t.exec("searchCrateForDistillator", t.player.click_loc, "mournercrateup", 1, { at = { 2554, 3327, 1 } })
        local distillator_await_result = t.inv.await("distillator", 1, 10)
        local distillator_count_result, distillator_count = t.inv.count("distillator")
        t.step("expectDistillator",
            (distillator_await_result == "ok" and distillator_count_result == "ok" and (distillator_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(distillator,1,10) -> %s; inv.count(distillator) -> %s (%s)",
                tostring(distillator_await_result), tostring(distillator_count_result), tostring(distillator_count)))
        t.expect("quest.stage.found_distillator", t.quest.expect_stage("found_distillator"))

        -- Out the way in: gate, upstairs door, stairs down, inner door,
        -- front door (from inside ~check_axis is false: a plain walk-through,
        -- doors.rs2:19-21).
        cross("goBackDownstairsInMournersHeadquarters.gateOut", "mournerquaters_gatel", 2551, 3326, 1,
            function(tt) return tt.x <= 2551 and tt.x >= 2547 and tt.level == 1 end, "back in mournerstew2's room, x 2547-2551 level 1")
        cross("goBackDownstairsInMournersHeadquarters.doorUp", "mournerstewdoorup", 2547, 3325, 1,
            function(tt) return tt.x <= 2546 and tt.level == 1 end, "on the stair landing, x <= 2546 level 1")
        local down_result, down_detail = t.player.click_loc("spiralstairstop", 1, { at = { 2543, 3325, 1 } })
        await_tile(function(tt) return tt.level == 0 end, 10, "mournerHq.stairsDown")
        local down_tile_result, down_tile = t.world.tile()
        t.check("goBackDownstairsInMournersHeadquarters",
            (down_result == "ok" or down_result == "timeout") and down_tile_result == "ok" and down_tile.level == 0
                and down_tile.x >= 2542 and down_tile.x <= 2546 and down_tile.z >= 3323 and down_tile.z <= 3327,
            "click_loc(spiralstairstop at 2543,3325,1) -> " .. tostring(down_result) .. " " .. tostring(down_detail)
                .. "; landed " .. tile_text(down_tile_result, down_tile) .. " (want level 0 in the stair room, x 2542-2546)")
        pass_door("goBackDownstairsInMournersHeadquarters.innerDoor", "poordoor", "poordooropen", 2546, 3325, 0, 2545, 3325, 2548, 3324,
            function(tt) return tt.x >= 2547 end, "back in the HQ hall, x >= 2547")
        t.player.walk_to(2551, 3321, 20)
        cross("leaveMournerHeadquarters", "mournerstewdoor", 2551, 3320, 0,
            function(tt) return tt.z <= 3320 and tt.level == 0 end, "on the street south of the HQ, z <= 3320")

        -- ==== Kilron, back over the wall (2556,3266,0; [opnpc1,kilron]) ====
        -- quest_biohazard_locs.rs2:225-241: option 1 throws his rope ladder
        -- and teleports to ^biohazard_east_wall_dest 0_39_51_65_3 =
        -- 2561,3267,0 (quest_biohazard.constant:18).
        t.exec("goto-talkToKilron", t.player.goto_tile, 2555, 3268, 0)
        t.exec("talkToKilron", t.player.talk_to, "kilron", 1)
        t.exec("talkToKilron-dialog", t.chat.play, {
            "npc:Welcome to West Ardougne friend.",
            "options",
            "choose:Yes, take me back over the wall.",
            "player:Yes, take me back over the wall.",
            "end",
        })
        await_tile(function(tt) return tt.x == 2561 and tt.z == 3267 end, 10, "kilron.ladder")
        check_tile("talkToKilron.overTheWall", function(tt) return tt.x == 2561 and tt.z == 3267 and tt.level == 0 end,
            "^biohazard_east_wall_dest 2561,3267,0 in East Ardougne")

        -- ==== Back to Elena with the distillator ====
        -- elena.rs2:60-94, case ^biohazard_found_distillator: hands over
        -- the distillator for three vials + a sample. TWO if_close/reopen
        -- boundaries sit in this one branch (line 73 and, MEASURED run 3,
        -- a second one at line 78), so three chat.play lists with two
        -- awaits between them (QUEST_AUTHORING.md section 8's payout-reopen
        -- trap).
        elena_in("talkToElenaWithDistillator")
        t.exec("talkToElenaDistillator", t.player.talk_to, "elena2_vis", 1)
        t.exec("elenaDistillator-dialog-1", t.chat.play, {
            "npc:So, have you managed to retrieve my distillator?",
            "player:Yes, here it is!",
            "npc:You have? That's great!",
            "player:Those look pretty fancy.",
            "npc:Well, yes and no.",
            "player:You're not kidding, I can smell it from here!",
            "end",
        })
        local elena_reopen_result, elena_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "elena.distillator_reopen",
        }, 10)
        t.step("elena.distillatorReopen", elena_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after if_close+mes+p_delay(2) -> "
                .. tostring(elena_reopen_result) .. " " .. tostring(elena_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("elenaDistillator-dialog-2", t.chat.play, {
            "npc:I don't understand... the touch paper hasn't changed colour",
            "npc:You'll need to go and see my old mentor Guidor.",
            "end",
        })
        local elena_reopen2_result, elena_reopen2_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "elena.distillator_reopen_2",
        }, 10)
        t.step("elena.distillatorReopen2", elena_reopen2_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after a SECOND if_close+inv grant+p_delay(2) -> "
                .. tostring(elena_reopen2_result) .. " " .. tostring(elena_reopen2_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("elenaDistillator-dialog-3", t.chat.play, {
            "npc:But first you'll need some more touch paper.",
            "npc:Just don't get into any fights",
            "npc:Those vials are fragile",
            "end",
        })
        local vials_await_result = t.inv.await_all({
            liquid_honey = 1,
            ethenea = 1,
            sulphuric_broline = 1,
            plaguesample = 1,
        }, 10)
        t.step("expectVialsAndSample", vials_await_result == "ok" and "PASS" or "FAIL",
            "inv.await_all({liquid_honey=1,ethenea=1,sulphuric_broline=1,plaguesample=1},10) -> " .. tostring(vials_await_result))
        local distillator_left_result, distillator_left = t.inv.count("distillator")
        t.check("distillator.handedOver", distillator_left_result == "ok" and distillator_left == 0,
            string.format("inv.count(distillator) -> %s (%s), expected 0 -- Elena takes it (elena.rs2:60-94)",
                tostring(distillator_left_result), tostring(distillator_left)))
        t.expect("quest.stage.given_distillator", t.quest.expect_stage("given_distillator"))
        elena_out("talkToElenaWithDistillator")

        -- ==== The Chemist, Rimmington (2934,3210,0; [opnpc1,chemist]) ====
        -- His house is walled in, x 2929-2939 z 3207-3213 (maps/m45_50.jl2);
        -- the north door poshdoor on the south edge of 2932,3214 faces the
        -- errand boys. case ^biohazard_given_distillator: choosing "This
        -- can't wait, I'm carrying a plague sample." runs straight into
        -- @chemist_touchpaperguidor, granting touch_paper and writing
        -- ^biohazard_spoken_chemist (chemist.rs2:39-48,84-97).
        --
        -- Ardougne to Rimmington is WALKED by choice, not forced: OSRS keeps
        -- the plague sample across a teleport since 25 July 2019 (wiki
        -- Biohazard oldid 15256425; docs/quests/biohazard.md:58-64; the old
        -- LostCity "it disintegrates in the crossing" rule is pre-2019 and
        -- this pack's teleport.rs2 does not carry it). reach.py 2592,3340 -> 2932,3215 at margin
        -- 300: NEEDS-DOOR via membergatel@2934,3320 -- the members' gate
        -- south of Taverley is the way on foot. Overland to its north side
        -- (REACH 774 at 300, doors shut), the gate pressed north to south,
        -- then overland to the chemist's door (REACH 125).
        t.exec("goto-chemist.memberGate", t.player.goto_tile, 2934, 3322, 0)
        t.exec("talkToTheChemist.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3321 }, far_ok = function(tile) return tile.z <= 3319 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "south of the members' gate, z <= 3319", far = { 2934, 3318 } })
        local function in_chemist(tt) return tt.x >= 2929 and tt.x <= 2939 and tt.z >= 3207 and tt.z <= 3213 end
        t.exec("goto-chemist", t.player.goto_tile, 2932, 3215, 0)
        pass_door("talkToTheChemist.doorIn", "poshdoor", "poshdooropen", 2932, 3214, 0, 2932, 3215, 2932, 3212,
            in_chemist, "inside the chemist's house, x 2929-2939 z 3207-3213")
        t.exec("talkToChemist", t.player.talk_to, "chemist", 1)
        t.exec("chemist-dialog", t.chat.play, {
            "npc:Sorry, I'm afraid we're just closing now.",
            "options",
            "choose:This can't wait, I'm carrying a plague sample.",
            "player:This can't wait, I'm carrying a plague sample",
            "npc:A plague sample? Keep it sealed.",
            "player:Who knows... I just need some touch paper for a guy called Guidor.",
            "npc:Guidor? This one's on me then",
            "npc:It's just that there've been rumours",
            "npc:They're even doing spot checks in Varrock.",
            "player:Oh right... so am I going to be ok carrying these three vials",
            "npc:With touch paper as well?",
            "npc:They're not the most reliable people in the world.",
            "npc:It's better than entering Varrock",
            "player:Ok, thanks for your help.",
            "npc:Yes well don't stand around here gassing.",
            "end",
        })
        local touchpaper_await_result = t.inv.await("touch_paper", 1, 10)
        local touchpaper_count_result, touchpaper_count = t.inv.count("touch_paper")
        t.step("expectTouchPaper",
            (touchpaper_await_result == "ok" and touchpaper_count_result == "ok" and (touchpaper_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(touch_paper,1,10) -> %s; inv.count(touch_paper) -> %s (%s)",
                tostring(touchpaper_await_result), tostring(touchpaper_count_result), tostring(touchpaper_count)))
        t.expect("quest.stage.spoken_chemist", t.quest.expect_stage("spoken_chemist"))
        pass_door("talkToTheChemist.doorOut", "poshdoor", "poshdooropen", 2932, 3214, 0, 2932, 3213, 2932, 3216,
            function(tt) return tt.z >= 3214 end, "out on the green north of the house, z >= 3214")

        -- ==== Errand boys, Rimmington: hand each the CORRECT vial ====
        -- (errand_boys.rs2). drunk1/gambler1/artist1 stand on the open green
        -- a few tiles north of the chemist's door; walked, not teleported.
        t.player.walk_to(2930, 3218, 15)
        t.exec("talkToDrunk1", t.player.talk_to, "drunk1", 1)
        -- MEASURED run 3: choosing a vial does NOT echo the choice text
        -- back as its own player page.
        t.exec("drunk1-dialog", t.chat.play, {
            "player:Hi, I've got something for you to take to Varrock.",
            "npc:Sounds like pretty thirsty work.",
            "player:Well, there's an Inn in Varrock",
            "npc:Don't worry, I'm a pretty resourceful fellow",
            "options",
            "choose:You give him the vial of sulphuric broline...",
            "player:Ok, I'll see you in Varrock.",
            "npc:Sure, I'm a regular at the Dancing Donkey Inn",
            "end",
        })
        local drunk1_sulphuric_result, drunk1_sulphuric = t.inv.count("sulphuric_broline")
        t.check("drunk1.gaveCorrectVial", drunk1_sulphuric_result == "ok" and drunk1_sulphuric == 0,
            string.format("inv.count(sulphuric_broline) -> %s (%s), expected 0 -- hops_sulphuricbroline sets hops_correct",
                tostring(drunk1_sulphuric_result), tostring(drunk1_sulphuric)))

        t.player.walk_to(2929, 3221, 15)
        t.exec("talkToGambler1", t.player.talk_to, "gambler1", 1)
        t.exec("gambler1-dialog", t.chat.play, {
            "player:Hello, I've got a vial for you to take to Varrock.",
            "npc:Tssch... that chemist asks for a lot",
            "player:Maybe you should ask him for more money.",
            "npc:Nah... I just use my initiative",
            "options",
            "choose:You give him the vial of liquid honey...",
            "player:Right. I'll see you later in the Dancing Donkey Inn.",
            "npc:Be lucky!",
            "end",
        })
        local gambler1_honey_result, gambler1_honey = t.inv.count("liquid_honey")
        t.check("gambler1.gaveCorrectVial", gambler1_honey_result == "ok" and gambler1_honey == 0,
            string.format("inv.count(liquid_honey) -> %s (%s), expected 0 -- chancy_liquidhoney sets chancy_correct",
                tostring(gambler1_honey_result), tostring(gambler1_honey)))

        t.player.walk_to(2927, 3218, 15)
        t.exec("talkToArtist1", t.player.talk_to, "artist1", 1)
        t.exec("artist1-dialog", t.chat.play, {
            "player:Hello, I hear you're an errand boy for the chemist.",
            "npc:Well that's my job yes.",
            "player:Good for you.",
            "npc:Go on then.",
            "options",
            "choose:You give him the vial of ethenea...",
            "player:Ok, we're meeting at the Dancing Donkey in Varrock right?.",
            "npc:That's right.",
            "end",
        })
        local artist1_ethenea_result, artist1_ethenea = t.inv.count("ethenea")
        t.check("artist1.gaveCorrectVial", artist1_ethenea_result == "ok" and artist1_ethenea == 0,
            string.format("inv.count(ethenea) -> %s (%s), expected 0 -- devinci_ethenea sets devinci_correct",
                tostring(artist1_ethenea_result), tostring(artist1_ethenea)))

        -- ==== The Varrock east gate (guidorgatelclosed 3264,3405,0) ====
        -- reach.py: every route from the Dancing Donkey Inn quarter
        -- (3270,3389), Asyff's shop and Guidor's house to the rest of the map
        -- crosses this gate (west edge of 3264,3405). Entering it from the
        -- west at %biohazard given_distillator..spoken_chemist runs the
        -- bioguard1 search (areas/varrock/scripts/east_gate.rs2:16-50):
        -- "Halt..." page, if_close, the search (no vial carried -- the
        -- errand boys have them), "You may now pass." page, if_close, the
        -- walk through to x >= 3264.
        t.exec("goto-goToVarrock", t.player.goto_tile, 3262, 3406, 0)
        t.exec("goToVarrock.gate", t.player.click_loc, "guidorgatelclosed", 1, { at = { 3264, 3405, 0 } })
        t.exec("goToVarrock.gate-dialog-1", t.chat.play, {
            "npc:Halt. I need to conduct a search on you.",
            "end",
        })
        local gate_reopen_result, gate_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "varrock_gate.search_reopen",
        }, 15)
        t.step("goToVarrock.gateSearchReopen", gate_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after if_close + the search's p_delay(3) -> "
                .. tostring(gate_reopen_result) .. " " .. tostring(gate_reopen_detail))
        t.exec("goToVarrock.gate-dialog-2", t.chat.play, {
            "npc:You may now pass.",
            "end",
        })
        await_tile(function(tt) return tt.x >= 3264 end, 10, "varrock_gate.in")
        check_tile("goToVarrock.throughGate", function(tt) return tt.x >= 3264 and tt.level == 0 end,
            "east of the gate, x >= 3264")
        for _, vial in ipairs({ "ethenea", "liquid_honey", "sulphuric_broline" }) do
            local vr, vc = t.inv.count(vial)
            t.check("goToVarrock.noVial." .. vial, vr == "ok" and vc == 0,
                "inv.count(" .. vial .. ") -> " .. tostring(vr) .. " (" .. tostring(vc) .. "), expected 0: the errand boy carries it past the search")
        end

        -- ==== Collect the vials back in Varrock (Dancing Donkey Inn) ====
        -- Two of the three collection branches (drunk2/gambler2) call
        -- if_close right after their own npc page and reopen a fresh one;
        -- artist2's does not.
        t.player.walk_to(3268, 3390, 40)
        t.exec("talkToDrunk2", t.player.talk_to, "drunk2", 1)
        t.exec("drunk2-dialog-1", t.chat.play, {
            "player:Hello, how was your journey?",
            "npc:Pretty thirst-inducing actually",
            "player:Please tell me that you haven't drunk the contents",
            "npc:Oh the gods no!",
            "npc:Here's your vial anyway.",
            "end",
        })
        local drunk2_reopen_result, drunk2_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "player"
            end,
            note = "drunk2.collect_reopen",
        }, 10)
        t.step("drunk2.collectReopen", drunk2_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == player) after if_close+mes+p_delay(2) -> "
                .. tostring(drunk2_reopen_result) .. " " .. tostring(drunk2_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("drunk2-dialog-2", t.chat.play, {
            "player:Thanks, I'll let you get your drink now.",
            "end",
        })
        local drunk2_vial_result, drunk2_vial = t.inv.count("sulphuric_broline")
        t.check("drunk2.returnedVial", drunk2_vial_result == "ok" and (drunk2_vial or 0) >= 1,
            string.format("inv.count(sulphuric_broline) -> %s (%s), expected >=1", tostring(drunk2_vial_result), tostring(drunk2_vial)))

        t.player.walk_to(3270, 3388, 10)
        -- Chancy (gambler2) and Da Vinci (artist2) draw model 25362 alone: a
        -- quad whose two faces are alpha -1, so nothing of them is drawn.
        -- The client picks such an npc by its box, like the reference's
        -- useAABBMouseCheck (seam matthew-mbp-m4-b58-seam1, conformance row
        -- seam.npc_drawing_no_face_is_pressed): a plain press reaches them.
        t.exec("talkToGambler2", t.player.talk_to, "gambler2", 1)
        t.exec("gambler2-dialog-1", t.chat.play, {
            "player:Hi, thanks for doing that.",
            "npc:No problem.",
            "end",
        })
        local gambler2_reopen_result, gambler2_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "gambler2.collect_reopen",
        }, 10)
        t.step("gambler2.collectReopen", gambler2_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after if_close+mes+p_delay(2) -> "
                .. tostring(gambler2_reopen_result) .. " " .. tostring(gambler2_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("gambler2-dialog-2", t.chat.play, {
            "npc:Next time give me something more valuable",
            "player:That was the idea.",
            "end",
        })
        local gambler2_vial_result, gambler2_vial = t.inv.count("liquid_honey")
        t.check("gambler2.returnedVial", gambler2_vial_result == "ok" and (gambler2_vial or 0) >= 1,
            string.format("inv.count(liquid_honey) -> %s (%s), expected >=1", tostring(gambler2_vial_result), tostring(gambler2_vial)))

        -- artist2 never wanders (npc_movement.generated.npc: wanderrange=0)
        -- and stands at 3272,3389 (m51_52.spawn:13), tucked against the bar;
        -- Stand on his open east side and name the copy (same no-face model
        -- as gambler2 above: picked by its box).
        t.player.walk_to(3273, 3389, 10)
        t.exec("talkToArtist2", t.player.talk_to, "artist2", 1, { at = { 3272, 3389 } })
        t.exec("artist2-dialog", t.chat.play, {
            "npc:Hello again.",
            "player:Well, as they say, it's always sunny in RuneScape.",
            "npc:Ok, here it is.",
            "player:Thanks, you've been a big help.",
            "end",
        })
        local artist2_vial_result, artist2_vial = t.inv.count("ethenea")
        t.check("artist2.returnedVial", artist2_vial_result == "ok" and (artist2_vial or 0) >= 1,
            string.format("inv.count(ethenea) -> %s (%s), expected >=1", tostring(artist2_vial_result), tostring(artist2_vial)))

        -- ==== Asyff/tailorp, Varrock (3281,3398,0): free priest gown ====
        -- (quests/quest_eaglepeak/scripts/asyff.rs2:46-81) -- offered while
        -- %biohazard is between spoken_chemist and found_secret and
        -- %biohazard_free_clothes=0, both true here. Same quarter as the
        -- inn (reach.py REACH with every door closed): walked.
        t.player.walk_to(3281, 3397, 30)
        t.exec("talkToAsyff", t.player.talk_to, "tailorp", 1)
        t.exec("asyff-dialog", t.chat.play, {
            "npc:Now you look like someone who goes to a lot of fancy dress parties.",
            "player:Errr... what are you saying exactly?",
            "npc:I'm just saying that perhaps you would like to peruse my selection of garments.",
            "options",
            "choose:Do you have a spare Priest Gown?",
            "player:Do you have a spare Priest Gown?",
            "npc:Well I do sell them.",
            "player:Please! It's really important.",
            "npc:Well I suppose you can have this old one.",
            "player:Thank you.",
            "end",
        })
        local priest_await_result = t.inv.await_all({ priest_gown = 1, priest_robe = 1 }, 10)
        t.step("expectPriestOutfit", priest_await_result == "ok" and "PASS" or "FAIL",
            "inv.await_all({priest_gown=1,priest_robe=1},10) -> " .. tostring(priest_await_result))
        t.exec("equipPriestGown", t.player.equip, "priest_gown")
        t.exec("equipPriestRobe", t.player.equip, "priest_robe")

        -- ==== Guidor's house (maps/m51_52.jl2) ====
        -- Front door fai_varrock_castle_door on the east edge of 3278,3382
        -- (the door tile is the street), front room x 3279-3282; Guidor's
        -- bedroom x 3283-3285 behind guidordoor on the east edge of
        -- 3282,3382. With the full priest set worn, Guidor's wife lets the
        -- player through (quest_biohazard/scripts/guidors_wife.rs2:11-25:
        -- "Guidor's wife allows you to go in." + ~west_ardy_walk_door);
        -- from inside it is a plain walk-through.
        pass_door("talkToGuidor.frontDoorIn", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3278, 3382, 0, 3277, 3382, 3280, 3382,
            function(tt) return tt.x >= 3279 and tt.x <= 3282 end, "in Guidor's front room, x 3279-3282")
        t.player.walk_to(3281, 3382, 10)
        cross("talkToGuidor.bedroomDoorIn", "guidordoor", 3282, 3382, 0,
            function(tt) return tt.x >= 3283 and tt.x <= 3285 and tt.level == 0 end, "in Guidor's bedroom, x 3283-3285 (priest set worn)")
        t.exec("talkToGuidor", t.player.talk_to, "guidor", 1)
        t.exec("guidor-dialog", t.chat.play, {
            "player:Hello, you must be Guidor.",
            "npc:Is my wife asking priests to visit me now?",
            "npc:Ever since she heard rumors",
            "npc:Of course she means well",
            "options",
            "choose:I've come to ask your assistance in stopping a plague.",
            "player:Well it's funny you should ask",
            "npc:So you're the plague carrier!",
            "options",
            "choose:I've been sent by your old pupil Elena.",
            "player:I've been sent by your old pupil Elena",
            "npc:Elena eh?",
            "player:Yes, she wants you to analyse it.",
            "npc:Right then, sounds like we'd better get to work!",
            "player:I have the plague sample.",
            "npc:Now I'll be needing some liquid honey",
            "player:... some ethenea?",
            "npc:Indeed!",
            "npc:Now I'll just apply these to the sample",
            "options",
            "choose:So what does that mean exactly?",
            "player:So what does that mean exactly?",
            "npc:I don't know what this sample is",
            "player:So what about the plague?",
            "npc:Don't you understand? There is no Plague!",
            "npc:I'm very sorry",
            "npc:The only question is",
            "end",
        })
        local items_gone_result, sample_left = t.inv.count("plaguesample")
        t.check("guidor.itemsConsumed", items_gone_result == "ok" and sample_left == 0,
            string.format("inv.count(plaguesample) -> %s (%s), expected 0 -- guidor_elena consumes sample/vials/paper",
                tostring(items_gone_result), tostring(sample_left)))
        t.expect("quest.stage.found_secret", t.quest.expect_stage("found_secret"))
        cross("talkToGuidor.bedroomDoorOut", "guidordoor", 3282, 3382, 0,
            function(tt) return tt.x >= 3279 and tt.x <= 3282 and tt.level == 0 end, "back in the front room, x 3279-3282")
        pass_door("talkToGuidor.frontDoorOut", "fai_varrock_castle_door", "fai_varrock_castle_door_open", 3278, 3382, 0, 3279, 3382, 3276, 3382,
            function(tt) return tt.x <= 3278 end, "on the street west of the house, x <= 3278")

        -- Back out of the quarter through the east gate, east to west: at
        -- found_secret the search branch is skipped and the gate walks the
        -- player to movecoord(loc_coord, -1, 0, 0) = 3263,3405
        -- (east_gate.rs2:56-61).
        t.player.walk_to(3265, 3405, 40)
        cross("returnToElenaAfterSampling.gate", "guidorgatelclosed", 3264, 3405, 0,
            function(tt) return tt.x <= 3263 and tt.level == 0 end, "west of the gate, x <= 3263")

        -- ==== Report to Elena -- no choices, auto-advances ====
        -- Varrock to Ardougne: every walk on foot opens a members' gate
        -- (membergater 2935,3450 at margin 160, the b63 grader's charge), so
        -- the trip is a real Ardougne Teleport. Guidor consumed the plague
        -- sample (guidor.itemsConsumed above), so nothing is lost to it.
        local sample_result, sample_count = t.inv.count("plaguesample")
        t.check("returnToElenaAfterSampling.noSampleCarried", sample_result == "ok" and sample_count == 0,
            "inv.count(plaguesample) -> " .. tostring(sample_result) .. " (" .. tostring(sample_count) .. "), want 0 before teleporting")
        t.player.teleport_cast("ardougne_teleport", { 2661, 3301, 0 }, { name = "returnToElenaAfterSampling.ardougneTeleport",
            runes = { { "waterrune", 2 }, { "lawrune", 2 } }, where = "East Ardougne market" })
        elena_in("returnToElenaAfterSampling")
        t.exec("talkToElenaReport", t.player.talk_to, "elena2_vis", 1)
        t.exec("elenaReport-dialog", t.chat.play, {
            "npc:You're back! So what did Guidor say?",
            "player:Nothing.",
            "npc:What?",
            "player:He said that there is no plague.",
            "npc:So what, this thing has all been a big hoax?",
            "player:Or maybe we're about to uncover something huge.",
            "npc:Then I think this thing may be bigger than both of us.",
            "player:What do you mean?",
            "npc:I mean you need to go right to the top",
            "end",
        })
        t.expect("quest.stage.reported_elena", t.quest.expect_stage("reported_elena"))
        elena_out("returnToElenaAfterSampling")

        -- ==== King Lathas, East Ardougne castle (2578,3293,1) -- finale ====
        -- maps/m40_51.jl2: double door w_ardougnedoubledoorl/r on the west
        -- edge of 2576,3298-3299 (the door tile is the street); the stairs
        -- (2571-2572, 3295-3297) are climbed from their north front
        -- 2571,3298 (no up maplink row: +1 plane on the stand tile); the
        -- king's room x 2575-2579 z 3292-3294,1 is behind elfdoor on the
        -- west edge of 2575,3293,1. Choosing "I don't understand..." runs
        -- the full exposition and ends in
        -- queue(quest_biohazard_complete,0,0) -- asynchronous (section 8),
        -- so the reward/complete read is polled after ticking.
        local thieving_snapshot_result, thieving_snapshot = t.skill.snapshot()
        t.step("skillSnapshotBeforeReward", thieving_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot() -> " .. tostring(thieving_snapshot_result))

        t.exec("goto-kingLathas", t.player.goto_tile, 2577, 3298, 0)
        pass_door("informTheKing.castleDoor", "w_ardougnedoubledoorl", "w_ardougnedoubledoorlopen", 2576, 3298, 0, 2577, 3298, 2573, 3298,
            function(tt) return tt.x <= 2575 end, "in the castle hall, x <= 2575")
        climb("informTheKingGoUpstairs", "stairs", 2571, 3295, 0, 2571, 3298, 1,
            function(tt) return tt.x == 2571 and tt.z == 3298 end, "2571,3298,1 above the stand tile")
        pass_door("informTheKing.kingsDoor", "elfdoor", "elfdooropen", 2575, 3293, 1, 2574, 3293, 2576, 3293,
            function(tt) return tt.x >= 2575 and tt.x <= 2579 and tt.z >= 3292 and tt.z <= 3294 end, "in King Lathas's room, x 2575-2579 z 3292-3294 level 1")
        t.exec("talkToKingLathas", t.player.talk_to, "kinglathas", 1)
        t.exec("kingLathas-dialog", t.chat.play, {
            "player:I assume that you are the King of East Ardougne?",
            "npc:You assume correctly",
            "player:I get it from finding out that the plague is a hoax.",
            "npc:A hoax? I've never heard such a ridiculous thing",
            "player:I have evidence, from Guidor of Varrock.",
            "npc:Ah... I see. Well then you are right about the plague.",
            "player:When is it ever good to lie to people like that?",
            "npc:When it protects them from a far greater danger",
            "options",
            "choose:I don't understand...",
            "player:I don't understand...",
            "npc:Their King, Tyras, journeyed out to the West",
            "npc:The Dark Lord agreed to spare his life",
            "player:So what happened?",
            "npc:The chalice corrupted him.",
            "npc:And so I erected this wall",
            "npc:Now, with the King of West Ardougne",
            "npc:So I'm sorry that I lied about the plague.",
            "player:Well at least I know now, but what can we do about it?",
            "npc:Nothing at the moment, I'm waiting for my scouts",
            "npc:When this happens, can I count on your support?",
            "player:Absolutely!",
            "npc:Thank the gods! I give you permission to use my training area.",
            "npc:It's located just to the north west of Ardougne",
            "player:Ok. There's just one thing I don't understand",
            "npc:How could I not do? He was my brother.",
            "end",
        })
        t.ticks(3) -- queue(quest_biohazard_complete,0,0) is a fresh queued task (section 8's async-completion rule)

        t.quest.expect_complete() -- writes quest.varp_complete/quest.scroll_title/quest.points/quest.journal itself
        t.expect("skill.thievingReward", t.skill.expect_gain("thieving", 1250, thieving_snapshot))
        t.finish(0)
    end,
}
