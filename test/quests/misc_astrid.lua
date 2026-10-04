-- Throne of Miscellania, Astrid path (1 QP). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_misc/scripts/
--   misc_king_vargas.rs2, misc_princess_astrid.rs2, misc_prince_brand.rs2,
--   misc_queen_sigrid.rs2, misc_advisor_ghrim.rs2, misc_giant_nib.rs2,
--   misc_courting_emotes.rs2, misc_door_guard.rs2, misc_debug.rs2
-- and areas/area_miscellania/scripts/{flower_girl,derrik,lumberjack_leif}.rs2
--
-- Second Miscellania test (QUEUE.tsv last_failure): misc.lua already drives
-- the whole quest courting Prince Brand and declares Astrid's eight-step
-- ladder a content gap (mutually exclusive choice, misc_king_vargas.rs2's
-- own opt=1/opt=2 [opnpc1] choice, %misc_partner_multivar 1 vs 0). This file
-- drives the OTHER side: King Vargas is told to court Princess Astrid
-- instead, so the eight courtAstrid steps are real rows here and Brand's
-- eight courtBrand steps are the declared gap (his own bard duty --
-- brand_give_anthem, checked BEFORE the toldking/partner guard in
-- misc_prince_brand.rs2:23 -- fires for either partner, so getAnthem below
-- is still driven, not a gap).
--
-- Travel (door rule, owner 2026-10-03): every door, stair and the throne
-- room's guarded door is clicked on every visit, going in and coming out.
-- The only gotos are overland hops between open outdoor tiles: Lumbridge ->
-- outside the Miscellania castle gate (the first goto of the run; the pack
-- has no boat from Rellekka -- viking_sailor.rs2 only chats, royaltrouble.lua
-- leg 1), and between the castle gate, the Etceteria castle's west door,
-- Derrik's house door and Lumberjack Leif, which the static map joins on foot.
-- The castles' routes (doors read off maps/m39_60 and m40_60, landings from
-- royaltrouble's own selftest ledger) are the helpers below:
--   Miscellania L0: gate castledoor 2510,3860 -> entry hall -> castledoor
--     2505,3860 -> main hall -> castledoor 2506,3851 -> spiralstairs_wooden
--     2505,3848 -> L1 landing -> castledoor 2506,3851 (L1 copy) -> south
--     corridor -> misc_ulby_throneroomdoor 2506,3857 -> throne room (Vargas,
--     Ghrim). The north throne door 2506,3863 opens on the north corridor,
--     where castledoor 2504,3867 is Astrid's room; castledoor 2504,3853 off
--     the south corridor is Brand's.
--   Etceteria L0: castledoor 2608,3875 -> corridor -> castledoor 2611,3866 ->
--     spiralstairs 2613,3867 -> L1 -> castledoor 2615,3870 -> Sigrid.
-- Access: misc_door_guard.rs2 hard-gates the throne room on %heroquest =
-- ^hero_complete (Heroes' Quest, unported -- ::complete quest_heroes in
-- setup); before the quest starts the first press of the throne room door
-- is the guard's "Halt! Who goes there?" chat, which grants the audience.
--
-- Courting partner: Princess Astrid (%misc_partner_multivar = 0). Her ladder
-- (misc_princess_astrid.rs2) is the mirror of Brand's: talk1 (flowers
-- request) -> give flowers (opnpcu, mes() only) -> Dance emote (real,
-- misc_courting_emotes.rs2's ~misc_emote_performed_astrid, content-parity
-- fix 2026-09-23) -> talk2 (bow request) -> give a bow (opnpcu) -> talk3
-- (ends "Truly?") -> Blow Kiss emote (real, same hook) -> give a ring
-- (opnpcu) -> %misc_acceptedtorule = 1.
--
-- Items: flowers (misc_flowergirl, 15gp, bought live below -- first, as the
-- guide's getFlowers step 1.2 does, so Astrid is visited once) are the only
-- courting/anthem/pen ingredient actually sold in the quest area itself;
-- the bow, iron bar, logs and ring are bring-along materials the same way
-- Advisor Ghrim's own reputation item already is (setup, not driven).
--
-- 75%-support finish gate: same real Managing Miscellania resource loop
-- misc.lua drives (lumberjack_leif.rs2's leif_intercept_wood): chop the
-- kingdom's maples for real beside Lumberjack Leif (Woodcutting 45 + an
-- axe, both set up below) until %misc_approval is demonstrably moving, then
-- ::misc_earnapproval -- the sanctioned GRIND fast-forward for exactly this
-- loop (docs/QUEST_SERVER_CHEATS.md) -- finishes it to the 75% threshold;
-- its effect is read back (t.msg.expect + var.await_server).
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
-- other and the grant is asserted below. The other two scroll lines really
-- are text only, graded by the scroll's own literal lines plus the 1 quest
-- point quest.expect_complete()'s quest.points row already checks.

return {
    id = "misc_astrid",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000, -- six round trips into the castle and three to Etceteria, every door and stair walked
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so everything below fits
        "::give coins 20", -- misc_flowergirl wants 15gp for three flowers
        "::give iron_bar 1", -- Derrik forges the giant nib from this (misc_smithy.rs2)
        "::give logs 1", -- combined with the nib to make the giant pen (misc_giant_nib.rs2)
        "::give shortbow 1", -- Astrid's second courting gift (opnpcu bow case, misc_princess_astrid.rs2:114)
        "::give gold_ring 1", -- Astrid's third courting gift (opnpcu ring case, misc_princess_astrid.rs2:122)
        "::setlevel woodcutting 45", -- the maple row's own level gate (woodcutting_trees), also Ghrim's reputation-tool level for the real support grind
        "::give bronze_axe 1", -- the real axe ::misc_earnapproval's ~woodcutting_axe_checker needs
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

        -- ------------------------------------------------ travel helpers
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        -- A box test on the player's tile: level, then x and z ranges.
        local function inside(tt, level, x0, x1, z0, z1)
            return type(tt) == "table" and tt.level == level
                and tt.x >= x0 and tt.x <= x1 and tt.z >= z0 and tt.z <= z1
        end

        -- Wait for a teleport the click queued (a stair climb, the throne
        -- room door's walk-through) to land; the row after reads the tile.
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end

        -- A copy of `sym` on ONE level within `slack` tiles of x,z. t.world.
        -- loc_near answers the first copy of a symbol in the loc pool
        -- whatever its level, and both castles stack a castledoor on levels
        -- 0 and 1 at the same x,z whose open leaves land on the same tile --
        -- so it answered the level-0 leaf to a player on level 1 (run1: open
        -- leaf at 2506,3852,0 for the landing door 2506,3851,1). This reads
        -- the same pool loc_near reads (t.drive._pool_read, charged to the
        -- scan meter the same way) and keeps only a copy on `level`.
        local function loc_on_level(sym, x, z, level, slack)
            local target, sym_result = t.player.by_symbol("loc", sym)
            if not target then
                return false, sym .. ": " .. tostring(sym_result), nil
            end
            local rr, rows = t.drive._pool_read("locs", 5, t.drive._scan_cost_near)
            if rr ~= "ok" or type(rows) ~= "table" then
                return false, "loc pool -> " .. tostring(rr), nil
            end
            t.drive._scan_spend(#rows, t.drive._scan_cost_near)
            local other = {}
            for i = 1, #rows do
                local row = rows[i]
                if (row.loc_id == target.id or row.resolved_loc_id == target.id)
                    and math.abs(row.x - x) <= slack and math.abs(row.z - z) <= slack then
                    if row.level == level then
                        return true, sym .. " at " .. row.x .. "," .. row.z .. "," .. row.level, row
                    end
                    other[#other + 1] = row.x .. "," .. row.z .. "," .. tostring(row.level)
                end
            end
            return false, sym .. ": none within " .. slack .. " of " .. x .. "," .. z .. " on level " .. level
                .. " (other levels: " .. (#other > 0 and table.concat(other, "; ") or "none") .. ")", nil
        end

        -- A door that opens in place (castledoor <-> opencastledoor,
        -- viking_abode_door <-> viking_abode_door_open: doors.loc
        -- next_loc_stage; doors.rs2 ~door_open_active is loc_del(500) of
        -- the closed leaf + loc_add(500) of the open one). Walk to the near
        -- side and check the tile; press the CLOSED copy named by tile AND
        -- level (a closed copy that is not there answers `no_row` and
        -- presses nothing -- then the open leaf must stand within a tile of
        -- the door on the player's own level, a row that fails when neither
        -- leaf is there); walk to the far side and check the tile; then
        -- CLOSE it behind you (below).
        --
        -- Why close it: run1 and run2 both lost the castle gate at
        -- vargas6.castleGateIn. It was pressed open at vargas4, its 500-tick
        -- timer ran out while the player was ~40 tiles east at Derrik's and
        -- Leif's (same scene, no rebuild), and on the way back the client's
        -- loc pool held NEITHER leaf at 2510,3860,0 for six ticks while the
        -- server's door was shut (the walk stopped at 2511,3860). Nothing in
        -- a quest file can bring that copy back, so the test never leaves a
        -- door open to time out: every door it opens it shuts, and the next
        -- visit opens it again.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, level, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and nt.level == level
                and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. "," .. level .. " beside " .. closed_sym .. " at "
                    .. door_x .. "," .. door_z .. "," .. level .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z, level } })
            if cr == "no_row" then
                local open_ok, open_detail = loc_on_level(open_sym, door_x, door_z, level, 1)
                t.check(prefix .. ".doorStandsOpen", open_ok == true,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. "," .. level .. ": " .. tostring(cd)
                        .. "; " .. tostring(open_detail)
                        .. " (want the open leaf within 1 of the door tile on level " .. level
                        .. ": standing open, walked through, not pressed again)")
            else
                t.check(prefix .. ".openDoor", cr == "ok",
                    "click_loc(" .. closed_sym .. " at " .. door_x .. "," .. door_z .. "," .. level .. ") -> " .. tostring(cr) .. " " .. tostring(cd))
                t.ticks(1)
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
            -- Shut it behind you: press the open leaf (op1 Close) on this
            -- level; graded on the closed leaf being back on the door tile on
            -- this level and the player still on the far side.
            local leaf_ok, leaf_detail, leaf = loc_on_level(open_sym, door_x, door_z, level, 1)
            local sr, sd = "not pressed", leaf_detail
            if leaf_ok then
                sr, sd = t.player.click_loc(open_sym, 1, { at = { leaf.x, leaf.z, level } })
                t.ticks(1)
            end
            local shut_ok, shut_detail = loc_on_level(closed_sym, door_x, door_z, level, 0)
            local ar, at = t.world.tile()
            t.check(prefix .. ".closeDoor", leaf_ok and (sr == "ok" or sr == "timeout") and shut_ok and ar == "ok" and far_ok(at),
                "click_loc(" .. open_sym .. " op1 Close) -> " .. tostring(sr) .. " " .. tostring(sd) .. "; " .. tostring(shut_detail)
                    .. "; player " .. tile_text(ar, at) .. " (want the closed leaf back on " .. door_x .. "," .. door_z .. "," .. level
                    .. " and the player still in " .. far_desc .. ")")
        end

        local function castle_door(prefix, door_x, door_z, level, near_x, near_z, far_x, far_z, far_ok, far_desc)
            pass_door(prefix, "castledoor", "opencastledoor", door_x, door_z, level, near_x, near_z, far_x, far_z, far_ok, far_desc)
        end

        -- A spiral staircase: click the named copy, wait for the plane to
        -- change, and check the landing is the staircase's own foot or head
        -- (within 2 tiles of `want_x,want_z`) on `want_level`.
        local function climb(name, sym, op, loc_x, loc_z, loc_level, want_x, want_z, want_level)
            local sr, st = t.world.tile()
            local cr, cd = t.player.click_loc(sym, op, { at = { loc_x, loc_z, loc_level } })
            await_tile(function(tt) return tt.level == want_level end, 12, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and sr == "ok" and st.level == loc_level
                and wr == "ok" and wt.level == want_level
                and math.abs(wt.x - want_x) <= 2 and math.abs(wt.z - want_z) <= 2,
                "from " .. tile_text(sr, st) .. " click_loc(" .. sym .. " op" .. op .. " at " .. loc_x .. "," .. loc_z .. "," .. loc_level
                    .. ") -> " .. tostring(cr) .. " " .. tostring(cd) .. "; landed " .. tile_text(wr, wt)
                    .. " (want within 2 of " .. want_x .. "," .. want_z .. "," .. want_level .. ")")
        end

        -- The throne room door (misc_door_guard.rs2 ~misc_ulby_walk_door):
        -- a walk-through teleport, never left open, so it is pressed on
        -- every crossing. A one-tile walk-through can answer `timeout
        -- settle_after_click` on a crossing that landed (start-and-travel:
        -- "A short hop (stiles) does not trip it"), so the row is graded on
        -- the tiles: not on the far side before the click, on it after.
        local function throne_door(name, door_z, near_x, near_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc("misc_ulby_throneroomdoor", 1, { at = { 2506, door_z, 1 } })
            await_tile(far_ok, 10, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and not far_ok(bt) and wr == "ok" and far_ok(wt),
                "from " .. tile_text(br, bt) .. " click_loc(misc_ulby_throneroomdoor at 2506," .. door_z .. ",1) -> "
                    .. tostring(cr) .. " " .. tostring(cd) .. "; world.tile -> " .. tile_text(wr, wt) .. " (want " .. far_desc .. ")")
        end

        -- Regions (static map, doors closed).
        local function misc_outside(tt) return type(tt) == "table" and tt.level == 0 and tt.x >= 2511 end
        local function misc_entry_hall(tt) return inside(tt, 0, 2505, 2510, 3857, 3863) end
        local function misc_main_hall(tt) return inside(tt, 0, 2498, 2504, 3852, 3868) end
        local function misc_south_stair_room(tt) return inside(tt, 0, 2504, 2508, 3846, 3851) end
        local function misc_main_hall_south(tt) return inside(tt, 0, 2498, 2507, 3852, 3868) end
        local function misc_south_landing(tt) return inside(tt, 1, 2503, 2508, 3846, 3851) end
        local function misc_south_corridor(tt) return inside(tt, 1, 2505, 2507, 3852, 3856) end
        local function misc_north_corridor(tt) return inside(tt, 1, 2505, 2507, 3864, 3868) end
        local function misc_throne_room(tt) return inside(tt, 1, 2498, 2510, 3857, 3863) end
        local function misc_astrid_room(tt) return inside(tt, 1, 2499, 2504, 3866, 3871) end
        local function misc_brand_room(tt) return inside(tt, 1, 2499, 2504, 3849, 3854) end
        local function etc_outside(tt) return type(tt) == "table" and tt.level == 0 and tt.x <= 2607 end
        local function etc_corridor(tt) return inside(tt, 0, 2608, 2611, 3865, 3885) end
        local function etc_stair_room(tt) return inside(tt, 0, 2612, 2617, 3865, 3870) end
        local function etc_landing(tt) return inside(tt, 1, 2612, 2617, 3865, 3870) end
        local function etc_sigrid_room(tt) return inside(tt, 1, 2610, 2617, 3871, 3879) end
        local function derrik_house(tt) return inside(tt, 0, 2548, 2552, 3894, 3898) end
        local function derrik_outside(tt) return type(tt) == "table" and tt.level == 0 and tt.z <= 3893 end

        local MISC_GATE_X, MISC_GATE_Z = 2513, 3860 -- open ground east of the castle gate
        local ETC_GATE_X, ETC_GATE_Z = 2606, 3875 -- open ground west of the Etceteria castle's west door
        local DERRIK_X, DERRIK_Z = 2551, 3891 -- open ground south of Derrik's door

        -- Miscellania castle: outside the gate -> main hall (L0), and back.
        local function misc_castle_in(p)
            castle_door(p .. ".castleGateIn", 2510, 3860, 0, 2511, 3860, 2508, 3860, misc_entry_hall, "the entry hall, x 2505-2510 z 3857-3863 level 0")
            castle_door(p .. ".hallDoorIn", 2505, 3860, 0, 2505, 3860, 2503, 3860, misc_main_hall, "the main hall, x <= 2504 level 0")
        end
        local function misc_castle_out(p)
            castle_door(p .. ".hallDoorOut", 2505, 3860, 0, 2504, 3860, 2507, 3860, misc_entry_hall, "the entry hall, x 2505-2510 level 0")
            castle_door(p .. ".castleGateOut", 2510, 3860, 0, 2510, 3860, MISC_GATE_X, MISC_GATE_Z, misc_outside, "outside the gate, x >= 2511 level 0")
        end
        -- Main hall (L0) -> south corridor (L1) by the south spiral staircase, and back.
        local function misc_south_up(p)
            castle_door(p .. ".southStairDoor", 2506, 3851, 0, 2506, 3852, 2506, 3850, misc_south_stair_room, "the south stair room, z <= 3851 level 0")
            climb(p .. ".southStairsUp", "spiralstairs_wooden", 1, 2505, 3848, 0, 2504, 3849, 1)
            castle_door(p .. ".southLandingDoor", 2506, 3851, 1, 2506, 3851, 2506, 3853, misc_south_corridor, "the south corridor, x 2505-2507 z 3852-3856 level 1")
        end
        local function misc_south_down(p)
            castle_door(p .. ".southLandingDoor", 2506, 3851, 1, 2506, 3852, 2506, 3850, misc_south_landing, "the south stair landing, z <= 3851 level 1")
            climb(p .. ".southStairsDown", "spiralstairsmiddle_wooden", 3, 2505, 3848, 1, 2505, 3850, 0)
            castle_door(p .. ".southStairDoor", 2506, 3851, 0, 2506, 3851, 2506, 3853, misc_main_hall_south, "the main hall, z >= 3852 level 0")
        end
        local function throne_in_south(p)
            throne_door(p .. ".throneDoorIn", 3857, 2506, 3855, misc_throne_room, "the throne room, z 3857-3863 level 1")
        end
        local function throne_out_south(p)
            throne_door(p .. ".throneDoorOut", 3857, 2506, 3858, misc_south_corridor, "the south corridor, z <= 3856 level 1")
        end
        local function throne_in_north(p)
            throne_door(p .. ".throneDoorIn", 3863, 2506, 3865, misc_throne_room, "the throne room, z 3857-3863 level 1")
        end
        local function throne_out_north(p)
            throne_door(p .. ".throneDoorOut", 3863, 2506, 3862, misc_north_corridor, "the north corridor, z >= 3864 level 1")
        end
        -- Outside the gate -> the throne room, and back out.
        local function to_vargas(p)
            misc_castle_in(p)
            misc_south_up(p)
            throne_in_south(p)
        end
        local function from_throne_room(p)
            throne_out_south(p)
            misc_south_down(p)
            misc_castle_out(p)
        end

        -- Etceteria castle: outside its west door -> Queen Sigrid's room (L1), and back.
        local function etc_in(p)
            castle_door(p .. ".etcGateIn", 2608, 3875, 0, 2607, 3875, 2609, 3874, etc_corridor, "the castle corridor, x 2608-2611 level 0")
            castle_door(p .. ".etcStairDoorIn", 2611, 3866, 0, 2611, 3866, 2613, 3866, etc_stair_room, "the stair room, x 2612-2617 z 3865-3870 level 0")
            climb(p .. ".etcStairsUp", "spiralstairs", 1, 2613, 3867, 0, 2614, 3867, 1)
            castle_door(p .. ".sigridDoorIn", 2615, 3870, 1, 2615, 3870, 2614, 3873, etc_sigrid_room, "Queen Sigrid's room, z 3871-3879 level 1")
        end
        local function etc_out(p)
            castle_door(p .. ".sigridDoorOut", 2615, 3870, 1, 2615, 3871, 2615, 3869, etc_landing, "the stair landing, z <= 3870 level 1")
            climb(p .. ".etcStairsDown", "spiralstairstop", 1, 2614, 3867, 1, 2614, 3866, 0)
            castle_door(p .. ".etcStairDoorOut", 2611, 3866, 0, 2612, 3866, 2610, 3866, etc_corridor, "the castle corridor, x <= 2611 level 0")
            castle_door(p .. ".etcGateOut", 2608, 3875, 0, 2608, 3875, ETC_GATE_X, ETC_GATE_Z, etc_outside, "outside the west door, x <= 2607 level 0")
        end
        -- The overland hops between the two castles (open ground, joined on foot).
        local function to_etceteria(name)
            t.exec(name, t.player.goto_tile, ETC_GATE_X, ETC_GATE_Z, 0)
        end
        local function to_miscellania(name)
            t.exec(name, t.player.goto_tile, MISC_GATE_X, MISC_GATE_Z, 0)
        end

        -- ---------------------------------------------------- flowers first (guide getFlowers)
        t.exec("goto-misc", t.player.goto_tile, MISC_GATE_X, MISC_GATE_Z, 0)
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

        -- ---------------------------------------------------- King Vargas: offer
        t.player.walk_to(MISC_GATE_X, MISC_GATE_Z, 30)
        misc_castle_in("vargas1")
        misc_south_up("vargas1")
        -- Before the quest starts (%misc_quest 0, %misc_grantedaudience 0)
        -- the throne room door is the guard's: misc_door_guard.rs2
        -- [oploc1,misc_ulby_throneroomdoor] runs @doornotyetpass while
        -- misc_ulby_doorguard stands within 5 tiles, and that chat's last
        -- line sets %misc_grantedaudience = 1. The next press walks through.
        t.player.walk_to(2506, 3855, 30)
        t.exec("throneGuard", t.player.click_loc, "misc_ulby_throneroomdoor", 1, { at = { 2506, 3857, 1 } })
        t.exec("throneGuard-dialog", t.chat.play, {
            "npc:Halt! Who goes there?",
            "player:My name is",
            "npc:I'm afraid the King won't give an audience",
            "player:I am a member of the Heroes' Guild",
            "npc:Then you may pass.",
        })
        throne_in_south("vargas1")
        t.exec("talkVargas1", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas1-dialog", t.chat.play, {
            "player:You wanted to see me, Your Maj",
            "npc:Ah, yes. I am cursed -- I cann",
            "npc:My children Brand and Astrid c",
            "choose:I'll try to win over Princess Astrid.",
            "player:I'll try to win over Princess As",
            "npc:Astrid can usually be found ups",
        })

        -- The choice above ("I'll try to win over Princess Astrid.") is
        -- mutually exclusive with the Brand branch (misc_king_vargas.rs2's
        -- own opt=1/opt=2 [opnpc1,misc_king_vargas] choice, %misc_partner_multivar
        -- 0 vs 1) -- Quest Helper's courtBrand ladder can never run in the
        -- same playthrough as courtAstrid, so its eight steps are declared
        -- here rather than driven (mirror of misc.lua's own Astrid gap).
        -- Astrid was courted instead: getAnthem below still drives Brand's
        -- separate bard duty, which fires for either partner
        -- (misc_prince_brand.rs2:23's own guard runs before the
        -- toldking/partner check).
        -- BRANCH-IN: misc talkBrand1 Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_prince_brand.rs2:51)
        -- BRANCH-IN: misc giveFlowersToBrand Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_prince_brand.rs2:112)
        -- BRANCH-IN: misc clapForBrand Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_courting_emotes.rs2:32)
        -- BRANCH-IN: misc talkBrand2 Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_prince_brand.rs2:72)
        -- BRANCH-IN: misc giveCakeToBrand Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_prince_brand.rs2:123)
        -- BRANCH-IN: misc talkBrand3 Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_prince_brand.rs2:85)
        -- BRANCH-IN: misc blowKissToBrand Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_courting_emotes.rs2:39)
        -- BRANCH-IN: misc useRingOnBrand Astrid was courted here, the mutually exclusive choice; misc.lua drives the Brand branch (misc_prince_brand.rs2:132)

        -- ---------------------------------------------------- courting Astrid
        -- The throne room's north door opens on the north corridor, where
        -- Astrid's room is: no stairs between them.
        throne_out_north("astrid")
        castle_door("astrid.astridDoorIn", 2504, 3867, 1, 2505, 3867, 2503, 3868, misc_astrid_room, "Princess Astrid's room, x 2499-2504 z 3866-3871 level 1")
        -- content-parity fix: Vargas now sets %misc_affection to
        -- not_started (not step0), so this FIRST visit reads astrid_talk1,
        -- the five-line courtship intro (Quest Helper's own talkAstrid1 step).
        t.exec("talkAstrid1", t.player.talk_to, "misc_princess_astrid", 1)
        t.exec("talkAstrid1-dialog", t.chat.play, {
            "player:So, Princess, I hear you're quite the archer.",
            "npc:Archery is a noble art! Not everyone in this castle",
            "player:That doesn't sound very fair.",
            "npc:Derrik has been very helpful, teaching me in secret",
            "npc:It's kind of you to listen. If you brought me some flowers",
        })

        -- misc_princess_astrid.rs2's opnpcu flowers case ends in a plain
        -- mes() line (a chat-log message, not a ~mesbox), so use_on's own
        -- settle (new chat line / backpack change) is the whole of the row
        -- -- it only gets Astrid to ASK for the Dance now, it does not
        -- perform it.
        local astrid = t.player.by_symbol("npc", "misc_princess_astrid")
        t.exec("giveFlowersToAstrid", t.player.use_on, "flowers_waterfall_quest", astrid)
        -- inv.await(name, 0, ticks) never actually waits (total >= 0 is
        -- always true -- QUEST_AUTHORING.md's gaps section) -- ticks then a
        -- direct count read is the real "wait for it to be consumed".
        t.ticks(2)
        local flowers_gone_result, flowers_gone_count = t.inv.count("flowers_waterfall_quest")
        t.check("giveFlowersToAstrid.consumed", flowers_gone_result == "ok" and flowers_gone_count == 0,
            "inv.count(flowers_waterfall_quest) -> " .. tostring(flowers_gone_result) .. " " .. tostring(flowers_gone_count))

        -- Content-parity leg: astrid_need_dance -> the player must actually
        -- play Dance next to Astrid (misc_courting_emotes.rs2's
        -- ~misc_emote_performed_astrid, hooked off ~emote_perform), s1_step1
        -- (11) -> s1_step5 (15). Quest Helper's own danceForAstrid step.
        t.exec("danceForAstrid", t.player.emote, "dance")
        t.expect("affection.s1_step5", t.var.expect("varb73_misc_affection", 15))

        t.exec("talkAstrid2", t.player.talk_to, "misc_princess_astrid", 1)
        t.exec("talkAstrid2-dialog", t.chat.play, {
            "player:What happened next?",
            "npc:Derrik took me hunting once, out past the walls",
            "player:That sounds like a good idea.",
            "npc:I'm quite fond of it myself, though I'd never say so",
        })

        -- Unlike the flowers/ring cases, the bow branch (misc_princess_
        -- astrid.rs2:114-121) never calls inv_del -- Astrid only examines
        -- it and hands it back, so the real evidence is the affection
        -- state advancing to s2_step4, not the backpack falling.
        astrid = t.player.by_symbol("npc", "misc_princess_astrid")
        t.exec("giveBowToAstrid", t.player.use_on, "shortbow", astrid)
        t.ticks(2)
        local bow_kept_result, bow_kept_count = t.inv.count("shortbow")
        t.check("giveBowToAstrid.kept", bow_kept_result == "ok" and bow_kept_count == 1,
            "inv.count(shortbow) -> " .. tostring(bow_kept_result) .. " " .. tostring(bow_kept_count)
                .. " (misc_princess_astrid.rs2's bow case never inv_dels -- she hands it back)")
        t.expect("affection.s2_step4", t.var.expect("varb73_misc_affection", 24))

        -- astrid_talk3 now ENDS at "Truly?" (s2_step4 -> s3_step4) -- the
        -- kiss itself is the emote leg below, not narrated inline any more.
        t.exec("talkAstrid3", t.player.talk_to, "misc_princess_astrid", 1)
        t.exec("talkAstrid3-dialog", t.chat.play, {
            "player:Do you like it here in Miscellania?",
            "npc:It's a lovely little country, though I don't suppose",
            "player:I could say the same about Brand and his poetry.",
            "npc:And what a great bard he makes! Truly, though...",
        })

        -- Content-parity leg: astrid_need_kiss -> the player must actually
        -- play Blow Kiss next to Astrid, s3_step4 -> s3_step0 (30). Named
        -- after Quest Helper's own blowKissToAstrid step.
        t.exec("blowKissToAstrid", t.player.emote, "blow kiss")
        t.expect("affection.s3_step0", t.var.expect("varb73_misc_affection", 30))

        astrid = t.player.by_symbol("npc", "misc_princess_astrid")
        t.exec("useRingOnAstrid", t.player.use_on, "gold_ring", astrid)
        t.exec("useRingOnAstrid-dialog", t.chat.play, {
            "player:Princess Astrid, will you vouch for me",
            "npc:I will -- and gladly.",
        })
        -- The ring case inv_dels the ring it was handed (misc_princess_
        -- astrid.rs2's gold_ring branch); misc_acceptedtorule = 1 is proven
        -- live by Vargas's "Wonderful!" branch below, which only fires when
        -- it reads 1.
        t.ticks(2)
        local ring_gone_result, ring_gone_count = t.inv.count("gold_ring")
        t.check("useRingOnAstrid.consumed", ring_gone_result == "ok" and ring_gone_count == 0,
            "inv.count(gold_ring) -> " .. tostring(ring_gone_result) .. " " .. tostring(ring_gone_count))

        -- ---------------------------------------------------- back to Vargas
        castle_door("vargas2.astridDoorOut", 2504, 3867, 1, 2504, 3867, 2506, 3866, misc_north_corridor, "the north corridor, x 2505-2507 level 1")
        throne_in_north("vargas2")
        t.exec("talkVargas2", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas2-dialog", t.chat.play, {
            "npc:Wonderful! Now, let us discuss securing peace",
        })
        t.expect("quest.stage.talked_to_king", t.quest.expect_stage("talked_to_king"))

        -- ---------------------------------------------------- Etceteria diplomacy
        from_throne_room("sigrid1")
        to_etceteria("goto-etceteria1")
        etc_in("sigrid1")
        t.exec("talkSigrid1", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("talkSigrid1-dialog", t.chat.play, {
            "player:King Vargas sent me to discuss peace",
            "npc:Peace? Only if Vargas is willing to formally",
        })
        t.expect("quest.stage.talked_to_queen", t.quest.expect_stage("talked_to_queen"))

        etc_out("vargas3")
        to_miscellania("goto-misc2")
        to_vargas("vargas3")
        t.exec("talkVargas3", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("talkVargas3-dialog", t.chat.play, {
            "player:Queen Sigrid wants you to recognise Etceteria",
            "npc:Recognise Etceteria? After the insults",
        })
        t.expect("quest.stage.queen_requests_recognition", t.quest.expect_stage("queen_requests_recognition"))

        from_throne_room("sigrid2")
        to_etceteria("goto-etceteria2")
        etc_in("sigrid2")
        t.exec("talkSigrid2", t.player.talk_to, "misc_queen_sigrid", 1)
        t.exec("talkSigrid2-dialog", t.chat.play, {
            "player:King Vargas says he'll recognise Etceteria",
            "npc:A new anthem? Our anthem is a fine old song",
        })
        t.expect("quest.stage.need_bard_for_anthem", t.quest.expect_stage("need_bard_for_anthem"))

        -- ---------------------------------------------------- the anthem
        -- Brand's own bard duty (brand_give_anthem) fires regardless of
        -- who is being courted -- misc_prince_brand.rs2:23's guard runs
        -- before the toldking/partner check, so this leg is driven here
        -- exactly as in misc.lua, not a gap.
        etc_out("brand")
        to_miscellania("goto-misc3")
        misc_castle_in("brand")
        misc_south_up("brand")
        castle_door("brand.brandDoorIn", 2504, 3853, 1, 2505, 3853, 2503, 3852, misc_brand_room, "Prince Brand's room, x 2499-2504 z 3849-3854 level 1")
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

        castle_door("ghrim.brandDoorOut", 2504, 3853, 1, 2504, 3853, 2506, 3854, misc_south_corridor, "the south corridor, x 2505-2507 level 1")
        throne_in_south("ghrim")
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

        from_throne_room("sigrid3")
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
        to_miscellania("goto-misc4")
        to_vargas("vargas4")
        t.exec("giveTreatyToVargas", t.player.talk_to, "misc_king_vargas", 1)
        t.exec("giveTreatyToVargas-dialog", t.chat.play, {
            "player:Queen Sigrid has agreed to the treaty.",
            "npc:At last! I'll sign this gladly",
        })
        t.expect("quest.stage.gave_king_treaty", t.quest.expect_stage("gave_king_treaty"))

        from_throne_room("derrik")
        t.exec("goto-derrik", t.player.goto_tile, DERRIK_X, DERRIK_Z, 0)
        pass_door("derrik.derrikDoorIn", "viking_abode_door", "viking_abode_door_open", 2551, 3893, 0, 2551, 3893, 2550, 3895,
            derrik_house, "Derrik's house, x 2548-2552 z 3894-3898 level 0")
        local derrik_tiles_result, derrik_tiles = t.npc.tiles("misc_smithy", 10)
        local here_result, here_tile = t.world.tile()
        t.note("before forgeNib: player " .. tile_text(here_result, here_tile) .. "; " .. tostring(derrik_tiles_result) .. " " .. tostring(derrik_tiles))
        t.exec("forgeNib", t.player.talk_to, "misc_smithy", 1)
        t.exec("forgeNib-dialog", t.chat.play, {
            "player:I have a slightly strange request",
            "npc:Let's see what we can do.",
            "npc:There you are. You'll need to fix that",
        })
        local nib_result, nib_detail = t.inv.await("misc_giant_nib", 1, 10)
        t.check("inv.gotNib", nib_result == "ok",
            "inv.await(misc_giant_nib,1) -> " .. tostring(nib_result) .. " " .. tostring(nib_detail))

        -- misc_giant_nib.rs2's opheldu combine is a single mes() line too --
        -- no dialogue to continue_() through, just an inventory change: the
        -- nib and the log are inv_del'd and the pen inv_add'ed.
        t.exec("makePen", t.player.use_item_on_item, "misc_giant_nib", "logs")
        local pen_result, pen_detail = t.inv.await("misc_giant_pen", 1, 10)
        t.check("inv.gotPen", pen_result == "ok",
            "inv.await(misc_giant_pen,1) -> " .. tostring(pen_result) .. " " .. tostring(pen_detail))
        local nib_left_result, nib_left = t.inv.count("misc_giant_nib")
        local logs_left_result, logs_left = t.inv.count("logs")
        t.check("makePen.consumed", nib_left_result == "ok" and nib_left == 0 and logs_left_result == "ok" and logs_left == 0,
            "inv.count(misc_giant_nib) -> " .. tostring(nib_left_result) .. " " .. tostring(nib_left)
                .. ", inv.count(logs) -> " .. tostring(logs_left_result) .. " " .. tostring(logs_left))

        pass_door("vargas5.derrikDoorOut", "viking_abode_door", "viking_abode_door_open", 2551, 3893, 0, 2551, 3894, DERRIK_X, DERRIK_Z,
            derrik_outside, "outside Derrik's house, z <= 3893 level 0")
        to_miscellania("goto-misc5")
        to_vargas("vargas5")
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
        -- "put me to work" reply is narration only (content-parity fix
        -- 2026-09-23) and sets nothing, so a visit here would be a driven
        -- row for a step the guide does not have -- skipped, following the
        -- guide (same as misc.lua).
        --
        -- Real leg: chop the kingdom's maples for real beside Lumberjack
        -- Leif. Woodcutting 45 + an axe are set up above; every successful
        -- swing inside the kingdom zone runs the real leif_intercept_wood
        -- (misc_approval +1, real Woodcutting xp) -- click_loc starts the
        -- real repeating chop action (oploc1 -> oploc3 resume, woodcut.rs2)
        -- that keeps swinging on its own -- but get_logs rolls a 1-in-8
        -- deplete chance on EVERY successful swing, and a deplete stops the
        -- resume outright, so re-find and re-click "mapletree" each time
        -- the current session ends short of the target, awaiting one more
        -- approval point than is already banked (same pattern as misc.lua).
        --
        -- Trap 15's "record the loop's OUTCOME row only": an attempt whose
        -- own non-effect is a walk, or that lands on a tree already
        -- depleted this same tick ("Nothing interesting happens." -- a
        -- real, occasional content answer while the neighbouring copy is
        -- mid-regrowth, not a click failure), is not graded per attempt --
        -- called directly rather than through t.exec, so a click that did
        -- not land this time cannot fail the ledger over a loop that still
        -- reaches its target. approval.real_chops below is the row that
        -- proves the outcome.
        from_throne_room("leif")
        t.exec("goto-leif", t.player.goto_tile, 2550, 3866, 0)
        local chop_attempts = 0
        local chop_last_result, chop_last_detail = "n/a", "n/a"
        local approval_result, approval_value = t.var.server("varb72_misc_approval")
        while (approval_result ~= "ok" or (approval_value or 0) < 3) and chop_attempts < 10 do
            chop_attempts = chop_attempts + 1
            local before_result, before_value = t.var.server("varb72_misc_approval")
            chop_last_result, chop_last_detail = t.player.click_loc("mapletree", 1)
            t.shot("chopMaple-" .. chop_attempts)
            t.var.await_server("varb72_misc_approval", (before_value or 0) + 1, 250)
            approval_result, approval_value = t.var.server("varb72_misc_approval")
        end
        t.check("approval.real_chops", approval_result == "ok" and (approval_value or 0) >= 3,
            "var.server(misc_approval) after " .. tostring(chop_attempts) .. " chopMaple attempt(s) (last: "
                .. tostring(chop_last_result) .. " " .. tostring(chop_last_detail) .. ") -> "
                .. tostring(approval_result) .. " " .. tostring(approval_value))

        -- ::misc_earnapproval is the sanctioned GRIND fast-forward for
        -- exactly this loop (docs/QUEST_SERVER_CHEATS.md) -- it walks the
        -- real ~leif_intercept_wood body in a guarded loop, skipping only
        -- the swing cadence and the maple's regrowth, until 96 of 127
        -- (75%). t.cheat is hollow (trap 12): record the call with t.step,
        -- then read its own line back with t.msg.expect and confirm the
        -- server varbit itself.
        local earn_cheat_result = t.cheat("::misc_earnapproval")
        t.step("approval.fast_forward_cheat", earn_cheat_result == "ok" and "PASS" or "FAIL",
            "::misc_earnapproval -> " .. tostring(earn_cheat_result))
        t.exec("approval.fast_forward", t.msg.expect, "You worked for the kingdom")
        -- Named after Quest Helper's own get75Support step ("Reach 75%
        -- support...") -- this is the row that proves it reached.
        t.exec("get75Support", t.var.await_server, "varb72_misc_approval", 96, 10)

        -- ---------------------------------------------------- back to Vargas: the crowning
        to_miscellania("goto-misc6")
        to_vargas("vargas6")

        -- The coffer reading taken on the tick BEFORE the crowning click, so
        -- the assertion below is a delta this run measured and not a guess
        -- at a starting balance. Read server-side: the coffer varbit is the
        -- kingdom's bookkeeping and nothing transmits it to this client's
        -- varp cache.
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
        -- Miscellania" as SCROLL TEXT only -- nothing in the tree inv_adds
        -- a ring or grants skill xp, so the scroll's own reward lines are
        -- the only real evidence of the other two documented rewards.
        -- t.scroll.rewards() awaits the scroll's own mount itself, so
        -- quest.expect_complete()'s quest.scroll shot below photographs a
        -- scroll already on screen (section 8's "settle before
        -- expect_complete" rule).
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
