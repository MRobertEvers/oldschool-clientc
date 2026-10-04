-- Throne of Miscellania (1 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_misc/scripts/
--   misc_king_vargas.rs2, misc_princess_astrid.rs2, misc_prince_brand.rs2,
--   misc_queen_sigrid.rs2, misc_advisor_ghrim.rs2, misc_giant_nib.rs2,
--   misc_courting_emotes.rs2, misc_debug.rs2
-- and areas/area_miscellania/scripts/{flower_girl,derrik,lumberjack_leif}.rs2
-- (Derrik's opnpc1 delegates to misc_smithy.rs2's
-- [label,misc_smithy_giant_nib] at exactly %misc_quest = ^misc_gave_king_treaty).
--
-- Access: misc_door_guard.rs2 hard-gates the throne room on %heroquest =
-- ^hero_complete (Heroes' Quest, unported -- ::complete quest_heroes in
-- setup).
--
-- RE-AUTHOR (matthew-mbp-m4-b59, door rule): NO goto into or out of a closed
-- space. Every room is walked into and out of on every visit:
--  * Miscellania castle (maps/m39_60.jl2): the east door castledoor
--    2510,3860, the hall door castledoor 2505,3860, the south stair room's
--    castledoor 2506,3851 (a copy on level 0 AND level 1 at that tile --
--    every door is pressed by tile and level), the south spiralstairs_wooden
--    2505,3848 (maplink.dbrow 0_39_60_9_10 up -> 2504,3849,1; the level-1
--    copy is spiralstairsmiddle_wooden, whose op1 "Climb" goes UP, so the way
--    down is op3 from 2504,3849,1 -> 2505,3850,0), Brand's castledoor
--    2504,3853,1, and the throne room's misc_ulby_throneroomdoor 2506,3857,1
--    (a walk-through, ~misc_ulby_walk_door; before the quest starts its first
--    press is the door guard's "Halt! Who goes there?", which grants the
--    audience).
--  * Etceteria castle (maps/m40_60.jl2): castledoor 2608,3875 and 2609,3875,
--    the stair room's castledoor 2615,3870 (both levels), spiralstairs
--    2613,3867 up -> 2615,3867,1 and spiralstairstop 2614,3867,1 down ->
--    2614,3866,0, then Sigrid's room through 2615,3870,1.
--  * Derrik's house: viking_abode_door 2551,3893.
-- Miscellania and Etceteria are one walkable landmass on level 0, so the hops
-- between the two castles' front doors, Derrik's door and Leif's grove are
-- overland travel between open tiles. The voyage to the island is not: the
-- guide's travelToMisc is a boat from Rellekka, and this pack has no
-- Miscellania boat (viking_sailor.rs2:14-17 answers "I still need to fix
-- this longboat" after the Fremennik Trials; LostCity has no such sailor
-- either), so the run's FIRST goto stands the player on the island's dock
-- 2581,3845 -- where that boat lands -- in the open, outside every building.
--
-- Courting partner: Prince Brand (%misc_partner_multivar = 1). Brand also
-- writes the anthem later regardless of who is courted (his own file
-- header), so courting him means only one npc's affection ladder to climb
-- instead of two -- Astrid's Dance leg is never reached from this path and
-- is not driven here.
--
-- Content-parity fix 2026-09-23 (docs/QUEST_HELPER_COVERAGE_2026-09-23.md):
-- two things this port used to soft-skip are real now.
--   (1) King Vargas's own [opnpc1] courting choice now sets %misc_affection
--       = ^misc_affection_not_started (not step0), so brand_talk1 (the
--       five-line courtship intro, "Be still, my heart...") fires on the
--       FIRST live visit as Quest Helper's own talkBrand1 step expects.
--   (2) misc_courting_emotes.rs2 hooks ~emote_perform: the Clap leg (after
--       giving flowers, %misc_affection = s1_step1 -> s1_step5) and the
--       Blow Kiss leg (after talkBrand3's "Truly?", s3_step4 -> s3_step0)
--       both require the player to actually PLAY the emote next to Brand
--       (within ^misc_courting_emote_radius of ^misc_brand_coord) -- giving
--       the item alone no longer narrates it. Quest Helper's courtBrand
--       ladder names both as their own steps (clapForBrand, blowKissToBrand).
--
-- Items: flowers (misc_flowergirl, 15gp, bought live below) are the only
-- courting/anthem/pen ingredient actually sold in the quest area itself;
-- the cake, ring, iron bar and logs are bring-along materials the way
-- Advisor Ghrim's own rake/pickaxe/axe/harpoon/lobster-pot "reputation
-- item" already is (misc_advisor_ghrim.rs2's own dialogue: "Bring a rake, a
-- pickaxe..."), so they are given in setup rather than driven through a
-- shop trip this content pack has no wired source for.
--
-- 75%-support finish gate: content-parity fix 2026-09-23 landed the real
-- Managing Miscellania resource-collection loop (lumberjack_leif.rs2's
-- leif_intercept_wood, ported from LostCity like weed_herbs.rs2/
-- miner_magnus.rs2/fisherman_frodi.rs2 already were) -- Advisor Ghrim's own
-- "put me to work" no longer sets %misc_approval straight to threshold, it
-- only narrates ("Rake the herb patches ... work near any of them and the
-- kingdom's stock grows, and so does your approval.") and Quest Helper's
-- own guide (ThroneOfMiscellania.java's finishOff/get75Support step, stage
-- 90) has NO talk-to-Ghrim leg at this point at all -- it goes straight
-- from king_signed_treaty to the chop/mine/rake/fish activity, then to King
-- Vargas. This file follows the guide: no Ghrim visit here. The player
-- chops the kingdom's maples for real (click_loc("mapletree") beside
-- Lumberjack Leif, Woodcutting 45 + an axe, both set up below) until
-- %misc_approval is demonstrably moving, then ::misc_earnapproval -- the
-- sanctioned GRIND fast-forward for this exact loop (docs/QUEST_SERVER_CHEATS.md)
-- -- finishes it to the 75% threshold (96 of 127); its own guard requires
-- the same real preconditions (stage, inzone, level, axe) the chop already
-- proved, and its effect is read back (t.msg.expect + var.await_server).
--
-- Reward: quest_misc's own ~quest_complete_rewards call lists "10000
-- coins|Management of Miscellania|Ring of wealth teleport to Miscellania"
-- as scroll text, and the FIRST of those three is a real grant this pack
-- makes: misc_king_vargas.rs2's [label,vargas_finish_quest] runs
-- `%misc_coffers = add(%misc_coffers, 10000)` on the line directly above
-- `queue(misc_quest_complete, 0, 0)`. That is the kingdom's coffer, not the
-- player's backpack -- but it is a DECLARED varbit (configs/all.varbit's
-- [misc_coffers], basevar misc_varbit_2, startbit 0 endbit 26;
-- all.varbit.compack 74=misc_coffers), so t.var.server reads it like any
-- other and the grant is asserted below (reward.coffers: the reading taken
-- immediately before the crowning click, plus exactly 10000). The other two
-- scroll lines really are text only -- nothing in the quest_misc tree
-- inv_adds a ring or grants skill xp, and the scaffold's Quest Helper banner
-- agrees (0 experience, 0 item, quest points 1) -- so those two are graded by
-- the scroll's own literal lines (scroll.rewardLines) and the 1 quest point
-- quest.expect_complete()'s quest.points row already checks.

return {
    id = "misc",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000, -- every castle visit is walked: two castles, ~16 entries and exits
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so everything below fits
        "::give coins 20", -- misc_flowergirl wants 15gp for three flowers
        "::give iron_bar 1", -- Derrik forges the giant nib from this (misc_smithy.rs2)
        "::give logs 1", -- combined with the nib to make the giant pen (misc_giant_nib.rs2)
        "::give gold_ring 1", -- Brand's third courting gift (misc_prince_brand.rs2's opnpcu ring case)
        "::give cake 1", -- Brand's second courting gift (opnpcu cake case)
        "::setlevel woodcutting 45", -- the maple row's own level gate (woodcutting_trees), also Ghrim's reputation-tool level for the real support grind
        "::give bronze_axe 1", -- Advisor Ghrim's reputation item AND the real axe ::misc_earnapproval's ~woodcutting_axe_checker needs
        "::complete quest_heroes", -- misc_door_guard.rs2's hard gate: %heroquest = ^hero_complete
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp359_misc_quest",
            constants = {
                not_started = 0,
                talked_to_king = 10,
                talked_to_queen = 20,
                queen_requests_recognition = 30,
                need_bard_for_anthem = 40,
                prince_composed_anthem = 50,
                advisor_corrected_anthem = 60,
                queen_gave_treaty = 70,
                gave_king_treaty = 80,
                king_signed_treaty = 90,
                complete = 100, -- managing_miscellania.constant:6, not the quest_misc.constant file (that one stops at 90)
            },
            row = "quest_throneofmiscellania", -- all.dbrow.compack:148
            display = "Throne of Miscellania",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::give, ::complete) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- Helpers: every crossing is walked and read back.
        -- ---------------------------------------------------------------
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- Cross one swinging door on foot. Walk to the near side and check
        -- the tile. Probe THIS level's closed leaf by tile AND level
        -- (click_loc's `at` selector presses only that copy and answers
        -- no_row, pressing nothing, when it is not there -- the castles stack
        -- a castledoor copy on level 0 and level 1 at the same tile, and
        -- loc_near returns the first copy on any level). no_row means an
        -- earlier press left this door open (a door swings back after 500
        -- ticks): assert the open leaf stands within one tile of the door
        -- tile -- a row that fails when it does not -- and walk through
        -- without pressing it again. Then walk to the far tile and check it
        -- exactly: a door that did not open stops that walk at the wall.
        -- Is an open leaf of `open_sym` standing on `level` within one tile of
        -- the door tile? loc_near cannot say: it answers the first copy on ANY
        -- level, and here that is the level-0 leaf straight below a level-1
        -- door (run 4). So read the scene's copies with their levels: a loc
        -- selector naming a plane that does not exist (9) matches no copy,
        -- presses nothing, and answers no_row listing the nearest copies as
        -- x,z,level (QD.player._loc_copy) -- read-only.
        local function leaf_on_level(open_sym, door_x, door_z, level)
            local r, d = t.player.click_loc(open_sym, 1, { at = { door_x, door_z, 9 } })
            local listed = (r == "no_row" and tostring(d):match("nearest copies: ([^)]*)%)")) or nil
            if listed == nil then
                return false, nil, "probe answered " .. tostring(r) .. " " .. tostring(d)
            end
            for xs, zs, ls in listed:gmatch("(%d+),(%d+),(%-?%d+)") do
                local x, z, l = tonumber(xs), tonumber(zs), tonumber(ls)
                if l == level and math.abs(x - door_x) <= 1 and math.abs(z - door_z) <= 1 then
                    return true, { x = x, z = z, level = l }, listed
                end
            end
            return false, nil, listed
        end

        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, level, near_x, near_z, far_x, far_z)
            t.player.walk_to(near_x, near_z, 60)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.x == near_x and nt.z == near_z and nt.level == level,
                "walked to " .. near_x .. "," .. near_z .. "," .. level .. " beside " .. closed_sym .. " at "
                    .. door_x .. "," .. door_z .. "," .. level .. " -> " .. tile_text(nr, nt))
            local pr, pd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z, level } })
            local open_ok, leaf, leaves
            local waited = 0
            while pr == "no_row" do
                open_ok, leaf, leaves = leaf_on_level(open_sym, door_x, door_z, level)
                if open_ok or waited >= 10 then
                    break
                end
                -- Neither leaf in the client's scene yet: give the scene a
                -- few ticks to catch up, then probe the closed leaf again.
                t.ticks(2)
                waited = waited + 2
                pr, pd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z, level } })
            end
            if waited > 0 then
                t.note(prefix .. ": neither leaf of " .. closed_sym .. " at " .. door_x .. "," .. door_z .. "," .. level
                    .. " was in the scene on arrival; waited " .. waited .. " tick(s), probe now " .. tostring(pr))
            end
            if pr == "no_row" then
                t.check(prefix .. ".doorStandsOpen", open_ok,
                    "no closed " .. closed_sym .. " at " .. door_x .. "," .. door_z .. "," .. level .. " (" .. tostring(pd)
                        .. "); " .. open_sym .. ": " .. (open_ok and ("open leaf at " .. leaf.x .. "," .. leaf.z .. "," .. leaf.level)
                        or ("no copy on level " .. level .. " within 1 of the door tile; the scene's nearest copies: " .. leaves))
                        .. " (want the open leaf on this level within 1 of the door tile: left open by an earlier press,"
                        .. " walked through, not pressed again)")
            else
                -- A door says nothing when it opens, so click_loc may answer
                -- timeout although it swung; the walk below is the proof.
                t.check(prefix .. ".openDoor", pr == "ok" or pr == "timeout",
                    "click_loc(" .. closed_sym .. ", 1, at " .. door_x .. "," .. door_z .. "," .. level .. ") -> "
                        .. tostring(pr) .. " " .. tostring(pd))
                t.ticks(1)
            end
            local wr, wd = t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and ft.x == far_x and ft.z == far_z and ft.level == level,
                "walked through " .. closed_sym .. " " .. door_x .. "," .. door_z .. "," .. level .. " to " .. far_x .. ","
                    .. far_z .. " -> " .. tile_text(fr, ft) .. " (walk " .. tostring(wr) .. (wd and (" " .. tostring(wd)) or "") .. ")")
        end

        -- A click that TELEPORTS the player (a stair, the throne room's
        -- walk-through door): walk to the exact stand tile (a maplink is keyed
        -- on the player's own tile, and the throne door picks its side from
        -- it), click the copy by tile and level, wait for the landing, and
        -- grade the row on the tiles before and after -- a short hop can
        -- answer `timeout settle_after_click` although it landed, so the
        -- click's answer is in the detail and the landing decides.
        local function hop(name, sym, op, at_x, at_z, at_level, stand_x, stand_z, want_x, want_z, want_level)
            t.player.walk_to(stand_x, stand_z, 60)
            local br, bt = t.world.tile()
            local stood = br == "ok" and bt.x == stand_x and bt.z == stand_z and bt.level == at_level
            local cr, cd = t.player.click_loc(sym, op, { at = { at_x, at_z, at_level } })
            t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and tt.x == want_x and tt.z == want_z and tt.level == want_level
                end,
                note = name .. ": waiting for the landing",
            }, 12)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and stood
                    and wr == "ok" and wt.x == want_x and wt.z == want_z and wt.level == want_level,
                "from " .. tile_text(br, bt) .. " (stand tile " .. stand_x .. "," .. stand_z .. "," .. at_level .. ") click_loc("
                    .. sym .. ", " .. op .. ", at " .. at_x .. "," .. at_z .. "," .. at_level .. ") -> " .. tostring(cr) .. " "
                    .. tostring(cd) .. "; landed " .. tile_text(wr, wt) .. " (want " .. want_x .. "," .. want_z .. "," .. want_level .. ")")
        end

        local CD, CDO = "castledoor", "opencastledoor"

        -- Miscellania castle: from the open ground outside the east door up
        -- to the level-1 south corridor (2506,3853,1), and back down and out.
        local function castle_in(p)
            pass_door(p .. ".eastDoor", CD, CDO, 2510, 3860, 0, 2511, 3860, 2509, 3860)
            pass_door(p .. ".hallDoor", CD, CDO, 2505, 3860, 0, 2506, 3860, 2504, 3860)
            pass_door(p .. ".stairRoomDoor", CD, CDO, 2506, 3851, 0, 2506, 3852, 2506, 3850)
            hop(p .. ".stairsUp", "spiralstairs_wooden", 1, 2505, 3848, 0, 2505, 3850, 2504, 3849, 1)
            pass_door(p .. ".landingDoor", CD, CDO, 2506, 3851, 1, 2506, 3851, 2506, 3853)
        end
        local function castle_out(p)
            pass_door(p .. ".landingDoor", CD, CDO, 2506, 3851, 1, 2506, 3852, 2506, 3850)
            hop(p .. ".stairsDown", "spiralstairsmiddle_wooden", 3, 2505, 3848, 1, 2504, 3849, 2505, 3850, 0)
            pass_door(p .. ".stairRoomDoor", CD, CDO, 2506, 3851, 0, 2506, 3851, 2506, 3853)
            pass_door(p .. ".hallDoor", CD, CDO, 2505, 3860, 0, 2504, 3860, 2506, 3860)
            pass_door(p .. ".eastDoor", CD, CDO, 2510, 3860, 0, 2510, 3860, 2512, 3860)
        end
        -- The castle's NORTH way up: castledoor 2506,3869 (both levels), the
        -- north spiralstairs_wooden 2505,3871 (maplink.dbrow 0_39_60_10_30 up
        -- -> 2507,3871,1), the north corridor, and the throne room's north
        -- door misc_ulby_throneroomdoor 2506,3863 (pressed from 2506,3864,
        -- whose z is not the door's, so ~check_axis lands the player on the
        -- door tile inside). Used for the crowning visit: see there.
        local function castle_in_north(p)
            pass_door(p .. ".eastDoor", CD, CDO, 2510, 3860, 0, 2511, 3860, 2509, 3860)
            pass_door(p .. ".hallDoor", CD, CDO, 2505, 3860, 0, 2506, 3860, 2504, 3860)
            pass_door(p .. ".northStairRoomDoor", CD, CDO, 2506, 3869, 0, 2506, 3868, 2506, 3870)
            hop(p .. ".northStairsUp", "spiralstairs_wooden", 1, 2505, 3871, 0, 2506, 3870, 2507, 3871, 1)
            pass_door(p .. ".northLandingDoor", CD, CDO, 2506, 3869, 1, 2506, 3869, 2506, 3867)
            hop(p .. ".northThroneDoorIn", "misc_ulby_throneroomdoor", 1, 2506, 3863, 1, 2506, 3864, 2506, 3863, 1)
        end
        -- Brand's room, from and to the south corridor.
        local function brand_in(p)
            pass_door(p .. ".brandDoor", CD, CDO, 2504, 3853, 1, 2505, 3853, 2503, 3853)
        end
        local function brand_out(p)
            pass_door(p .. ".brandDoor", CD, CDO, 2504, 3853, 1, 2504, 3853, 2506, 3853)
        end
        -- The throne room's south door: a walk-through teleport. Pressed from
        -- the corridor tile 2506,3856 (z != the door's, so ~check_axis puts
        -- the player on the door tile inside); pressed from 2505,3857 inside
        -- (the door's own z, so it puts him on 2506,3856 outside).
        local function throne_in(p)
            hop(p .. ".throneDoorIn", "misc_ulby_throneroomdoor", 1, 2506, 3857, 1, 2506, 3856, 2506, 3857, 1)
        end
        local function throne_out(p)
            hop(p .. ".throneDoorOut", "misc_ulby_throneroomdoor", 1, 2506, 3857, 1, 2505, 3857, 2506, 3856, 1)
        end
        -- Etceteria castle: from the open ground west of its front door up to
        -- Queen Sigrid's room (2615,3872,1), and back down and out.
        local function etc_in(p)
            pass_door(p .. ".etcFrontDoor", CD, CDO, 2608, 3875, 0, 2607, 3875, 2608, 3875)
            pass_door(p .. ".etcHallDoor", CD, CDO, 2609, 3875, 0, 2609, 3875, 2610, 3875)
            pass_door(p .. ".etcStairRoomDoor", CD, CDO, 2615, 3870, 0, 2615, 3871, 2615, 3869)
            hop(p .. ".etcStairsUp", "spiralstairs", 1, 2613, 3867, 0, 2615, 3868, 2615, 3867, 1)
            pass_door(p .. ".etcSigridDoor", CD, CDO, 2615, 3870, 1, 2615, 3870, 2615, 3872)
        end
        local function etc_out(p)
            pass_door(p .. ".etcSigridDoor", CD, CDO, 2615, 3870, 1, 2615, 3871, 2615, 3869)
            hop(p .. ".etcStairsDown", "spiralstairstop", 1, 2614, 3867, 1, 2615, 3867, 2614, 3866, 0)
            -- far tile 2615,3871: the opened leaf stands on that tile and
            -- walls its north edge, so the walk on goes round it.
            pass_door(p .. ".etcStairRoomDoor", CD, CDO, 2615, 3870, 0, 2615, 3870, 2615, 3871)
            pass_door(p .. ".etcHallDoor", CD, CDO, 2609, 3875, 0, 2610, 3875, 2609, 3875)
            pass_door(p .. ".etcFrontDoor", CD, CDO, 2608, 3875, 0, 2608, 3875, 2606, 3875)
        end
        -- Overland hops between open level-0 tiles of the island.
        local function to_castle(name)
            t.exec(name, t.player.goto_tile, 2512, 3860, 0)
        end
        local function to_etceteria(name)
            t.exec(name, t.player.goto_tile, 2606, 3875, 0)
        end

        -- ---------------------------------------------------- King Vargas: offer
        -- The voyage (see the header): the dock, then overland to the castle.
        t.exec("goto-dock", t.player.goto_tile, 2581, 3845, 0)
        to_castle("goto-castle1")
        castle_in("vargas1")
        -- First press of the throne door before the quest starts: the guard's
        -- challenge (misc_door_guard.rs2 [label,doornotyetpass], which sets
        -- %misc_grantedaudience = 1); the second press walks through.
        t.player.walk_to(2506, 3856, 30)
        t.exec("vargas1.throneDoorGuard", t.player.click_loc, "misc_ulby_throneroomdoor", 1, { at = { 2506, 3857, 1 } })
        t.exec("vargas1.throneDoorGuard-dialog", t.chat.play, {
            "npc:Halt! Who goes there?",
            "player:My name is",
            "npc:I'm afraid the King won't give an audience",
            "player:I am a member of the Heroes' Guild",
            "npc:Then you may pass.",
        })
        throne_in("vargas1")
        t.exec("talkVargas1", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas1-dialog", t.chat.play, {
            "player:You wanted to see me, Your Maj",
            "npc:Ah, yes. I am cursed -- I cann",
            "npc:My children Brand and Astrid c",
            "choose:I'll try to win over Prince Brand.",
            "player:I'll try to win over Prince Br",
            "npc:Brand can usually be found ups",
        })

        -- The choice above ("I'll try to win over Prince Brand.") is
        -- mutually exclusive with the Astrid branch (misc_king_vargas.rs2's
        -- own opt=1/opt=2 [opnpc1,misc_king_vargas] choice, %misc_partner_multivar
        -- 1 vs 0) -- Quest Helper's courtAstrid ladder can never run in the
        -- same playthrough as courtBrand, so its eight steps are declared
        -- here rather than driven.
        -- BRANCH-IN: misc_astrid talkAstrid1 Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_princess_astrid.rs2:46)
        -- BRANCH-IN: misc_astrid giveFlowersToAstrid Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_princess_astrid.rs2:103)
        -- BRANCH-IN: misc_astrid danceForAstrid Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_courting_emotes.rs2:50)
        -- BRANCH-IN: misc_astrid talkAstrid2 Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_princess_astrid.rs2:64)
        -- BRANCH-IN: misc_astrid giveBowToAstrid Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_princess_astrid.rs2:114)
        -- BRANCH-IN: misc_astrid talkAstrid3 Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_princess_astrid.rs2:77)
        -- BRANCH-IN: misc_astrid blowKissToAstrid Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_courting_emotes.rs2:57)
        -- BRANCH-IN: misc_astrid useRingOnAstrid Brand was courted here, the mutually exclusive choice; misc_astrid.lua drives the Astrid branch (misc_princess_astrid.rs2:122)

        -- ---------------------------------------------------- courting Brand
        throne_out("brand1")
        brand_in("brand1")
        -- content-parity fix: Vargas now sets %misc_affection to
        -- not_started (not step0), so this FIRST visit reads brand_talk1,
        -- the five-line courtship intro (Quest Helper's own talkBrand1 step).
        t.exec("talkBrand1", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("talkBrand1-dialog", t.chat.play, {
            "player:Be still, my heart -- that's quite",
            "npc:You will be the greatest patron",
            "player:How poetic.",
            "npc:They don't understand your poetry",
            "npc:It's kind of you to listen. If you",
        })

        brand_out("flowergirl")
        castle_out("flowergirl")
        -- The flower girl stands in the open a few tiles north-east of the
        -- castle's east door (m39_60.spawn 2514,3866,0): walked.
        local fg_walk = t.player.walk_to(2514, 3864, 30)
        local fgr, fgt = t.world.tile()
        t.check("flowergirl.walked", fgr == "ok" and fgt.level == 0 and math.abs(fgt.x - 2514) <= 1 and math.abs(fgt.z - 3864) <= 1,
            "walk_to 2514,3864 -> " .. tostring(fg_walk) .. ", at " .. tile_text(fgr, fgt))
        t.exec("buyFlowers", t.player.talk_to, "misc_flowergirl", 1)
        t.exec("buyFlowers-dialog", t.chat.play, {
            "npc:Hello.",
            "player:Good day. What are you doing?",
            "npc:I'm selling flowers, 15gp for three",
            "choose:Yes, please.",
            "player:Yes, please",
            "npc:Thank you! Here you go.",
        })
        local flowers_await_result, flowers_await_detail = t.inv.await("flowers_waterfall_quest", 1, 10)
        t.check("inv.gotFlowers", flowers_await_result == "ok",
            "inv.await(flowers_waterfall_quest,1) -> " .. tostring(flowers_await_result) .. " " .. tostring(flowers_await_detail))

        -- Back to the east door on foot, in, up, and into Brand's room.
        t.player.walk_to(2512, 3860, 30)
        castle_in("brand2")
        brand_in("brand2")
        -- misc_prince_brand.rs2's opnpcu flowers case ends in a plain mes()
        -- line (a chat-log message, not a ~mesbox), so use_on's own settle
        -- (new chat line / backpack change) is the whole of the row -- it
        -- only gets Brand to ASK for the Clap now, it does not perform it.
        local brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("giveFlowersBrand", t.player.use_on, "flowers_waterfall_quest", brand)
        -- inv.await(name, 0, ticks) never actually waits (total >= 0 is
        -- always true -- QUEST_AUTHORING.md's gaps section) -- ticks then a
        -- direct count read is the real "wait for it to be consumed".
        t.ticks(2)
        local flowers_gone_result, flowers_gone_count = t.inv.count("flowers_waterfall_quest")
        t.check("giveFlowersBrand.consumed", flowers_gone_result == "ok" and flowers_gone_count == 0,
            "inv.count(flowers_waterfall_quest) -> " .. tostring(flowers_gone_result) .. " " .. tostring(flowers_gone_count))

        -- Content-parity leg: brand_need_clap -> the player must actually
        -- play Clap next to Brand (misc_courting_emotes.rs2's
        -- ~misc_emote_performed_brand, hooked off ~emote_perform), s1_step1
        -- (11) -> s1_step5 (15). Quest Helper's own clapForBrand step.
        t.exec("clapForBrand", t.player.emote, "clap")
        t.expect("affection.s1_step5", t.var.expect("varb73_misc_affection", 15))

        brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("talkBrand2", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("talkBrand2-dialog", t.chat.play, {
            "player:A much nobler pursuit than swordplay",
            "npc:How inspiring to hear you say so!",
            "player:How poetic.",
            "npc:I'm glad someone appreciates it. Have you brought",
        })

        brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("giveCakeBrand", t.player.use_on, "cake", brand)
        t.ticks(2)
        local cake_gone_result, cake_gone_count = t.inv.count("cake")
        t.check("giveCakeBrand.consumed", cake_gone_result == "ok" and cake_gone_count == 0,
            "inv.count(cake) -> " .. tostring(cake_gone_result) .. " " .. tostring(cake_gone_count))

        -- brand_talk3 now ENDS at "Truly?" (s2_step4 -> s3_step4) -- the
        -- kiss itself is the emote leg below, not narrated inline any more.
        t.exec("talkBrand3", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("talkBrand3-dialog", t.chat.play, {
            "player:I'm glad to hear it's going so well.",
            "npc:I wouldn't presume to have the skill",
            "player:That was lovely. I'm touched!",
            "npc:Truly?",
        })

        -- Content-parity leg: brand_need_kiss -> the player must actually
        -- play Blow Kiss next to Brand, s3_step4 -> s3_step0 (30). Named
        -- after Quest Helper's own blowKissToBrand step (helper_coverage.py
        -- matches a ledger row to a guide step by name).
        t.exec("blowKissToBrand", t.player.emote, "blow kiss")
        t.expect("affection.s3_step0", t.var.expect("varb73_misc_affection", 30))

        brand = t.player.by_symbol("npc", "misc_prince_brand")
        t.exec("giveRingBrand", t.player.use_on, "gold_ring", brand)
        t.exec("giveRingBrand-dialog", t.chat.play, {
            "player:Prince Brand, will you vouch for me",
            "npc:I will -- and gladly.",
        })
        -- The ring case inv_dels the ring it was handed (misc_prince_brand.rs2
        -- opnpcu gold_ring branch, `inv_del(inv, last_useitem, 1)`).
        t.ticks(2)
        local ring_gone_result, ring_gone_count = t.inv.count("gold_ring")
        t.check("giveRingBrand.consumed", ring_gone_result == "ok" and ring_gone_count == 0,
            "inv.count(gold_ring) -> " .. tostring(ring_gone_result) .. " " .. tostring(ring_gone_count))
        -- misc_acceptedtorule is the varBIT the ring case sets (opnpcu
        -- misc_prince_brand, gold_ring branch) -- proven live by the very
        -- next row below, Vargas's "Wonderful!" branch, which only fires
        -- when it reads 1 (misc_king_vargas.rs2's %misc_acceptedtorule
        -- check); a direct var poll here is redundant with that.

        -- ---------------------------------------------------- back to Vargas
        brand_out("vargas2")
        throne_in("vargas2")
        t.exec("talkVargas2", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas2-dialog", t.chat.play, {
            "npc:Wonderful! Now, let us discuss securing peace",
        })
        t.expect("quest.stage.talked_to_king", t.quest.expect_stage("talked_to_king"))

        -- ---------------------------------------------------- Etceteria diplomacy
        throne_out("sigrid1")
        castle_out("sigrid1")
        to_etceteria("goto-etceteria1")
        etc_in("sigrid1")
        t.exec("talkSigrid1", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("talkSigrid1-dialog", t.chat.play, {
            "player:King Vargas sent me to discuss peace",
            "npc:Peace? Only if Vargas is willing to formally",
        })
        t.expect("quest.stage.talked_to_queen", t.quest.expect_stage("talked_to_queen"))

        etc_out("vargas3")
        to_castle("goto-castle2")
        castle_in("vargas3")
        throne_in("vargas3")
        t.exec("talkVargas3", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas3-dialog", t.chat.play, {
            "player:Queen Sigrid wants you to recognise Etceteria",
            "npc:Recognise Etceteria? After the insults",
        })
        t.expect("quest.stage.queen_requests_recognition", t.quest.expect_stage("queen_requests_recognition"))

        throne_out("sigrid2")
        castle_out("sigrid2")
        to_etceteria("goto-etceteria2")
        etc_in("sigrid2")
        t.exec("talkSigrid2", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("talkSigrid2-dialog", t.chat.play, {
            "player:King Vargas says he'll recognise Etceteria",
            "npc:A new anthem? Our anthem is a fine old song",
        })
        t.expect("quest.stage.need_bard_for_anthem", t.quest.expect_stage("need_bard_for_anthem"))

        -- ---------------------------------------------------- the anthem
        etc_out("brand3")
        to_castle("goto-castle3")
        castle_in("brand3")
        brand_in("brand3")
        t.exec("getAnthem", t.player.talk_to, "misc_prince_brand", 1)
        t.exec("getAnthem-dialog", t.chat.play, {
            "player:King Vargas mentioned you fancy yourself a bit of a bard",
            "npc:A bard! Yes, I've always fancied myself",
            "npc:There! A masterpiece, if I do say so",
        })
        t.expect("quest.stage.prince_composed_anthem", t.quest.expect_stage("prince_composed_anthem"))
        local awful_result, awful_detail = t.inv.await("misc_awful_anthem", 1, 10)
        t.check("inv.gotAwfulAnthem", awful_result == "ok",
            "inv.await(misc_awful_anthem,1) -> " .. tostring(awful_result) .. " " .. tostring(awful_detail))

        -- Advisor Ghrim stands in the throne room beside the king
        -- (m39_60.spawn 2499,3857,1).
        brand_out("ghrim1")
        throne_in("ghrim1")
        t.exec("correctAnthem", t.player.talk_to, "misc_advisor_ghrim", 1)
        t.exec("correctAnthem-dialog", t.chat.play, {
            "player:Prince Brand wrote this anthem for Etceteria",
            "npc:Let me see that. ...Oh dear.",
            "npc:There. A vast improvement, if I may say so.",
        })
        t.expect("quest.stage.advisor_corrected_anthem", t.quest.expect_stage("advisor_corrected_anthem"))
        local good_result, good_detail = t.inv.await("misc_good_anthem", 1, 10)
        t.check("inv.gotGoodAnthem", good_result == "ok",
            "inv.await(misc_good_anthem,1) -> " .. tostring(good_result) .. " " .. tostring(good_detail))

        throne_out("sigrid3")
        castle_out("sigrid3")
        to_etceteria("goto-etceteria3")
        etc_in("sigrid3")
        t.exec("giveAnthemToSigrid", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("giveAnthemToSigrid-dialog", t.chat.play, {
            "player:Advisor Ghrim has finished the new anthem.",
            "npc:Why, this is rather good! Very well",
        })
        t.expect("quest.stage.queen_gave_treaty", t.quest.expect_stage("queen_gave_treaty"))
        local treaty_result, treaty_detail = t.inv.await("misc_treaty", 1, 10)
        t.check("inv.gotTreaty", treaty_result == "ok",
            "inv.await(misc_treaty,1) -> " .. tostring(treaty_result) .. " " .. tostring(treaty_detail))

        -- ---------------------------------------------------- the treaty and the pen
        etc_out("vargas4")
        to_castle("goto-castle4")
        castle_in("vargas4")
        throne_in("vargas4")
        t.exec("giveTreatyToVargas", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("giveTreatyToVargas-dialog", t.chat.play, {
            "player:Queen Sigrid has agreed to the treaty.",
            "npc:At last! I'll sign this gladly",
        })
        t.expect("quest.stage.gave_king_treaty", t.quest.expect_stage("gave_king_treaty"))

        -- Derrik (misc_smithy, m39_60.spawn 2551,3897,0) works inside his
        -- house: overland to the open ground south of its door, then in.
        throne_out("derrik")
        castle_out("derrik")
        t.exec("goto-derrik", t.player.goto_tile, 2551, 3891, 0)
        pass_door("derrik.door", "viking_abode_door", "viking_abode_door_open", 2551, 3893, 0, 2551, 3893, 2551, 3895)
        -- Close it behind you. The opened leaf swings onto 2551,3894 and walls
        -- that tile off from the nook east of it (2552,3894, between the
        -- crates and the wall), where Derrik often stands: with the door open
        -- every press on him answers "I can't reach that!" (run 3, shot 340).
        local close_result, close_detail = t.player.click_loc("viking_abode_door_open", 1, { at = { 2551, 3894, 0 } })
        t.ticks(1)
        local shut_result, shut = t.world.loc_near("viking_abode_door", 2)
        t.check("derrik.closeDoor", (close_result == "ok" or close_result == "timeout")
                and shut_result == "ok" and shut.tile_x == 2551 and shut.tile_z == 3893,
            "click_loc(viking_abode_door_open, 1, at 2551,3894,0) -> " .. tostring(close_result) .. " " .. tostring(close_detail)
                .. "; closed leaf: " .. (shut_result == "ok" and (shut.tile_x .. "," .. shut.tile_z .. "," .. tostring(shut.level)) or tostring(shut_result))
                .. " (want back on 2551,3893)")
        -- Derrik wanders his cramped house (fire, anvil and crates fill half
        -- of it). A press that still answers "I can't reach that!" is pressed
        -- again a few ticks later, up to six times, one graded row.
        local forge_result, forge_detail, forge_tries = nil, nil, 0
        repeat
            forge_tries = forge_tries + 1
            forge_result, forge_detail = t.player.talk_to("misc_smithy", 1)
            if forge_result ~= "ok" then
                t.ticks(4)
            end
        until forge_result == "ok" or forge_tries >= 6
        t.check("forgeNib", forge_result == "ok",
            "talk_to(misc_smithy, 1) -> " .. tostring(forge_result) .. " " .. tostring(forge_detail)
                .. " on press " .. forge_tries .. " of at most 6")
        t.exec("forgeNib-dialog", t.chat.play, {
            "player:I have a slightly strange request",
            "npc:Let's see what we can do.",
            "npc:There you are. You'll need to fix that",
        })
        local nib_result, nib_detail = t.inv.await("misc_giant_nib", 1, 10)
        t.check("inv.gotNib", nib_result == "ok",
            "inv.await(misc_giant_nib,1) -> " .. tostring(nib_result) .. " " .. tostring(nib_detail))

        -- misc_giant_nib.rs2's opheldu combine is a single mes() line too --
        -- no dialogue to continue_() through, just an inventory change.
        t.exec("makePen", t.player.use_item_on_item, "misc_giant_nib", "logs")
        local pen_result, pen_detail = t.inv.await("misc_giant_pen", 1, 10)
        t.check("inv.gotPen", pen_result == "ok",
            "inv.await(misc_giant_pen,1) -> " .. tostring(pen_result) .. " " .. tostring(pen_detail))

        pass_door("vargas5.derrikDoor", "viking_abode_door", "viking_abode_door_open", 2551, 3893, 0, 2551, 3894, 2551, 3892)
        to_castle("goto-castle5")
        castle_in("vargas5")
        throne_in("vargas5")
        t.exec("giveVargasPen", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("giveVargasPen-dialog", t.chat.play, {
            "npc:A giant pen! Now I can sign in a manner",
        })
        t.expect("quest.stage.king_signed_treaty", t.quest.expect_stage("king_signed_treaty"))

        -- ---------------------------------------------------- earning 75% support
        -- Quest Helper's own guide (ThroneOfMiscellania.java, stage-90
        -- finishOff/get75Support) has NO talk-to-Ghrim leg here -- it goes
        -- straight from king_signed_treaty to the chop/mine/rake/fish
        -- activity, then to King Vargas. misc_advisor_ghrim.rs2's own
        -- "put me to work" reply is narration only now (content-parity fix
        -- 2026-09-23) and sets nothing, so a visit here would be a driven
        -- row for a step the guide does not have -- skipped, following the
        -- guide.
        --
        -- Real leg: chop the kingdom's maples for real beside Lumberjack
        -- Leif (misc_dummy_mapletree is a non-choppable decoy -- the real
        -- placed trees are the ordinary "mapletree" loc, category 222,
        -- resolved by db_find(woodcutting_trees:tree, mapletree) in both
        -- the real woodcut.rs2 chop and ::misc_earnapproval's own loop).
        -- Woodcutting 45 + an axe are set up above; every successful swing
        -- inside the kingdom zone runs the real leif_intercept_wood
        -- (misc_approval +1, real Woodcutting xp, the log to Leif instead
        -- of the backpack) -- click_loc starts the real repeating chop
        -- action (oploc1 -> oploc3 resume, woodcut.rs2), and the approval
        -- read proves it actually landed before the grind cheat is called.
        throne_out("leif")
        castle_out("leif")
        t.exec("goto-leif", t.player.goto_tile, 2550, 3866, 0)
        -- Level 45 / bronze axe measures ~6% success per swing (4-tick
        -- cadence, misc_debug.rs2's own loop: 1477-1520 swings for 96
        -- successes across two runs). A single click_loc starts a
        -- repeating chop action (oploc1 -> oploc3 resume, woodcut.rs2)
        -- that keeps swinging on its own -- but get_logs rolls a 1-in-8
        -- deplete chance on EVERY successful swing, and a deplete stops
        -- the resume outright (loc_change to the stump stage, no further
        -- p_oploc(3) is issued): after the npc wander parity landing moved
        -- the shared RNG stream (seam15, 2026-09-25), the very tree being
        -- chopped can now fall before three approval points land -- a
        -- single flat 900-tick await read 2 of 3 and then sat idle for the
        -- rest of the wait, because the action had stopped, not slowed.
        -- A longer wait cannot fix a stopped action, so re-find and
        -- re-click "mapletree" (the symbol resolves to whichever live
        -- copy is nearest -- Leif's grove holds a dozen within one map
        -- square, m39_60.jl2, so a depleted tree is never the only one
        -- left) each time the current session ends short of the target,
        -- awaiting one more approval point than is already banked.
        local chop_attempts = 0
        local approval_result, approval_value = t.var.server("varb72_misc_approval")
        while (approval_result ~= "ok" or (approval_value or 0) < 3) and chop_attempts < 10 do
            chop_attempts = chop_attempts + 1
            local before_result, before_value = t.var.server("varb72_misc_approval")
            t.exec("chopMaple-" .. chop_attempts, t.player.click_loc, "mapletree", 1)
            t.var.await_server("varb72_misc_approval", (before_value or 0) + 1, 250)
            approval_result, approval_value = t.var.server("varb72_misc_approval")
        end
        t.check("approval.real_chops", approval_result == "ok" and (approval_value or 0) >= 3,
            "var.server(misc_approval) after " .. tostring(chop_attempts) .. " chopMaple attempt(s) -> "
                .. tostring(approval_result) .. " " .. tostring(approval_value))

        -- ::misc_earnapproval is the sanctioned GRIND fast-forward for
        -- exactly this loop (docs/QUEST_SERVER_CHEATS.md) -- it walks the
        -- real ~leif_intercept_wood body (real stat_random rolls, real
        -- Woodcutting xp, real approval +1 per success) in a guarded loop,
        -- skipping only the swing cadence and the maple's regrowth, until
        -- 96 of 127 (75%). t.cheat is hollow (trap 12): record the call with
        -- t.step, then read its own line back with t.msg.expect (not
        -- t.msg.await -- t.cheat has already consumed the reply, per the
        -- cheat doc's own note) and confirm the server varbit itself.
        local earn_cheat_result = t.cheat("::misc_earnapproval")
        t.step("approval.fast_forward_cheat", earn_cheat_result == "ok" and "PASS" or "FAIL",
            "::misc_earnapproval -> " .. tostring(earn_cheat_result))
        t.exec("approval.fast_forward", t.msg.expect, "You worked for the kingdom")
        -- Named after Quest Helper's own get75Support step ("Reach 75%
        -- support...") -- this is the row that proves it reached.
        t.exec("get75Support", t.var.await_server, "varb72_misc_approval", 96, 10)

        -- ---------------------------------------------------- back to Vargas: the crowning
        -- vargas_check_support reads %misc_approval >= 75% (just reached
        -- above) and falls straight through to vargas_finish_quest in the
        -- same click (no return between the labels) -- one dialogue, one row.
        -- Up the castle's NORTH stairs this time. The south way's doors were
        -- all last opened on the way in to the pen (vargas5), so they swing
        -- shut ~500 ticks later -- while the player climbs back from Leif's
        -- grove -- and the level-1 landing door 2506,3851 then comes back
        -- into the client's scene as NEITHER leaf (no castledoor, no
        -- opencastledoor on level 1) while the server still holds it shut:
        -- an engine resync bug, not game behaviour (runs 4 and 5, rows
        -- vargas6.landingDoor.*; reported). The north way's doors have not
        -- been touched this run, so both ends agree on them.
        to_castle("goto-castle6")
        castle_in_north("vargas6")

        -- The coffer reading taken on the tick BEFORE the crowning click, so
        -- the assertion below is a delta this run measured and not a guess at
        -- a starting balance (a fresh character's %misc_coffers is 0, but the
        -- delta is what vargas_finish_quest's own `add(%misc_coffers, 10000)`
        -- is worth). Read server-side: the coffer varbit is the kingdom's
        -- bookkeeping and nothing transmits it to this client's varp cache.
        local coffers_before_result, coffers_before = t.var.server("varb74_misc_coffers")

        t.exec("talkVargasFinish", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargasFinish-dialog", t.chat.play, {
            "npc:The people trust you, the treaty is signed, and my own children speak well of you. I hereby name you regent of Miscellania!",
        })

        -- vargas_finish_quest writes %misc_quest = ^misc_complete then
        -- queue(misc_quest_complete, 0, 0) -- the varp write and the scroll
        -- paint it queues are not client-visible in the click's own tick
        -- (section 8's completion-is-asynchronous rule).
        t.ticks(3)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))

        -- The 10000gp the scroll's first reward line promises. Same label,
        -- one line above the `%misc_quest = ^misc_complete` the row above
        -- just proved, so a stage of complete and an unmoved coffer would
        -- mean the grant line was skipped.
        local coffers_after_result, coffers_after = t.var.server("varb74_misc_coffers")
        t.check("reward.coffers",
            coffers_before_result == "ok" and coffers_after_result == "ok"
                and type(coffers_before) == "number" and type(coffers_after) == "number"
                and coffers_after == coffers_before + 10000,
            "misc_coffers before the crowning click = " .. tostring(coffers_before)
                .. " (" .. tostring(coffers_before_result) .. "), after = "
                .. tostring(coffers_after) .. " (" .. tostring(coffers_after_result)
                .. "), delta = "
                .. tostring((type(coffers_after) == "number" and type(coffers_before) == "number")
                    and (coffers_after - coffers_before) or "n/a")
                .. ", expected 10000 from misc_king_vargas.rs2's "
                .. "[label,vargas_finish_quest] `%varb74_misc_coffers = add(%varb74_misc_coffers, 10000)`")

        -- The quest's own ~quest_complete_rewards call lists "10000
        -- coins|Management of Miscellania|Ring of wealth teleport to
        -- Miscellania" as SCROLL TEXT only -- misc_king_vargas.rs2's
        -- vargas_finish_quest adds to %misc_coffers (the KINGDOM's, not the
        -- player's) and nothing else in the tree inv_adds or grants xp, so
        -- the scroll's own reward lines are the only real evidence of this
        -- quest's documented reward (file header above). t.scroll.rewards()
        -- awaits the scroll's own mount itself, so quest.expect_complete()'s
        -- quest.scroll shot below photographs a scroll already on screen
        -- (section 8's "settle before expect_complete" rule).
        local rewards_result, rewards_detail = t.scroll.rewards()
        local rewards_text = "nil"
        local rewards_ok = false
        if rewards_result == "ok" and type(rewards_detail) == "table" and type(rewards_detail.lines) == "table" then
            rewards_text = table.concat(rewards_detail.lines, " | ")
            -- The scroll wraps the third reward across two lines ("Ring of
            -- wealth teleport to" / "Miscellania"), so match the two whole
            -- lines the wrap leaves intact rather than the joined phrase.
            rewards_ok = rewards_text:find("10000 coins", 1, true) ~= nil
                and rewards_text:find("Management of Miscellania", 1, true) ~= nil
                and rewards_text:find("Ring of wealth teleport to", 1, true) ~= nil
        end
        t.check("scroll.rewardLines", rewards_ok,
            "scroll.rewards() -> " .. tostring(rewards_result) .. " lines=" .. rewards_text)

        t.quest.expect_complete()
        t.finish(0)
    end,
}
