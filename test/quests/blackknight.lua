-- Black Knights' Fortress -- driven through the real Sir Amik Varze dialogue
-- tree (areas/falador/scripts/sir_amik_varze.rs2), the witchgrill listen and
-- the cabbage-in-the-hole sabotage (quests/quest_blackknight/scripts/
-- quest_blackknight.rs2), never through ::setvar on %spy.
--
-- Every closed space is entered and left on foot (owner rule 2026-10-03):
-- the only gotos are three overland hops between open tiles (Lumbridge ->
-- the White Knights' Castle courtyard 2968,3338; the courtyard -> the open
-- path south of the fortress door 3016,3512; and back). The castle's double
-- door and both spiral staircases are clicked going up to Sir Amik and
-- coming down, and the fortress is walked the guide's way, every door,
-- secret wall and ladder clicked in and out:
--   bkfortressdoor1 (disguise) -> pushWall (bksecretdoor 3016,3517) ->
--   climbUpLadder1/2 -> climbDownLadder3 -> wild_door 3019,3515,1 ->
--   climbUpLadder4 -> climbDownLadder5 -> bkfortressdoor3 3025,3511,1 ->
--   climbDownLadder6 -> listenAtGrill, then the same chain back to the
--   secret room, pushWall3, bkfortressdoor2, the meeting ladder, pushWall2,
--   useCabbageOnHole, and out again (pushWall2, the meeting ladder down,
--   bkfortressdoor2, bkfortressdoor1).
-- The m47_54 square has no maplink rows, so every dk ladder is the
-- +/-1-plane default on the PLAYER's own tile (ladders_stairs/scripts/
-- ladders.rs2 [proc,climb]): each climb walks to the stand tile first, and
-- that tile decides the landing (L1 3021,3511 is solid on L0, so ladder 6 is
-- climbed from 3022,3510; ladder 5 from 3025,3514 lands in the corridor that
-- reaches the grill ladder only through bkfortressdoor3).
--
-- Two real fights: bkfortressdoor3 crossed westward (quest_blackknight.rs2
-- [oploc1,bkfortressdoor3]; LostCity's copy notes "black knights guarding
-- the ladder down to the grill will aggress the player") and
-- bkfortressdoor2's "I don't care. I'm going in anyway." both call
-- ~black_knights_aggro, which puts every aggressive_black_knight within 5 on
-- the player with npc_setmode(opplayer2). `::passive` does not block that
-- (QUEST_SERVER_CHEATS.md section F: content staging a fight is never
-- gated); it only stops the dozen wandering level-33 knights from STARTING
-- fights elsewhere on the route. So the guide's recommended food is carried
-- and eaten, and each fight leg ends in a margin row.
--
-- %qp must be >= ^blackknight_qp_req (12) before Sir Amik offers the quest
-- at all (sir_amik_varze.rs2's prequest branch) -- a prerequisite (quest
-- points earned elsewhere), not this quest's own deliverable.

local EAT_BELOW = 30

return {
    id = "blackknight",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- the fortress is walked floor by floor, both ways: run 1 ran out at ~2,000 ticks
    setup = {
        "::clearinv",
        "::blackknightrun",         -- content's own reset: %spy=0, clears the cauldron/armour hints and any leftover coins/cabbage/dossier
        "::give bronze_med_helm 1", -- Quest Helper bring-along (guard disguise), worn for real in run() below
        "::give iron_chainbody 1",
        "::give cabbage 1",         -- Quest Helper bring-along (an ordinary cabbage, sourced anywhere) -- the SABOTAGE is using it on the hole, driven for real below
        "::give lobster 10",        -- Quest Helper getItemRecommended: food (BlackKnightFortress.java:188, 377); eaten below EAT_BELOW in the two aggro fights
        -- The guide warns "Be prepared for multiple level 33 Black Knights to
        -- attack you" (enterFortress) and recommends armour; the disguise
        -- fixes the helm and body, so the character is staged at a level a
        -- player taking that warning would bring (a fresh account's 10 hp
        -- dies to three of them).
        "::setlevel hitpoints 45",
        "::setlevel defence 30",
        "::setvar varp101_qp 12",           -- prerequisite quest points, not blackknight's own reward
        -- areas/world/configs/m47_54.spawn scatters a dozen
        -- `aggressive_black_knight`/`kr_aggressive_black_knight` (level 33)
        -- across every floor the route walks; they stop STARTING fights.
        -- The two fights content stages itself (door3, door2) still happen.
        "::passive aggressive_black_knight",
        "::passive kr_aggressive_black_knight",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp130_spy",
            constants = {
                not_started = 0,
                started = 1,
                listened = 2,
                sabotaged = 3,
                complete = 4,
                questpoints = 3,
                qp_req = 12,
            },
            row = "quest_blackknightsfortress",
            display = "Black Knights' Fortress",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the setup cheats' writes are not client-visible yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- Hitpoints are sampled after every row inside the fortress; below
        -- EAT_BELOW a lobster is eaten. The margin rows read these.
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    local er = t.player.inv_op("lobster", 1)
                    if er == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(1)
                end
            end
        end
        local function margin_row(name, fight)
            vitals()
            local fr, food = t.inv.count("lobster")
            local hr, hp = t.skill.read("hitpoints")
            t.check(name, hp_low ~= nil and hp_low >= 25 and fr == "ok" and food >= 1,
                fight .. ": lowest hp " .. tostring(hp_low) .. "/45 (sampled after every row), hp now "
                    .. tostring(hr == "ok" and hp.level or hr) .. ", lobsters staged 10, eaten " .. hp_eaten
                    .. ", left " .. tostring(food) .. " (" .. tostring(fr) .. ") (margin: lowest hp >= 25 AND food left)")
            hp_low = nil
        end

        -- Walk to the near side of a door that opens in place (next_loc_stage
        -- pair). If the closed leaf stands at door_x,door_z, click THAT copy;
        -- otherwise an earlier press left it open (doors swing back after 500
        -- ticks), so assert the open leaf really stands within a tile of the
        -- door tile -- a row that fails when neither leaf is there -- and do
        -- not press it again. Then walk to the far side and check the tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.world.loc_near(closed_sym, 3)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": " .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z) or tostring(cr))
                        .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z) or tostring(orr))
                        .. " (want within 1 of the door tile: already standing open from an earlier press, so it is walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- Wait for a teleport the click queued (a door's walk-through, a
        -- climb) to land; the row after it reads the tile and grades it.
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end

        -- A fortress door or secret wall: content's ~blackknight_walk_door
        -- teleports the player across (no open leaf, nothing stays open), so
        -- it is clicked on every crossing and graded on the far tile.
        -- A one-tile walk-through (bkfortressdoor3) is a hop too short for
        -- click_loc's teleport arm, so it answers `timeout
        -- settle_after_click` on a crossing that landed (start-and-travel:
        -- "A short hop (stiles) does not trip it"): the row is graded on the
        -- tile, which must be on the far side AND the tile before the click
        -- must not have been.
        local function cross(name, sym, lx, lz, far_ok, far_desc)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { lx, lz } })
            await_tile(far_ok, 10, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and not far_ok(bt) and wr == "ok" and far_ok(wt),
                "from " .. tile_text(br, bt) .. " click_loc(" .. sym .. " at " .. lx .. "," .. lz .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; world.tile -> " .. tile_text(wr, wt) .. " (want " .. far_desc .. ")")
            vitals()
        end

        -- A dk ladder: walk to the stand tile beside it, click that copy,
        -- and check the landing is the stand tile one plane up or down.
        local function climb(name, sym, lx, lz, stand_x, stand_z, want_level)
            t.player.walk_to(stand_x, stand_z, 30)
            local sr, st = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { lx, lz } })
            await_tile(function(tt) return tt.level == want_level end, 8, name)
            local wr, wt = t.world.tile()
            t.check(name, cr == "ok" and wr == "ok" and wt.level == want_level and wt.x == stand_x and wt.z == stand_z,
                "from " .. tile_text(sr, st) .. " click_loc(" .. sym .. " at " .. lx .. "," .. lz .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(wr, wt) .. " (want " .. stand_x .. "," .. stand_z .. "," .. want_level .. ")")
            vitals()
        end

        -- A castle spiral staircase (a maplink, not the own-tile default):
        -- graded on the plane it lands on.
        local function stairs(name, sym, want_level)
            local cr, cd = t.player.click_loc(sym, 1)
            t.ticks(3)
            local wr, wt = t.world.tile()
            t.check(name, cr == "ok" and wr == "ok" and wt.level == want_level,
                "click_loc(" .. sym .. ") -> " .. tostring(cr) .. " " .. tostring(cd) .. "; landed " .. tile_text(wr, wt)
                    .. " (want level " .. want_level .. ")")
        end

        -- The White Knights' Castle: the goto lands in the open courtyard
        -- (2968,3338: reach.py walks it to 2990,3360 with every door shut);
        -- the west keep holding the spiral stairs is behind the double door
        -- fai_falador_castledoubledoorl/r at 2965,3338-3339 (courtyard
        -- x >= 2965, keep x <= 2964), crossed on foot both ways.
        local function castle_in(pfx)
            pass_door(pfx .. ".castleDoorIn", "fai_falador_castledoubledoorl", "fai_falador_opencastledoubledoorl", 2965, 3338, 2966, 3338, 2962, 3338,
                function(tt) return tt.x <= 2964 and tt.level == 0 end, "inside the west keep, x <= 2964")
        end
        local function castle_out(pfx)
            pass_door(pfx .. ".castleDoorOut", "fai_falador_castledoubledoorl", "fai_falador_opencastledoubledoorl", 2965, 3338, 2963, 3338, 2968, 3338,
                function(tt) return tt.x >= 2965 and tt.level == 0 end, "back in the open courtyard, x >= 2965")
        end

        -- ------------------------------------------------- accept the quest
        t.exec("goto-castle", t.player.goto_tile, 2968, 3338, 0)
        castle_in("speakToAmik")
        stairs("climbToWhiteKnightsCastleF1", "fai_falador_castle_spiralstairs", 1)
        stairs("climbToWhiteKnightsCastleF2", "fai_falador_castle_spiralstairs", 2)
        t.exec("speakToAmik", t.player.talk_to, "sir_amik_varze")

        local d1_result, d1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("amik.drain_to_quest_offer", d1_result, d1_detail)
        t.shot("amik-quest-offer")

        t.exec("amik.choose_seek_quest", t.chat.choose, "I seek a quest!")

        local d2_result, d2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("amik.drain_to_danger_choice", d2_result, d2_detail)
        t.shot("amik-danger-choice")

        t.exec("amik.choose_laugh", t.chat.choose, "I laugh in the face of danger!")

        local d3_result, d3_detail = t.chat.drain({ stop_at = "options" })
        t.expect("amik.drain_to_start_confirm", d3_result, d3_detail)
        t.shot("amik-start-confirm")

        t.exec("amik.choose_start_yes", t.chat.choose, "Yes.")

        local d4_result, d4_detail = t.chat.drain({ stop_at = "none" })
        t.expect("amik.accept_drain_close", d4_result, d4_detail)
        t.shot("amik-accepted")

        -- inv_add from the dialogue's own grant is not client-visible the
        -- instant drain's last continue_ returns (section 8's inv-sync
        -- gap) -- poll, then write the settled count back as the detail.
        local dossier_wait_result = t.inv.await("bk_dossier", 1, 10)
        local dossier_count_result, dossier_count = t.inv.count("bk_dossier")
        t.check("dossier.granted",
            dossier_wait_result == "ok" and dossier_count_result == "ok" and dossier_count == 1,
            "inv.await(bk_dossier,1) -> " .. tostring(dossier_wait_result)
                .. " count=" .. tostring(dossier_count) .. "(" .. tostring(dossier_count_result) .. ")")

        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- Quest Helper's "Infiltrate the fortress" panel lists
        -- bronzeMed/ironChainbody: the disguise bkfortressdoor1 checks
        -- (~blackknight_disguised), worn for real.
        t.exec("equip.bronze_med_helm", t.player.equip, "bronze_med_helm")
        t.exec("equip.iron_chainbody", t.player.equip, "iron_chainbody")

        stairs("climbDownToWhiteKnightsCastleF1", "fai_falador_castle_spiralstairstop", 1)
        stairs("climbDownToWhiteKnightsCastleF0", "fai_falador_castle_spiralstairstop", 0)
        castle_out("enterFortress")

        -- ---------------------------------------------- into the fortress
        -- 3016,3512 is the open path south of bkfortressdoor1 (3016,3514,
        -- wall on its north edge; reach.py walks 3016,3512 to 3016,3480 and
        -- to the castle courtyard with every door shut). check_axis makes
        -- z = 3514 the outside: disguised, ~blackknight_walk_door(true)
        -- teleports the player to 3016,3515.
        t.exec("goto-fortress-door", t.player.goto_tile, 3016, 3512, 0)
        -- 3016,3512 is in a [zone,0_47_54_16_56] wilderness_warning zone
        -- (areas/area_wilderness/scripts/wilderness_warning.rs2): the first
        -- entry queues three mesboxes and p_stopaction, which swallowed run
        -- 1's door click. Read them through before the door.
        local warn_open = t.await({
            level = function() return t.chat.kind() ~= "none" end,
            note = "wilderness warning: waiting for the first mesbox",
        }, 6)
        if warn_open == "ok" then
            t.exec("wildernessWarning.text", t.chat.expect_text, "WARNING! Proceed with caution.")
            local ww_result, ww_kind = t.chat.drain({ stop_at = "none" })
            t.expect("wildernessWarning.drain", ww_result, ww_kind)
        end
        cross("enterFortress", "bkfortressdoor1", 3016, 3514,
            function(tt) return tt.level == 0 and tt.z >= 3515 end, "inside the entrance hall, z >= 3515")

        -- pushWall: bksecretdoor 3016,3517 (south edge). From 3016,3516
        -- check_axis_locactive is false -> the player lands on 3016,3517,
        -- the secret room (x 3015-3016, z 3517-3518).
        t.player.walk_to(3016, 3516, 20)
        cross("pushWall", "bksecretdoor", 3016, 3517,
            function(tt) return tt.level == 0 and tt.z >= 3517 and tt.x <= 3016 end, "in the secret room, z >= 3517")
        t.exec("pushWall.message", t.msg.expect, "You push against the wall")

        -- ------------------------------------------ up and over to the grill
        climb("climbUpLadder1", "dk_ladder", 3015, 3519, 3015, 3518, 1)
        climb("climbUpLadder2", "dk_ladder", 3016, 3519, 3016, 3518, 2)
        -- Ladder 3 is approached from its south tile 3017,3515 on both
        -- floors (run 2: a press from 3016,3516 walked round to 3017,3515).
        climb("climbDownLadder3", "dk_laddertop", 3017, 3516, 3017, 3515, 1)
        pass_door("climbUpLadder4.wildDoorEast", "wild_door", "wild_door_open", 3019, 3515, 3019, 3515, 3021, 3515,
            function(tt) return tt.x >= 3020 and tt.level == 1 end, "east of the door, x >= 3020, level 1")
        vitals()
        climb("climbUpLadder4", "dk_ladder", 3023, 3513, 3023, 3514, 2)
        climb("climbDownLadder5", "dk_laddertop", 3025, 3513, 3025, 3514, 1)
        -- bkfortressdoor3 (3025,3511, east edge): from the east side
        -- (x = 3026) check_axis is false -> ~black_knights_aggro on the
        -- knights guarding the grill ladder, then the player lands on 3025,3511.
        t.player.walk_to(3026, 3511, 20)
        cross("bkfortressdoor3.west", "bkfortressdoor3", 3025, 3511,
            function(tt) return tt.level == 1 and tt.x <= 3025 end, "west of the door, x <= 3025, level 1")
        climb("climbDownLadder6", "dk_laddertop", 3021, 3510, 3022, 3510, 0)

        -- --------------------------------------------- listen at the grill
        local grill_loc_result, grill_loc = t.world.loc_near("witchgrill", 20)
        t.step("grill.locate", grill_loc_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(witchgrill,20) -> " .. tostring(grill_loc_result) .. " "
                .. (grill_loc_result == "ok"
                    and string.format("tile=%d,%d,%d match=%s", grill_loc.tile_x, grill_loc.tile_z,
                        grill_loc.level, tostring(grill_loc.match))
                    or tostring(grill_loc)))

        -- click_loc's own approach answered `covered` from every side in
        -- earlier runs, so this presses through drive.click_minimenu from
        -- the room's own tiles beside the grill (west, north, south: the
        -- east side is past the wall), WALKED to inside the listening room.
        -- "A dialogue opened" is the success test, not the press's `ok`.
        local listen_result, listen_detail
        local listen_tried = ""
        local listen_row_text = "n/a"
        local listen_opened = false
        if grill_loc_result == "ok" then
            local grill_sides = {
                { grill_loc.tile_x - 1, grill_loc.tile_z, "west" },
                { grill_loc.tile_x, grill_loc.tile_z + 1, "north" },
                { grill_loc.tile_x, grill_loc.tile_z - 1, "south" },
            }
            for side_index = 1, #grill_sides do
                local side_x = grill_sides[side_index][1]
                local side_z = grill_sides[side_index][2]
                local side_name = grill_sides[side_index][3]
                t.player.walk_to(side_x, side_z, 20)
                local sr, st = t.world.tile()
                listen_tried = listen_tried .. side_name .. "@" .. tile_text(sr, st) .. " "
                for press = 1, 2 do
                    listen_result, listen_detail = t.drive.click_minimenu(grill_loc, 1)
                    listen_tried = listen_tried .. "#" .. press .. "=" .. tostring(listen_result) .. " "
                    if listen_result == "ok" then
                        if type(listen_detail) == "table" then
                            listen_row_text = tostring(listen_detail.row_text)
                        end
                        local open_result = t.await({
                            level = function() return t.chat.kind() ~= "none" end,
                            note = "listen.grill: waiting for the grill's own dialogue",
                        }, 8)
                        if open_result == "ok" then
                            listen_opened = true
                            break
                        end
                    end
                end
                if listen_opened then
                    break
                end
            end
        end
        t.step("listenAtGrill", listen_opened and "PASS" or "FAIL",
            "drive.click_minimenu(witchgrill,1) attempts: " .. listen_tried
                .. "-- pressed row: " .. listen_row_text .. " -- dialogue opened: " .. tostring(listen_opened))
        t.shot("listen-grill")

        local d5_result, d5_detail = t.chat.drain({ stop_at = "none" })
        t.expect("grill.dialogue_drain", d5_result, d5_detail)
        t.shot("grill-dialogue-closed")

        t.expect("quest.stage.listened", t.quest.expect_stage("listened"))

        -- ------------------------------------- back to the secret room
        -- The guide's climbUpLadder6..climbDownLadder1, the same chain in
        -- reverse. bkfortressdoor3 from its west tile (x = 3025) is the
        -- outside: no aggro, the player lands on 3026,3511.
        climb("climbUpLadder6", "dk_ladder", 3021, 3510, 3022, 3510, 1)
        t.player.walk_to(3025, 3511, 20)
        cross("bkfortressdoor3.east", "bkfortressdoor3", 3025, 3511,
            function(tt) return tt.level == 1 and tt.x >= 3026 end, "east of the door, x >= 3026, level 1")
        climb("climbUpLadder5", "dk_ladder", 3025, 3513, 3025, 3514, 2)
        margin_row("listenAtGrill.fight.margin", "the grill-ladder knights bkfortressdoor3 set on the player")
        climb("climbDownLadder4", "dk_laddertop", 3023, 3513, 3023, 3514, 1)
        pass_door("climbUpLadder3.wildDoorWest", "wild_door", "wild_door_open", 3019, 3515, 3020, 3515, 3018, 3515,
            function(tt) return tt.x <= 3019 and tt.level == 1 end, "west of the door, x <= 3019, level 1")
        climb("climbUpLadder3", "dk_ladder", 3017, 3516, 3017, 3515, 2)
        climb("climbDownLadder2", "dk_laddertop", 3016, 3519, 3016, 3518, 1)
        climb("climbDownLadder1", "dk_laddertop", 3015, 3519, 3015, 3518, 0)

        -- ---------------------------------------------- sabotage the potion
        -- pushWall3: bksecretdoor from inside (z = 3517 -> lands 3016,3516).
        t.player.walk_to(3016, 3517, 20)
        cross("pushWall3", "bksecretdoor", 3016, 3517,
            function(tt) return tt.level == 0 and tt.z <= 3516 end, "out of the secret room, z <= 3516")
        t.exec("pushWall3.message", t.msg.expect, "You push against the wall")

        -- bkfortressdoor2 from outside is the guard's warning; option 2 "I
        -- don't care. I'm going in anyway." (quest_blackknight.rs2:208-218)
        -- walks the player in and sets the meeting room's knights on him.
        t.exec("goUpLadderToCabbageZone.door", t.player.click_loc, "bkfortressdoor2", 1)
        t.exec("goUpLadderToCabbageZone.dialogue", t.chat.play, {
            "npc:I wouldn't go in there",
            "options",
            "choose:I don't care. I'm going in anyway.",
            "player:I don't care",
        })
        t.ticks(2)
        local d2_r, d2_at = t.world.tile()
        t.check("goUpLadderToCabbageZone.inside", d2_r == "ok" and d2_at.x >= 3020 and d2_at.level == 0,
            "world.tile after bkfortressdoor2 -> " .. tile_text(d2_r, d2_at) .. " (want x >= 3020, level 0)")
        vitals()
        cross("goUpLadderToCabbageZone", "dk_meeting_ladder", 3022, 3518,
            function(tt) return tt.level == 1 end, "level 1, the north path")

        -- pushWall2: bksecretdoor 3030,3510,1 (south edge) from the north
        -- path (z = 3510) -> lands 3030,3509, the cabbage-hole room.
        t.player.walk_to(3030, 3511, 30)
        vitals()
        t.player.walk_to(3030, 3510, 10)
        cross("pushWall2", "bksecretdoor", 3030, 3510,
            function(tt) return tt.level == 1 and tt.z <= 3509 end, "in the cabbage-hole room, z <= 3509")
        t.exec("pushWall2.message", t.msg.expect, "You push against the wall")

        local hole_loc_result, hole_loc = t.world.loc_near("blackknighthole", 20)
        t.step("hole.locate", hole_loc_result == "ok" and "PASS" or "FAIL",
            "world.loc_near(blackknighthole,20) -> " .. tostring(hole_loc_result) .. " "
                .. (hole_loc_result == "ok"
                    and string.format("tile=%d,%d,%d match=%s", hole_loc.tile_x, hole_loc.tile_z,
                        hole_loc.level, tostring(hole_loc.match))
                    or tostring(hole_loc)))

        local hole_target = t.player.by_symbol("loc", "blackknighthole")
        t.exec("useCabbageOnHole", t.player.use_on, "cabbage", hole_target)

        local d6_result, d6_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hole.sabotage_drain", d6_result, d6_detail)
        t.shot("hole-sabotage-closed")

        t.expect("quest.stage.sabotaged", t.quest.expect_stage("sabotaged"))

        local cabbage_absent_result, cabbage_absent_detail = t.inv.expect_absent("cabbage")
        t.check("cabbage.consumed", cabbage_absent_result == "ok",
            "inv.expect_absent(cabbage) -> " .. tostring(cabbage_absent_result) .. " " .. tostring(cabbage_absent_detail))

        -- ------------------------------------------- out of the fortress
        -- The cabbage room is left the way it was entered: bksecretdoor from
        -- inside (z = 3509 -> 3030,3510), the meeting ladder down
        -- (goBackDownFromCabbageZone), bkfortressdoor2 from inside (x = 3020
        -- -> 3019,3515), bkfortressdoor1 from inside (z = 3515 -> 3016,3514).
        t.player.walk_to(3030, 3509, 10)
        cross("pushWall2.out", "bksecretdoor", 3030, 3510,
            function(tt) return tt.level == 1 and tt.z >= 3510 end, "back on the north path, z >= 3510")
        t.player.walk_to(3022, 3517, 30)
        vitals()
        cross("goBackDownFromCabbageZone", "dk_meeting_laddertop", 3022, 3518,
            function(tt) return tt.level == 0 end, "level 0, the meeting room")
        t.player.walk_to(3020, 3515, 20)
        vitals()
        cross("leaveMeetingRoom", "bkfortressdoor2", 3020, 3515,
            function(tt) return tt.level == 0 and tt.x <= 3019 end, "the entrance hall, x <= 3019")
        t.player.walk_to(3016, 3515, 20)
        vitals()
        cross("leaveFortress", "bkfortressdoor1", 3016, 3514,
            function(tt) return tt.level == 0 and tt.z <= 3514 end, "outside the fortress, z <= 3514")
        t.player.walk_to(3016, 3512, 20)
        local out_r, out_at = t.world.tile()
        t.check("leaveFortress.outside", out_r == "ok" and out_at.z <= 3512 and out_at.level == 0,
            "walked to the open path 3016,3512 -> " .. tile_text(out_r, out_at))
        margin_row("goBackDownFromCabbageZone.fight.margin", "the meeting-room knights bkfortressdoor2 set on the player")

        -- ------------------------------------------------------- hand in
        t.exec("goto-castle-return", t.player.goto_tile, 2968, 3338, 0)
        castle_in("returnToAmik")
        stairs("climbToWhiteKnightsCastleF1ToFinish", "fai_falador_castle_spiralstairs", 1)
        stairs("climbToWhiteKnightsCastleF2ToFinish", "fai_falador_castle_spiralstairs", 2)

        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        t.exec("returnToAmik", t.player.talk_to, "sir_amik_varze")

        -- Stop AT a mesbox rather than draining past it: the hand-in's own
        -- [queue,black_knights_fortress_quest_complete] can land mid-drain
        -- or only after the monologue has closed (section 8's
        -- completion-is-asynchronous gap).
        local d7_result, d7_kind = t.chat.drain({ stop_at = "mesbox" })
        t.expect("amik.return_drain", d7_result, d7_kind)
        t.shot("amik-return-closed")

        if d7_kind ~= "mesbox" then
            local mesbox_wait_result, mesbox_wait_detail = t.await({
                level = function() return t.chat.kind() == "mesbox" end,
                note = "handin: waiting for the queued coins mesbox",
            }, 20)
            t.expect("handin.await_mesbox", mesbox_wait_result, mesbox_wait_detail)
        end

        t.exec("handin.mesbox_text", t.chat.expect_text, "Sir Amik hands you 2500 coins.")
        t.exec("handin.dismiss_mesbox", t.chat.continue_, true)

        t.quest.expect_complete()

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins",
            reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
                and reward_coins_after == reward_coins_before + 2500,
            string.format("coins %s -> %s (want +2500), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
