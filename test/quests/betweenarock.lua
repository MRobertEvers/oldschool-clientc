-- Between a Rock... -- hand-driven against the quest's own scripts
-- (server/scripts/quests/quest_betweenarock/) and docs/quests/between_a_rock.md.
-- Tier 1.
--
-- Real prerequisite (betweenarock_shared.rs2's own `dwarfrock_real_prereqs_met`)
-- is ONLY `%fishingcompo >= ^fishingcompo_complete` -- the cache dbrow's two
-- `requirement_quests` values decode to the wrong rows (sheep herder / mage
-- arena 1), and Dwarf Cannon is deliberately soft-skipped in this tree
-- because `%mcannon` never advances past 0 anywhere in the tree (the audit
-- doc's own P0 #1 finding). So setup completes only Fishing Contest.
--
-- TRAVEL (owner rule 2026-10-03: no goto into or out of a closed space). The
-- guide's own route is walked and clicked on every visit:
--   * Dondakan, every time: goto the open hillside west of the Keldagrim
--     entrance (2730,3712), `trollromance_stronghold_exit_tunnel` (enterDwarfCave*,
--     betweenarock_travel.rs2:18), `dwarf_cavewall_tunnel` (enterDwarfCave2*,
--     :29), then the Dwarven Ferryman for 5 coins (talkToFerryman*, :42).
--   * Dondakan -> Keldagrim, every time: walk to the jetty and take the second
--     ferryman (travelBackWithFerryman*, :67) -- the alcove has no other way out.
--   * Rolad: the hut's `poordoor` (3016,3453, maps/m47_53.jl2) is opened going in
--     and coming out; the Dwarven Mine is entered by `fai_dwarf_trapdoor_down`
--     (enterDwarvenMine) inside Rolad's room and left by
--     `ladder_from_cellar_directional` (goBackUpToRolad), which lands back in it.
--   * Khorvak: `tunnelstairstop` down (enterKhorvakRoom) and `tunnelstairs` up.
--   * Keldagrim -> the surface, every time (leaveKeldagrim*): walk to the
--     city dock, the Dwarven Boatman `dwarf_city_boatman_city` to the mines
--     (keldagrim_travel.rs2, lands 2838,10127 on the ferry bank), the bank's
--     `dwarf_cave_entrance` back to the troll room (betweenarock_travel.rs2:47,
--     2778,10161), then `trollromance_piste_exit_tunnel_bottom` up to the
--     hillside east of Rellekka (2730,3713). The gotos after it leave that
--     open hillside.
--   * The realm's outer wall of flame is jumped by its own op1 Jump-through
--     (betweenarock_realm.rs2:101) on the way to the centre ring.
-- Seam pass matthew-mbp-m4-b58-seam1 (OSRS-Content 6369379ada) fixed what used
-- to stop this route: the cave tunnel's landing (now on the ferry bank,
-- 2838,10124), the outer flames' Jump-through, and Keldagrim's exits.
-- OPEN (content, not driven around): `trollromance_stronghold_exit_tunnel`
-- (betweenarock_travel.rs2:18) still lands on 2781,10160, a rock tile beside
-- the cave; LostCity (quest_troll_love.rs2:44-45) and maplink give 2773,10162.
-- The next click (dwarf_cavewall_tunnel at 2781,10161) answers from there, so
-- the route is not stopped by it; enterDwarfCave* accepts either tile so the
-- row keeps passing when content moves the landing.
--
-- The four schematic pieces: Dondakan (from firing the golden cannonball),
-- the lore book's last page (read the book a SECOND time, at stage 80),
-- the Dwarven Engineer, and Khorvak. "Assemble" opens the REAL per-piece 2D
-- position puzzle (interfaces 113/114, betweenarock_schematics.rs2): each
-- piece is scrambled off its border target on open, and
-- dwarfrock_puzzle_dx{1,2,3}/dy{1,2,3} are read back through t.var.server and
-- driven within the 4px tolerance with the dr_move_* buttons.
--
-- The Arzinian Avatar's category is chosen by the player's strongest combat
-- stat (dwarfrock_realm.rs2's own dwarfrock_spawn_avatar) -- this character is
-- built melee-heavy, so the branch is the "mage" category, and its COLOUR is
-- picked by gold ore held at the flame: <15 ore -> _mage_green (level 125),
-- >=15 -> _mage_yellow (level 75).

local EAT_BELOW = 50

return {
    id = "betweenarock",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- four tunnel/ferry round trips, the mine, White Wolf Mountain
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99", -- exceeds the quest's own req_defence=30 gate
        "::setlevel hitpoints 99",
        "::setlevel mining 40", -- exactly the quest's req
        "::setlevel smithing 50", -- exactly the quest's req_smithing=50 gate
        "::complete quest_fishingcontest", -- quest_cheat.rs2's dispatch row; the ONLY prereq dwarfrock_real_prereqs_met checks
        "::give rune_scimitar 1", -- combat prerequisite (Quest Helper: combat gear), worn below
        "::give adamant_pickaxe 1", -- Quest Helper "Any pickaxe" (page 3, realm gold ore); mining 40 cannot wield rune (req 41)
        "::give hammer 1", -- Quest Helper bring-along for the golden helmet
        "::give gold_bar 4", -- Quest Helper bring-along: 1 smelted into the golden cannonball, 3 smithed into the helmet
        "::give ammo_mould 1", -- Quest Helper bring-along for casting the golden cannonball
        "::give coins 100", -- Quest Helper "Coins": the Dwarven Ferryman takes 5 per crossing (betweenarock_travel.rs2:42, four crossings)
        "::give swordfish 8", -- Quest Helper "Food" for the Avatar (and the mine's scorpion)
        -- The wiki's Avatar advice: pray against its style. This melee build meets the Avatar of
        -- Magic, so Protect from Magic (prayer 37, the lowest level that has it) and one prayer
        -- potion for the drain (prayer is not assumed to regenerate). Combat is already 99-melee,
        -- so no dialogue on the route changes branch on the extra prayer levels.
        "::setlevel prayer 37",
        "::give 4doseprayerrestore 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb299_dwarfrock_quest",
            constants = {
                not_started = 0,
                told_of_rock = 10,
                engineer_confirmed = 20,
                gathering_pages = 30,
                book_ready = 40,
                returned_with_book = 50,
                gold_bar_shown = 60,
                fired_into_rock = 70,
                assembling_schematics = 80,
                in_the_realm = 90,
                avatar_defeated = 100,
                complete = 110,
            },
            row = "quest_betweenarock",
            display = "Between a Rock...", -- all.quest.compack's own display text, confirmed by grep
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end
        local function loc_text(r, l)
            if r == "ok" and type(l) == "table" then
                return "at " .. tostring(l.tile_x) .. "," .. tostring(l.tile_z) .. "," .. tostring(l.level)
            end
            return tostring(r)
        end

        -- Wait for a teleport a click queued (a tunnel, a ferry, a climb) to land.
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and pred(tt)
                end,
                note = what .. ": waiting for the landing",
            }, ticks)
        end

        -- Hitpoints are sampled through every fight; below EAT_BELOW a
        -- swordfish is eaten. The margin rows read these.
        local hp_low, hp_eaten = nil, 0
        local function vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level then
                if hp_low == nil or hp.level < hp_low then
                    hp_low = hp.level
                end
                if hp.level < EAT_BELOW then
                    local er = t.player.inv_op("swordfish", 1)
                    if er == "ok" then
                        hp_eaten = hp_eaten + 1
                    end
                    t.ticks(1)
                end
            end
        end
        local function margin_row(name, fight)
            vitals()
            local fr, food = t.inv.count("swordfish")
            local hr, hp = t.skill.read("hitpoints")
            t.check(name, hp_low ~= nil and hp_low >= 25 and fr == "ok" and food >= 1,
                fight .. ": lowest hp " .. tostring(hp_low) .. "/99 (sampled through the fight), hp now "
                    .. tostring(hr == "ok" and hp.level or hr) .. ", swordfish staged 8, eaten " .. hp_eaten
                    .. ", left " .. tostring(food) .. " (" .. tostring(fr) .. ") (margin: lowest hp >= 25 AND food left)")
            hp_low = nil
        end

        -- Cross one door on foot (the b56 pass_door pattern). Walk to the tile
        -- on this side; if the closed leaf stands on door_x,door_z on the
        -- player's own level, click THAT copy; otherwise an earlier press left
        -- it open (a door swings back after 500 ticks), so assert the open leaf
        -- stands within 1 of the door tile -- a row that fails when neither leaf
        -- is there -- and do not press it again. Then walk through and check.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local here = (nr == "ok") and nt.level or 0
            local cr, cd = t.world.loc_near(closed_sym, 3)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == here then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and od.level == here and not (od.tile_x == door_x and od.tile_z == door_z)
                        and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": " .. (cr == "ok" and ("nearest closed copy " .. loc_text(cr, cd)) or tostring(cr))
                        .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf " .. loc_text(orr, od)) or tostring(orr))
                        .. " (want the open leaf within 1 of the door tile: an earlier press left it open, so it is walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 30)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- A climb (ladder, trapdoor, stairs): click the copy at lx,lz, wait for
        -- the plane or map frame to change, and grade the row on the landing.
        local function climb(name, sym, lx, lz, landed_ok, landed_desc)
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc(sym, 1, { at = { lx, lz } })
            await_tile(landed_ok, 10, name)
            local wr, wt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and br == "ok" and not landed_ok(bt) and wr == "ok" and landed_ok(wt),
                "from " .. tile_text(br, bt) .. " click_loc(" .. sym .. " at " .. lx .. "," .. lz .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(wr, wt) .. " (want " .. landed_desc .. ")")
        end

        -- Rolad's hut on Ice Mountain: Rolad's room (x 3017-3023, z 3450-3454)
        -- is closed by poordoor 3016,3453 (east edge) off the open passage
        -- x 3015-3016 that runs north to the hillside; the trapdoor down to the
        -- Dwarven Mine is inside the room (3019,3450).
        local function in_rolads_room(tt)
            return tt.level == 0 and tt.x >= 3017 and tt.x <= 3023 and tt.z >= 3450 and tt.z <= 3454
        end
        local function rolad_in(prefix)
            pass_door(prefix .. ".roladDoorIn", "poordoor", "poordooropen", 3016, 3453, 3016, 3453, 3018, 3453,
                in_rolads_room, "inside Rolad's room, x 3017-3023 z 3450-3454")
        end
        local function rolad_out(prefix)
            pass_door(prefix .. ".roladDoorOut", "poordoor", "poordooropen", 3016, 3453, 3017, 3453, 3015, 3456,
                function(tt) return tt.level == 0 and tt.x <= 3016 and tt.z >= 3455 end, "out on the hillside north of the hut, x <= 3016 z >= 3455")
        end

        -- After the ferry the scene rebuilds and Dondakan joins the client's
        -- npc pool 2-3 ticks late (seam b58-seam1 (e)): wait for him before
        -- every talk to or use on him.
        local function dondakan_present(name)
            t.exec(name .. ".dondakanPresent", t.npc.await_present, "dwarfrock_dondakan", 15, 10)
        end

        -- The guide's way to Dondakan: the Keldagrim entrance tunnel, the cave
        -- tunnel, the paid ferry.
        local ferry1_trips = 0
        local function to_dondakan(suffix)
            local sr, st = t.world.tile()
            if sr == "ok" and st.level == 0 and math.abs(st.x - 2730) <= 4 and math.abs(st.z - 3712) <= 4 then
                -- leaveKeldagrim* came up the troll tunnel onto this hillside: walk.
                t.player.walk_to(2730, 3712, 10)
                local hr, ht = t.world.tile()
                t.check("walk-trollTunnel" .. suffix, hr == "ok" and ht.level == 0 and ht.x == 2730 and ht.z == 3712,
                    "from " .. tile_text(sr, st) .. " walked to 2730,3712 beside the tunnel -> " .. tile_text(hr, ht))
            else
                t.exec("goto-trollTunnel" .. suffix, t.player.goto_tile, 2730, 3712, 0)
            end
            climb("enterDwarfCave" .. suffix, "trollromance_stronghold_exit_tunnel", 2731, 3712,
                function(tt) return tt.level == 0 and ((tt.x == 2781 and tt.z == 10160) or (tt.x == 2773 and tt.z == 10162)) end,
                "2781,10160,0 (betweenarock_travel.rs2:18 p_teleport(0_43_158_29_48), a rock tile: OPEN in the header) "
                    .. "or LostCity's 2773,10162,0")
            climb("enterDwarfCave2" .. suffix, "dwarf_cavewall_tunnel", 2781, 10161,
                function(tt) return tt.level == 0 and tt.x == 2838 and tt.z == 10124 end,
                "the ferry bank 2838,10124,0 (betweenarock_travel.rs2:42, seam b58-seam1 (e))")
            t.player.walk_to(2837, 10129, 20)
            local wr, wt = t.world.tile()
            t.check("walk-ferryBank" .. suffix, wr == "ok" and wt.level == 0 and math.abs(wt.x - 2837) <= 1 and math.abs(wt.z - 10129) <= 1,
                "walked from the cave landing to 2837,10129 beside the Dwarven Ferryman -> " .. tile_text(wr, wt))
            local cbr, coins_before = t.inv.count("coins")
            t.exec("talkToFerryman" .. suffix, t.player.talk_to, "dwarfrock_ferryman1", 1)
            if ferry1_trips == 0 then
                t.exec("talkToFerryman" .. suffix .. "-dialog", t.chat.play, {
                    "player:Can you take me across the water?",
                    "npc:Aye, but it'll cost you 5 coins",
                    "choose:Yes please.",
                })
            else
                t.exec("talkToFerryman" .. suffix .. "-dialog", t.chat.play, {
                    "npc:Back again? That'll be another 5 coins",
                    "choose:Yes please.",
                })
            end
            await_tile(function(tt) return tt.z >= 10160 end, 10, "talkToFerryman" .. suffix)
            local ar, at = t.world.tile()
            local car, coins_after = t.inv.count("coins")
            t.check("talkToFerryman" .. suffix .. ".crossed",
                ar == "ok" and at.x == 2823 and at.z == 10165 and at.level == 0
                    and cbr == "ok" and car == "ok" and coins_after == coins_before - 5,
                "landed " .. tile_text(ar, at) .. " (want Dondakan's side 2823,10165,0: betweenarock_travel.rs2:63); coins "
                    .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (want -5, ^dwarfrock_ferry_toll)")
            ferry1_trips = ferry1_trips + 1
            return true
        end

        -- Dondakan's alcove has one way out: the second ferryman, on the jetty
        -- 85 tiles east (static collision: REACH len=85 from the alcove).
        local ferry2_trips = 0
        local function to_keldagrim(name)
            t.player.walk_to(2855, 10146, 120)
            local wr, wt = t.world.tile()
            t.check(name .. ".atJetty", wr == "ok" and math.abs(wt.x - 2855) <= 2 and math.abs(wt.z - 10146) <= 2,
                "walked from the alcove to the jetty 2855,10146 -> " .. tile_text(wr, wt))
            t.exec(name, t.player.talk_to, "dwarfrock_ferryman2", 1)
            if ferry2_trips == 0 then
                t.exec(name .. "-dialog", t.chat.play, {
                    "player:Can you take me to Keldagrim?",
                    "npc:Climb aboard",
                })
            else
                t.exec(name .. "-dialog", t.chat.play, {
                    "npc:Heading back to Keldagrim?",
                })
            end
            await_tile(function(tt) return tt.z >= 10190 end, 10, name)
            local ar, at = t.world.tile()
            t.check(name .. ".landed", ar == "ok" and at.x == 2865 and at.z == 10195 and at.level == 0,
                "landed " .. tile_text(ar, at) .. " (want Keldagrim 2865,10195,0: betweenarock_travel.rs2:78)")
            ferry2_trips = ferry2_trips + 1
        end

        -- Keldagrim's real way out (seam b58-seam1 (f), keldagrim_travel.rs2):
        -- the city's Dwarven Boatman to the mines (lands 2838,10127 on the
        -- ferry bank), the bank's crack `dwarf_cave_entrance` back to the troll
        -- room (betweenarock_travel.rs2:47, 2778,10161), and the tunnel up to the
        -- hillside east of Rellekka (`trollromance_piste_exit_tunnel_bottom`,
        -- maplink 2730,3713).
        local function leave_keldagrim(name)
            local ck = t.chat.kind()
            if ck ~= "none" then t.chat.drain({ max_pages = 6 }) end
            t.ticks(2)
            local w1, d1 = t.player.walk_to(2888, 10225, 120)
            if w1 == "refused" then
                -- the dock is outside the scene the ferry landing built: walk
                -- north inside it first, then on to the dock
                for _, hop in ipairs({ { 2878, 10214 }, { 2880, 10210 }, { 2876, 10206 } }) do
                    local hr = t.player.walk_to(hop[1], hop[2], 40)
                    if hr == "ok" then break end
                end
                w1, d1 = t.player.walk_to(2888, 10225, 120)
            end
            local wr0, wt0 = t.world.tile()
            t.check(name .. ".walkToDock", wr0 == "ok" and wt0.level == 0 and math.abs(wt0.x - 2888) <= 3 and math.abs(wt0.z - 10225) <= 3,
                "chat was " .. tostring(ck) .. "; walk_to(2888,10225) -> " .. tostring(w1) .. " " .. tostring(d1) .. "; at " .. tile_text(wr0, wt0))
            t.exec(name .. "-boatman", t.player.talk_to, "dwarf_city_boatman_city", 1)
            t.exec(name .. "-boatman-dialog", t.chat.play, {
                "npc:Want me to take you back to the mines?",
                "choose:Yes, please take me.",
                "player:Yes, please take me.",
            })
            await_tile(function(tt) return tt.level == 0 and tt.x == 2838 and tt.z == 10127 end, 10, name)
            local ar, at = t.world.tile()
            t.check(name .. ".mines", ar == "ok" and at.level == 0 and at.x == 2838 and at.z == 10127,
                "landed " .. tile_text(ar, at) .. " (want 2838,10127,0: keldagrim_travel.rs2 boatman)")
            t.ticks(3)
            climb(name .. "-caveEntrance", "dwarf_cave_entrance", 2838, 10123,
                function(tt) return tt.level == 0 and tt.x == 2778 and tt.z == 10161 end,
                "the troll room 2778,10161,0 (betweenarock_travel.rs2:47)")
            t.ticks(3)
            climb(name .. "-trollRoomTunnel", "trollromance_piste_exit_tunnel_bottom", 2771, 10161,
                function(tt) return tt.level == 0 and tt.z < 4000 end,
                "the hillside east of Rellekka (maplink 2730,3713)")
            local sr, st = t.world.tile()
            t.check(name .. ".surface", sr == "ok" and st.level == 0 and st.x == 2730 and st.z == 3713,
                "out of the troll room -> " .. tile_text(sr, st) .. " (want 2730,3713,0, maplink trollromance_piste_exit_tunnel_bottom)")
            t.ticks(3)
        end

        -- ============================================================
        -- Dondakan #1 -- accept the quest (dwarfrock_dondakan_talk,
        -- not_started branch).
        -- ============================================================
        -- From the Lumbridge fixture the only way on foot to the Fremennik
        -- hillside is the members' gate south of Taverley (goto_table: NEEDS-DOOR
        -- via membergater@2933,3320 at 30/80/160). Overland to its south side
        -- (reach.py 3206,3233 -> 2934,3318: REACH closed-doors len=387), the
        -- gate pressed by its verb, then overland from its north side (2934,3322
        -- -> 2730,3712: REACH closed-doors len=948 at margin 80).
        t.exec("goto-enterDwarfCave.memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("enterDwarfCave.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320" })
        if not to_dondakan("") then
            return
        end
        dondakan_present("talkToDondakan")
        t.exec("talkToDondakan", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakan-dialog", t.chat.play, {
            "player:What are you doing firing a cannon at that wall?",
            "npc:Ha! I'm trying to blast my way into the rock",
            "player:So why were you trying to get through the rock again?",
            "npc:I've heard tales of a lost dwarven realm",
            "player:Sounds interesting! Can I help?",
            "npc:You certainly can! Head to Keldagrim",
        })
        t.expect("quest.stage.told_of_rock", t.quest.expect_stage("told_of_rock"))

        -- ============================================================
        -- Dwarven Engineer -- points at Rolad (betweenarock_schematics.rs2).
        -- ============================================================
        to_keldagrim("travelBackWithFerryman")
        t.exec("talkToEngineer", t.player.talk_to, "dwarfrock_engineer1", 1)
        t.exec("talkToEngineer-dialog", t.chat.play, {
            "player:Dondakan sent me -- he's trying to blast his way into a rock.",
            "npc:Ha! A cannonball alone won't crack solid rock like that.",
        })
        t.expect("quest.stage.engineer_confirmed", t.quest.expect_stage("engineer_confirmed"))

        -- ============================================================
        -- Rolad #1 -- accepts the three-page hunt (betweenarock_pages.rs2).
        -- Out of Keldagrim by its real exit (leave_keldagrim), then an
        -- overland goto from the open hillside east of Rellekka to the open
        -- hillside north of Rolad's hut; the hut door is opened on foot.
        -- ============================================================
        leave_keldagrim("leaveKeldagrimForRolad")
        -- Ice Mountain lies east of Taverley's wall: the only walk on foot goes
        -- through the members' gate 2935,3450 (goto_table: NEEDS-DOOR via
        -- membergater@2935,3450). Overland to its west side (2730,3713 ->
        -- 2932,3450: REACH closed-doors len=863 at margin 80), out through the
        -- gate by its verb, then overland (2937,3450 -> 3015,3457: REACH len=221).
        t.exec("goto-talkToRolad.memberGate", t.player.goto_tile, 2932, 3450, 0)
        t.exec("talkToRolad.memberGateOut", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
            near = { 2934, 3450 }, far_ok = function(tl) return tl.x >= 2936 end,
            far_desc = "out of Taverley, x >= 2936", far = { 2937, 3450 } })
        t.exec("goto-roladHut", t.player.goto_tile, 3015, 3457, 0)
        rolad_in("talkToRolad")
        t.exec("talkToRolad", t.player.talk_to, "dwarfrock_rolad", 1)
        t.exec("talkToRolad-dialog", t.chat.play, {
            "player:The Engineer sent me to see you about some old dwarven lore.",
            "npc:Ah, you'll be wanting to know about the old realm",
            "player:I'll be back later.",
            "npc:Bring me back three pages",
        })
        t.expect("quest.stage.gathering_pages", t.quest.expect_stage("gathering_pages"))

        -- ============================================================
        -- Dwarven Mine -- down Rolad's own trapdoor (ladders.loc
        -- category=climb_down_ladder, no maplink row: the climb keeps x and
        -- adds 6400 to z). Page 1 (kill a scorpion), page 3 (mine tin),
        -- page 2 (search the mine cart); all three auto-combine into
        -- dwarf_rock_pagex3 (dwarfrock_try_combine_pages). The two gotos
        -- below are hops between open tiles of the one mine passage
        -- (static collision with every door shut: ladder foot 3019,9849 ->
        -- 3042,9793 REACH len=81; 3055,9779 -> 3040,9819 REACH).
        -- ============================================================
        climb("enterDwarvenMine", "fai_dwarf_trapdoor_down", 3019, 3450,
            function(tt) return tt.level == 0 and tt.z > 9800 and tt.z < 9860 and tt.x >= 3012 and tt.x <= 3026 end,
            "the Dwarven Mine under the hut, x 3012-3026 z 9800-9860, level 0")

        t.exec("goto-scorpions", t.player.goto_tile, 3042, 9793, 0)
        vitals()
        local attack1_result, attack1_detail = t.player.attack("scorpion", 2, 20)
        t.check("attackScorpion", attack1_result == "ok", tostring(attack1_result) .. " " .. tostring(attack1_detail))
        vitals()
        t.exec("killScorpion", t.npc.await_dead, "scorpion", 60)
        vitals()
        local page1_await_result, page1_await_detail = t.inv.await("dwarf_rock_page1", 1, 20)
        local page1r, page1c = t.inv.count("dwarf_rock_page1")
        local pagex3r_b, pagex3c_b = t.inv.count("dwarf_rock_pagex3")
        t.check("gotPage1", page1_await_result == "ok" or pagex3c_b == 1,
            string.format("inv.await(dwarf_rock_page1,1,20) -> %s (%s); page1=%s(%s) pagex3=%s(%s)",
                tostring(page1_await_result), tostring(page1_await_detail),
                tostring(page1c), tostring(page1r), tostring(pagex3c_b), tostring(pagex3r_b)))
        margin_row("killScorpion.margin", "scorpion (level 14) in the Dwarven Mine")

        t.player.walk_to(3055, 9779, 40)
        local tin_tile_r, tin_tile = t.world.tile()
        t.check("walk-tinrock2", tin_tile_r == "ok" and math.abs(tin_tile.x - 3055) <= 1 and math.abs(tin_tile.z - 9779) <= 1,
            "walked to 3055,9779 beside tinrock2 3056,9780 -> " .. tile_text(tin_tile_r, tin_tile))
        t.exec("mineRock", t.player.click_loc, "tinrock2", 1, { at = { 3056, 9780 } })
        local page3_await_result, page3_await_detail = t.inv.await("dwarf_rock_page3", 1, 40)
        local page3r, page3c = t.inv.count("dwarf_rock_page3")
        local pagex3r_a, pagex3c_a = t.inv.count("dwarf_rock_pagex3")
        t.check("gotPage3", page3_await_result == "ok" or pagex3c_a == 1,
            string.format("inv.await(dwarf_rock_page3,1,40) -> %s (%s); page3=%s(%s) pagex3=%s(%s)",
                tostring(page3_await_result), tostring(page3_await_detail),
                tostring(page3c), tostring(page3r), tostring(pagex3c_a), tostring(pagex3r_a)))

        t.exec("goto-cart", t.player.goto_tile, 3040, 9819, 0)
        local cart_result, cart = t.world.loc_near("dwarfrock_book_cart", 6)
        t.check("locate.cart", cart_result == "ok",
            "world.loc_near(dwarfrock_book_cart,6) -> " .. loc_text(cart_result, cart))
        t.exec("searchCart", t.player.click_loc, "dwarfrock_book_cart", 1)
        local page2_await_result, page2_await_detail = t.inv.await("dwarf_rock_page2", 1, 20)
        local page2r, page2c = t.inv.count("dwarf_rock_page2")
        local pagex3r_c, pagex3c_c = t.inv.count("dwarf_rock_pagex3")
        t.check("gotPage2", page2_await_result == "ok" or pagex3c_c == 1,
            string.format("inv.await(dwarf_rock_page2,1,20) -> %s (%s); page2=%s(%s) pagex3=%s(%s)",
                tostring(page2_await_result), tostring(page2_await_detail),
                tostring(page2c), tostring(page2r), tostring(pagex3c_c), tostring(pagex3r_c)))

        local pagex3_final_result, pagex3_final_detail = t.inv.await("dwarf_rock_pagex3", 1, 10)
        local pagex3fr, pagex3fc = t.inv.count("dwarf_rock_pagex3")
        t.check("pagesCombined", pagex3_final_result == "ok" and pagex3fc == 1,
            string.format("inv.await(dwarf_rock_pagex3,1,10) -> %s (%s); count=%s(%s)",
                tostring(pagex3_final_result), tostring(pagex3_final_detail), tostring(pagex3fc), tostring(pagex3fr)))

        -- ============================================================
        -- Rolad #2 -- back up the mine ladder (it lands in Rolad's room),
        -- hand in the three pages, get the restored book.
        -- ============================================================
        t.exec("goto-mineLadder", t.player.goto_tile, 3020, 9849, 0)
        climb("goBackUpToRolad", "ladder_from_cellar_directional", 3019, 9850, in_rolads_room,
            "Rolad's room, x 3017-3023 z 3450-3454, level 0")
        -- the climb reloads the surface scene: Rolad joins the npc pool a few ticks later
        t.exec("talkToRoladWithPages.roladPresent", t.npc.await_present, "dwarfrock_rolad", 10, 10)
        t.exec("talkToRoladWithPages", t.player.talk_to, "dwarfrock_rolad", 1)
        t.exec("talkToRoladWithPages-dialog", t.chat.play, {
            "player:I found all three pages.",
            "npc:Wonderful! Let me bind them back into the book for you.",
            "mesbox:Rolad hands you the restored dwarven lore book.",
        })
        t.expect("quest.stage.book_ready", t.quest.expect_stage("book_ready"))

        -- ============================================================
        -- Read the book -- first read (opheld1, stage book_ready): a
        -- two-option choice, then the stage-advancing mesbox.
        -- ============================================================
        local book_sync_result, book_sync_detail = t.inv.await("dwarf_rock_book", 1, 10)
        t.check("bookSynced", book_sync_result == "ok", "inv.await(dwarf_rock_book,1,10) -> " .. tostring(book_sync_result) .. " " .. tostring(book_sync_detail))
        local read1_result, read1_detail = t.player.inv_op("dwarf_rock_book", 1)
        t.check("readBook", read1_result == "ok", "inv_op(dwarf_rock_book,1) -> " .. tostring(read1_result) .. " " .. tostring(read1_detail))
        t.exec("readBook-dialog", t.chat.play, {
            "choose:Read the book.",
            "mesbox:You read through the dwarven lore book.",
        })
        t.expect("quest.stage.returned_with_book", t.quest.expect_stage("returned_with_book"))

        local book2_await_result, book2_await_detail = t.inv.await("dwarf_rock_book", 1, 10)
        local book2_count_result, book2_count = t.inv.count("dwarf_rock_book")
        t.check("gotBookBack", book2_await_result == "ok" and book2_count == 1,
            "inv.await(dwarf_rock_book,1,10) after the read -> " .. tostring(book2_await_result) .. " " .. tostring(book2_await_detail)
                .. "; count=" .. tostring(book2_count_result == "ok" and book2_count or book2_count_result))

        -- ============================================================
        -- Dondakan #2 -- out of the hut on foot, the guide's route back,
        -- then the book report (stage 60).
        -- ============================================================
        rolad_out("enterDwarfCaveWithBook")
        -- Back west through the same members' gate (3015,3456 -> 2937,3450:
        -- REACH closed-doors len=222), pressed by its verb, then overland from
        -- inside Taverley (2932,3450 -> 2730,3712: REACH closed-doors len=864 at 80).
        t.exec("goto-enterDwarfCaveWithBook.memberGate", t.player.goto_tile, 2937, 3450, 0)
        t.exec("enterDwarfCaveWithBook.memberGateIn", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
            near = { 2936, 3450 }, far_ok = function(tl) return tl.x <= 2935 end,
            far_desc = "inside Taverley, x <= 2935" })
        if not to_dondakan("WithBook") then
            return
        end
        dondakan_present("talkToDondakanWithBook")
        t.exec("talkToDondakanWithBook", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakanWithBook-dialog", t.chat.play, {
            "player:I've read the whole book. It talks about a fortune in gold",
            "npc:Gold, is it? Ha! That'd explain why I've never managed to crack this rock with a pick",
            "player:Your boots?",
            "npc:Solid granite, these. I could kick a hole clean through a castle wall",
            "npc:If you've got a bit of gold on you, show me.",
        })
        t.expect("quest.stage.gold_bar_shown", t.quest.expect_stage("gold_bar_shown"))

        -- ============================================================
        -- useGoldBarOnDondakan -- item-on-npc, the ONLY writer of
        -- %dwarfrock_gold_cannonball. The bar is SHOWN, not consumed
        -- (betweenarock_dondakan.rs2:152-155, "neither is consumed here,
        -- only the later furnace smelt spends the bar"), so the row asserts
        -- the flag AND that all four bars are still held.
        -- ============================================================
        local bars0_r, bars0 = t.inv.count("gold_bar")
        dondakan_present("useGoldBarOnDondakan")
        local dondakan_book_target = t.player.by_symbol("npc", "dwarfrock_dondakan")
        t.exec("useGoldBarOnDondakan", t.player.use_on, "gold_bar", dondakan_book_target)
        t.exec("useGoldBarOnDondakan-dialog", t.chat.play, {
            "player:Here, take a look at this.",
            "npc:Haha, what am I meant to do with that? Gold's heavy and soft as butter",
            "player:The book said there's gold inside the rock.",
            "npc:Now that's not a bad thought",
            "mesbox:Dondakan agrees to try firing",
        })
        local flag_r = t.var.await_server("varb301_dwarfrock_gold_cannonball", 1, 5)
        local bars1_r, bars1 = t.inv.count("gold_bar")
        t.check("useGoldBarOnDondakan.flag", flag_r == "ok" and bars0_r == "ok" and bars1_r == "ok" and bars0 == 4 and bars1 == 4,
            "varb301_dwarfrock_gold_cannonball await 1 -> " .. tostring(flag_r) .. "; gold_bar " .. tostring(bars0) .. " -> "
                .. tostring(bars1) .. " (want 4 -> 4: shown, not spent)")

        -- ============================================================
        -- Furnace -- smelt a gold bar into the golden cannonball
        -- (dwarfrock_gold_bar_or_menu). The alcove's only way out is the
        -- second ferryman to Keldagrim, whose smithing quarter holds the
        -- furnace; use_on walks the player to it from the ferry landing
        -- (static collision: REACH len=22, no door).
        -- ============================================================
        to_keldagrim("travelToFurnaceWithFerryman")
        local furnace_result, furnace = t.world.loc_near("dwarf_keldagrim_furnace", 30)
        t.check("locate.furnace", furnace_result == "ok",
            "world.loc_near(dwarf_keldagrim_furnace,30) -> " .. loc_text(furnace_result, furnace))
        local furnace_target = t.player.by_symbol("loc", "dwarf_keldagrim_furnace")
        t.exec("makeGoldCannonball", t.player.use_on, "gold_bar", furnace_target)
        t.exec("makeGoldCannonball-dialog", t.chat.play, {
            "mesbox:You melt the gold bar and pour it into the mould",
        })
        local ball_sync_result, ball_sync_detail = t.inv.await("dwarf_rock_cannonball_gold", 1, 10)
        local ball_result, ball_count = t.inv.count("dwarf_rock_cannonball_gold")
        local bars2_r, bars2 = t.inv.count("gold_bar")
        t.check("gotCannonball", ball_sync_result == "ok" and ball_result == "ok" and ball_count == 1 and bars2_r == "ok" and bars2 == 3,
            "inv.await(dwarf_rock_cannonball_gold,1,10) -> " .. tostring(ball_sync_result) .. " " .. tostring(ball_sync_detail)
                .. "; count=" .. tostring(ball_result == "ok" and ball_count or ball_result)
                .. "; gold_bar " .. tostring(bars1) .. " -> " .. tostring(bars2) .. " (want 4 -> 3)")

        -- ============================================================
        -- Use the golden cannonball on Dondakan -- opnpcu (consumes the
        -- ball, sets %dwarfrock_fired_gold_cannonball), then a SEPARATE
        -- talk_to advances the stage. Out of Keldagrim by its real exit,
        -- then the guide's route back to the alcove.
        -- ============================================================
        leave_keldagrim("leaveKeldagrimWithCannonball")
        if not to_dondakan("WithCannonball") then
            return
        end
        dondakan_present("useGoldCannonballOnDondakan")
        local dondakan_target = t.player.by_symbol("npc", "dwarfrock_dondakan")
        t.exec("useGoldCannonballOnDondakan", t.player.use_on, "dwarf_rock_cannonball_gold", dondakan_target)
        -- if_close fires for BOTH branches of this p_choice2, so the dialogue
        -- CLOSES after the choice; a new one-page mesbox opens after p_delay(1).
        t.exec("useGoldCannonballOnDondakan-choice", t.chat.play, {
            "choose:Yes, I'm sure this will crack open the rock.",
        })
        t.await({
            level = function() return t.chat.kind() ~= "none" end,
            note = "useGoldCannonballOnDondakan.mesbox_open",
        }, 5)
        t.exec("useGoldCannonballOnDondakan-dialog", t.chat.play, {
            "mesbox:Dondakan loads the golden cannonball and fires it point blank",
        })
        local fired_r = t.var.await_server("varb313_dwarfrock_fired_gold_cannonball", 1, 5)
        local ball2_r, ball2 = t.inv.count("dwarf_rock_cannonball_gold")
        t.check("useGoldCannonballOnDondakan.fired", fired_r == "ok" and ball2_r == "ok" and ball2 == 0,
            "varb313_dwarfrock_fired_gold_cannonball await 1 -> " .. tostring(fired_r) .. "; dwarf_rock_cannonball_gold 1 -> "
                .. tostring(ball2) .. " (want 0: inv_del, betweenarock_dondakan.rs2:181)")

        dondakan_present("talkToDondakanAfterShot")
        t.exec("talkToDondakanAfterShot", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakanAfterShot-dialog", t.chat.play, {
            "player:So you want to... fire me into the rock?",
            "npc:Precisely! If I can survive being fired through",
            "player:I can't argue with that, shoot me in!",
            "npc:Right then, brace yourself!",
        })
        t.expect("quest.stage.fired_into_rock", t.quest.expect_stage("fired_into_rock"))

        dondakan_present("talkToDondakanForSchematic")
        t.exec("talkToDondakanForSchematic", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakanForSchematic-dialog", t.chat.play, {
            "player:What did you find on the other side?",
            "npc:A whole realm, hidden behind the rock!",
            "npc:Head back to the Engineer, and see if Rolad's book",
            "npc:together.",
        })
        t.expect("quest.stage.assembling_schematics", t.quest.expect_stage("assembling_schematics"))
        local s1_await = t.inv.await("dwarf_rock_schematic1", 1, 10)
        local schematic1_result, schematic1_count = t.inv.count("dwarf_rock_schematic1")
        t.check("gotSchematic1", schematic1_result == "ok" and schematic1_count == 1,
            "inv.await(dwarf_rock_schematic1,1,10) -> " .. tostring(s1_await) .. "; count " .. tostring(schematic1_result) .. " " .. tostring(schematic1_count))

        -- ============================================================
        -- Read the book a SECOND time -- the base schematic.
        -- ============================================================
        local read2_result, read2_detail = t.player.inv_op("dwarf_rock_book", 1)
        t.check("readBookAgain", read2_result == "ok", "inv_op(dwarf_rock_book,1) -> " .. tostring(read2_result) .. " " .. tostring(read2_detail))
        t.exec("readBookAgain-dialog", t.chat.play, {
            "mesbox:You turn to the last page of the book again.",
        })
        local base_await = t.inv.await("dwarf_rock_base_schematic", 1, 10)
        local base_result, base_count = t.inv.count("dwarf_rock_base_schematic")
        t.check("gotBaseSchematic", base_result == "ok" and base_count == 1,
            "inv.await(dwarf_rock_base_schematic,1,10) -> " .. tostring(base_await) .. "; count " .. tostring(base_result) .. " " .. tostring(base_count))

        -- ============================================================
        -- Engineer's schematic piece, then the golden helmet on the
        -- Keldagrim anvil (the guide's order: useGoldBarOnAnvil before
        -- enterKhorvakRoom).
        -- ============================================================
        to_keldagrim("travelBackWithFerrymanAgain")
        t.exec("talkToEngineerAgain", t.player.talk_to, "dwarfrock_engineer1", 1)
        t.exec("talkToEngineerAgain-dialog", t.chat.play, {
            "player:I need your help piecing together an old dwarven schematic.",
            "npc:Ah, I recognise this work!",
            "mesbox:The Dwarven Engineer hands you a schematic fragment.",
        })
        local s2_await = t.inv.await("dwarf_rock_schematic2", 1, 10)
        local schematic2_result, schematic2_count = t.inv.count("dwarf_rock_schematic2")
        t.check("gotSchematic2", schematic2_result == "ok" and schematic2_count == 1,
            "inv.await(dwarf_rock_schematic2,1,10) -> " .. tostring(s2_await) .. "; count " .. tostring(schematic2_result) .. " " .. tostring(schematic2_count))

        local anvil_result, anvil = t.world.loc_near("dwarf_keldagrim_anvil", 30)
        t.check("locate.anvil", anvil_result == "ok",
            "world.loc_near(dwarf_keldagrim_anvil,30) -> " .. loc_text(anvil_result, anvil))
        local bars3_r, bars3 = t.inv.count("gold_bar")
        local anvil_target = t.player.by_symbol("loc", "dwarf_keldagrim_anvil")
        t.exec("useGoldBarOnAnvil", t.player.use_on, "gold_bar", anvil_target)
        t.exec("useGoldBarOnAnvil-dialog", t.chat.play, {
            "mesbox:You carefully hammer three gold bars into a golden helmet.",
        })
        local helmet_sync_result, helmet_sync_detail = t.inv.await("dwarf_goldrock_helmet", 1, 10)
        local bars4_r, bars4 = t.inv.count("gold_bar")
        t.check("helmetSynced", helmet_sync_result == "ok" and bars3_r == "ok" and bars4_r == "ok" and bars3 == 3 and bars4 == 0,
            "inv.await(dwarf_goldrock_helmet,1,10) -> " .. tostring(helmet_sync_result) .. " " .. tostring(helmet_sync_detail)
                .. "; gold_bar " .. tostring(bars3) .. " -> " .. tostring(bars4) .. " (want 3 -> 0, betweenarock_schematics.rs2:378)")

        -- ============================================================
        -- Khorvak's schematic piece, under White Wolf Mountain. Out of
        -- Keldagrim by its real exit; the overland goto leaves the open
        -- hillside east of Rellekka and lands on the open mountainside north
        -- of the stair hut, and the stairs are walked both ways
        -- (maplink.dbrow 0_44_54_4_30 / 0_44_154_4_26).
        -- ============================================================
        leave_keldagrim("leaveKeldagrimForKhorvak")
        t.exec("goto-whiteWolfStairs", t.player.goto_tile, 2820, 3490, 0)
        climb("enterKhorvakRoom", "tunnelstairstop", 2820, 3484,
            function(tt) return tt.level == 0 and tt.z > 9800 and tt.z < 9900 end,
            "the tunnel under White Wolf Mountain, z 9800-9900, level 0 (maplink dest 2820,9882)")
        t.player.walk_to(2862, 9877, 120)
        local kr, kt = t.world.tile()
        t.check("walk-khorvak", kr == "ok" and math.abs(kt.x - 2862) <= 2 and math.abs(kt.z - 9877) <= 2,
            "walked to 2862,9877 beside Khorvak (m44_154.spawn:17, 2864,9876) -> " .. tile_text(kr, kt))
        t.exec("talkToKhorvak", t.player.talk_to, "dwarfrock_engineer2", 1)
        t.exec("talkToKhorvak-dialog", t.chat.play, {
            "player:I'm told you might have a piece of an old dwarven schematic.",
            "npc:Maybe I do, maybe I don't.",
            "choose:No, I've had enough of buying drinks for people!",
            "mesbox:Khorvak laughs and hands over his schematic fragment regardless.",
        })
        local s3_await = t.inv.await("dwarf_rock_schematic3", 1, 10)
        local schematic3_result, schematic3_count = t.inv.count("dwarf_rock_schematic3")
        t.check("gotSchematic3", schematic3_result == "ok" and schematic3_count == 1,
            "inv.await(dwarf_rock_schematic3,1,10) -> " .. tostring(s3_await) .. "; count " .. tostring(schematic3_result) .. " " .. tostring(schematic3_count))

        -- ============================================================
        -- Assemble the four schematic pieces: opheld1 on schematic1 opens
        -- the REAL per-piece 2D position puzzle. Each piece is knocked off
        -- its target by a random 12-40px offset per axis on open
        -- (dwarfrock_puzzle_dx/dy, read through t.var.server); the step and
        -- the tolerance are both 4px, so floor(|delta|/4) clicks of the
        -- reducing button always lands within tolerance -- every scramble
        -- the draw can produce is handled by the same arithmetic. No shot
        -- rows while the modal is open: its 512x334 black background reads
        -- as gate.py's pre_login fingerprint.
        -- ============================================================
        local assemble_result, assemble_detail = t.player.inv_op("dwarf_rock_schematic1", 1)
        t.step("assembleSchematic", assemble_result == "ok" and "PASS" or "FAIL",
            "inv_op(dwarf_rock_schematic1,1) -> " .. tostring(assemble_result) .. " " .. tostring(assemble_detail))
        local puzzle_open_result, puzzle_open_detail = t.ui.await_open("dwarf_rock_schematics")
        t.step("schematicPuzzle-open", puzzle_open_result == "ok" and "PASS" or "FAIL",
            "ui.await_open(dwarf_rock_schematics) -> " .. tostring(puzzle_open_result) .. " " .. tostring(puzzle_open_detail))

        local sel1_r, w_select1 = t.ui.widget("dwarf_rock_schematics_control:dr_select1")
        local sel2_r, w_select2 = t.ui.widget("dwarf_rock_schematics_control:dr_select2")
        local sel3_r, w_select3 = t.ui.widget("dwarf_rock_schematics_control:dr_select3")
        local up_r, w_up = t.ui.widget("dwarf_rock_schematics_control:dr_move_up")
        local down_r, w_down = t.ui.widget("dwarf_rock_schematics_control:dr_move_down")
        local left_r, w_left = t.ui.widget("dwarf_rock_schematics_control:dr_move_left")
        local right_r, w_right = t.ui.widget("dwarf_rock_schematics_control:dr_move_right")
        local rot_r, w_rotate = t.ui.widget("dwarf_rock_schematics_control:dr_rotate_button")
        t.step("schematicPuzzle-widgets",
            (sel1_r == "ok" and sel2_r == "ok" and sel3_r == "ok"
                and up_r == "ok" and down_r == "ok" and left_r == "ok" and right_r == "ok"
                and rot_r == "ok")
                and "PASS" or "FAIL",
            string.format("select1=%s select2=%s select3=%s up=%s down=%s left=%s right=%s rotate=%s",
                tostring(sel1_r), tostring(sel2_r), tostring(sel3_r),
                tostring(up_r), tostring(down_r), tostring(left_r), tostring(right_r), tostring(rot_r)))

        -- dr_move_left/right subtract/add the step from dx, up/down from dy;
        -- dr_rotate_button adds 1 (mod 4) to rot for every SELECTED piece, and
        -- rotation must reach 0 before position matters. Nudge and rotate act
        -- on every selected piece, so a finished piece is deselected before
        -- the next is selected.
        local puzzle_pieces = {
            { n = 1, select = w_select1, dx = "varp7153_dwarfrock_puzzle_dx1", dy = "varp7156_dwarfrock_puzzle_dy1", rot = "varp7168_dwarfrock_puzzle_rot1" },
            { n = 2, select = w_select2, dx = "varp7154_dwarfrock_puzzle_dx2", dy = "varp7157_dwarfrock_puzzle_dy2", rot = "varp7169_dwarfrock_puzzle_rot2" },
            { n = 3, select = w_select3, dx = "varp7155_dwarfrock_puzzle_dx3", dy = "varp7158_dwarfrock_puzzle_dy3", rot = "varp7170_dwarfrock_puzzle_rot3" },
        }
        for _, piece in ipairs(puzzle_pieces) do
            t.ui.invoke(piece.select, 1) -- select ON (togglebit)

            local before_rot_result, before_rot = t.var.server(piece.rot)
            if before_rot_result == "ok" and before_rot ~= 0 then
                local rot_clicks = (4 - before_rot) % 4
                for _ = 1, rot_clicks do
                    t.ui.invoke(w_rotate, 1)
                end
            end
            -- if_click is delivered on a server tick: tick before reading back.
            t.ticks(2)
            local after_rot_result, after_rot = t.var.server(piece.rot)

            local before_dx_result, before_dx = t.var.server(piece.dx)
            local before_dy_result, before_dy = t.var.server(piece.dy)
            if before_dx_result == "ok" and before_dx ~= 0 then
                local dx_clicks = math.floor(math.abs(before_dx) / 4)
                local dx_widget = before_dx > 0 and w_left or w_right
                for _ = 1, dx_clicks do
                    t.ui.invoke(dx_widget, 1)
                end
            end
            if before_dy_result == "ok" and before_dy ~= 0 then
                local dy_clicks = math.floor(math.abs(before_dy) / 4)
                local dy_widget = before_dy > 0 and w_up or w_down
                for _ = 1, dy_clicks do
                    t.ui.invoke(dy_widget, 1)
                end
            end
            t.ticks(2)
            local after_dx_result, after_dx = t.var.server(piece.dx)
            local after_dy_result, after_dy = t.var.server(piece.dy)
            t.step("schematicPuzzle-piece" .. piece.n,
                (after_rot_result == "ok" and after_rot == 0
                    and after_dx_result == "ok" and after_dy_result == "ok"
                    and math.abs(after_dx) <= 4 and math.abs(after_dy) <= 4)
                    and "PASS" or "FAIL",
                string.format("select dr_select%d, rotate piece %d: rot %s -> %s, move dx %s -> %s, dy %s -> %s (tolerance 4)",
                    piece.n, piece.n, tostring(before_rot), tostring(after_rot),
                    tostring(before_dx), tostring(after_dx), tostring(before_dy), tostring(after_dy)))

            t.ui.invoke(piece.select, 1) -- select OFF (toggle back) before the next piece
        end

        local solved_result, solved_value = t.var.server("varb305_dwarfrock_schematics_solved")
        t.step("schematicPuzzle-solved", (solved_result == "ok" and solved_value == 1) and "PASS" or "FAIL",
            "var.server(dwarfrock_schematics_solved) -> " .. tostring(solved_result) .. " " .. tostring(solved_value))

        -- dwarf_rock_close_button is buttontype=3 (trap 33): ESCAPE is the real close.
        local close_key_result = t.key("escape")
        t.step("schematicPuzzle-closeKey", close_key_result == "ok" and "PASS" or "FAIL",
            "key(escape) -> " .. tostring(close_key_result))
        local closed_result, closed_detail = t.ui.await_close("dwarf_rock_schematics")
        local modal_r, modal_after = t.ui.is_modal()
        t.check("schematicPuzzle-closed", closed_result == "ok" and modal_r == "ok" and modal_after == false,
            "ui.await_close(dwarf_rock_schematics) -> " .. tostring(closed_result) .. " " .. tostring(closed_detail)
                .. "; ui.is_modal() -> " .. tostring(modal_r) .. " " .. tostring(modal_after))

        local assembled_await = t.inv.await("dwarf_rock_schematic_assembled", 1, 10)
        local assembled_result, assembled_count = t.inv.count("dwarf_rock_schematic_assembled")
        t.check("gotAssembledSchematic", assembled_result == "ok" and assembled_count == 1,
            "inv.await(dwarf_rock_schematic_assembled,1,10) -> " .. tostring(assembled_await) .. "; count "
                .. tostring(assembled_result) .. " " .. tostring(assembled_count))

        -- ============================================================
        -- Back up the stairs, then the guide's route to Dondakan.
        -- ============================================================
        t.player.walk_to(2820, 9882, 120)
        climb("leaveKhorvakRoom", "tunnelstairs", 2820, 9883,
            function(tt) return tt.level == 0 and tt.z > 3470 and tt.z < 3500 end,
            "White Wolf Mountain at the stair hut, z 3470-3500, level 0 (maplink dest 2820,3486)")
        t.player.walk_to(2820, 3490, 20)
        local outr, outt = t.world.tile()
        t.check("leaveKhorvakRoom.outside", outr == "ok" and outt.z >= 3489 and outt.level == 0,
            "walked out of the stair hut to 2820,3490 -> " .. tile_text(outr, outt))

        -- ============================================================
        -- Dondakan #3 -- fire into the realm. With the helmet held but not
        -- worn, dondakan.rs2's stage-80 branch answers "Best wear that golden
        -- helmet" (betweenarock_dondakan.rs2's inv_total(worn, ...) = 0 case);
        -- worn, it reaches "Ready as I'll ever be.".
        -- ============================================================
        if not to_dondakan("WithHelmet") then
            return
        end
        dondakan_present("talkToDondakanWithHelmet-probe")
        t.exec("talkToDondakanWithHelmet-probe", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakanWithHelmet-probe-dialog", t.chat.play, {
            "npc:Best wear that golden helmet before I fire you in",
        })

        t.exec("equip.helmet", t.player.equip, "dwarf_goldrock_helmet")

        dondakan_present("talkToDondakanForEnd")
        t.exec("talkToDondakanForEnd", t.player.talk_to, "dwarfrock_dondakan", 1)
        -- the stage-80 branch with the helmet worn and the assembled schematic
        -- held: a page that is not "Ready as I'll ever be." fails this row
        t.exec("talkToDondakanWithHelmet-dialog", t.chat.play, {
            "player:Ready as I'll ever be.",
            "npc:You may fire when ready!",
        })
        t.expect("quest.stage.in_the_realm", t.quest.expect_stage("in_the_realm"))
        t.exec("enterRealm-dialog", t.chat.play, {
            "mesbox:Dondakan fires you clean through the rock!",
        })

        -- ============================================================
        -- The Arzinian realm -- mine at least 6 gold ore (click_loc walks to
        -- the rock from the landing; no goto), then the central wall of flame.
        -- ============================================================
        t.settle()
        local lr0, lt0 = t.world.tile()
        t.check("realm.landed", lr0 == "ok" and lt0.x == 2320 and lt0.z == 4950 and lt0.level == 0,
            "landed " .. tile_text(lr0, lt0) .. " (want 2320,4950,0: betweenarock_realm.rs2:49 p_teleport(0_36_77_16_22))")
        -- A gold rock gives one ore and depletes, and a bare click_loc picks
        -- copies across the realm's walls ("I can't reach that!", run 2). So
        -- the presses go round the copies the landing's walkable area touches
        -- (static collision, maps/m36_77.jl2), nearest first, each pressed by
        -- its own tile: stay on a rock until it pays or stops answering.
        -- Only the goldrock1 copies: skill_mining/configs/mine.dbrow:80-90 maps
        -- goldrock2 to perfect_gold_ore (Family Crest's Witchaven rocks), which
        -- the flame's inv_total(inv, gold_ore) (betweenarock_realm.rs2:151,172)
        -- does not count -- OPEN content bug: half the realm's rocks pay the
        -- wrong ore. With seven paying rocks at Mining 40 and the realm's
        -- 800-tick stay (betweenarock.constant:73-74, 16 x 50 ticks), the
        -- guide's optional 15 ore (level-75 Avatar) does not fit; this test
        -- takes the guide's 6 (level-125 Avatar) and prays against it.
        local gold_rocks = {
            { "goldrock1", 2311, 4954 }, { "goldrock1", 2310, 4957 }, { "goldrock1", 2323, 4941 },
            { "goldrock1", 2327, 4938 }, { "goldrock1", 2334, 4951 }, { "goldrock1", 2344, 4953 },
            { "goldrock1", 2343, 4946 },
        }
        local rock_i, presses, tries, unanswered_run = 1, 0, 0, 0
        local passed_over = ""
        local ore_result, ore_count = t.inv.count("gold_ore")
        while (ore_result ~= "ok" or ore_count < 6) and tries < 60 do
            tries = tries + 1
            local rock = gold_rocks[rock_i]
            local _, before_ore_count = t.inv.count("gold_ore")
            local cr, cd = t.player.click_loc(rock[1], 1, { at = { rock[2], rock[3] } })
            if cr == "ok" then
                presses = presses + 1
                unanswered_run = 0
                local ar = t.inv.await("gold_ore", (before_ore_count or 0) + 1, 40)
                ore_result, ore_count = t.inv.count("gold_ore")
                t.note("mine6GoldOre press " .. presses .. ": click_loc(" .. rock[1] .. " at " .. rock[2] .. "," .. rock[3] .. ") -> ok "
                    .. tostring(cd) .. "; gold_ore " .. tostring(before_ore_count) .. " -> " .. tostring(ore_count) .. " (" .. tostring(ar) .. ")"
                    .. (passed_over ~= "" and ("; passed over (depleted/unanswered): " .. passed_over) or ""))
                passed_over = ""
                if ar == "ok" then
                    rock_i = rock_i % #gold_rocks + 1 -- one ore depletes the rock
                end
            else
                passed_over = passed_over .. " " .. rock[2] .. "," .. rock[3] .. "=" .. tostring(cr)
                rock_i = rock_i % #gold_rocks + 1
                unanswered_run = unanswered_run + 1
                if unanswered_run >= #gold_rocks then
                    t.ticks(10) -- every paying rock is depleted: wait for a respawn (rock_respawnrate 200)
                    unanswered_run = 0
                end
            end
        end
        local pr_r, perfect = t.inv.count("perfect_gold_ore")
        t.check("mine6GoldOre", ore_result == "ok" and ore_count >= 6,
            "inv.count(gold_ore) after " .. tostring(presses) .. " press(es) of " .. tostring(tries) .. " tries -> "
                .. tostring(ore_result) .. " " .. tostring(ore_count) .. " (want >= 6, ^dwarfrock_gold_ore_needed); perfect_gold_ore "
                .. tostring(pr_r == "ok" and perfect or pr_r) .. " (goldrock2 not pressed)")

        -- ============================================================
        -- talkToSecondFlame. Every central wall of flame lies inside a ring
        -- of outer walls of flame the landing area cannot walk through
        -- (static collision: the landing's component, 429 tiles, touches no
        -- dwarf_firewall_centre_* copy). The nearest crossing is the outer
        -- wall at 2372,4939 (south edge), from 2372,4938: a walk on through
        -- is refused by the wall, and its op1 Jump-through
        -- (betweenarock_realm.rs2:101, @dwarfrock_jump_firewall) carries the
        -- player to the far tile. Then the centre ring's op2 Talk-to (:85-89,
        -- the guide's "talk to the second set").
        -- ============================================================
        t.player.walk_to(2372, 4938, 150)
        local wr0, wt0 = t.world.tile()
        t.check("walk-outerFlame", wr0 == "ok" and wt0.level == 0 and wt0.x == 2372 and wt0.z == 4938,
            "walked to 2372,4938 south of the outer wall of flame 2372,4939 -> " .. tile_text(wr0, wt0))
        local pw_r, pw_d = t.player.walk_to(2372, 4942, 10)
        local wr1, wt1 = t.world.tile()
        t.check("walk-outerFlameBlocks", wr1 == "ok" and wt1.z <= 4938,
            "walk_to(2372,4942) through the wall of flame -> " .. tostring(pw_r) .. " " .. tostring(pw_d) .. "; now "
                .. tile_text(wr1, wt1) .. " (want still south of it, z <= 4938: the wall blocks a walk)")
        local jump_r, jump_d = t.player.click_loc("dwarf_firewall_straight", 1, { at = { 2372, 4939 } })
        await_tile(function(tt) return tt.level == 0 and tt.z >= 4939 end, 6, "jumpOuterFlame")
        local wr2, wt2 = t.world.tile()
        t.check("jumpOuterFlame", (jump_r == "ok" or jump_r == "timeout") and wr2 == "ok" and wt2.level == 0
                and wt2.x == 2372 and wt2.z == 4939,
            "from " .. tile_text(wr1, wt1) .. " click_loc(dwarf_firewall_straight op1 Jump-through at 2372,4939) -> "
                .. tostring(jump_r) .. " " .. tostring(jump_d) .. "; now " .. tile_text(wr2, wt2)
                .. " (want the far tile 2372,4939,0, inside the outer ring: [proc,dwarfrock_firewall_far_side])")
        if not (wr2 == "ok" and wt2.z >= 4939) then
            return
        end

        -- Protect from Magic goes UP before the Avatar exists: it spawns
        -- aggressive on the flame's Talk-to, and npc_combat_magic.rs2:74-79
        -- reads the prayer when it swings (its attack animation), so a prayer
        -- switched on after seeing the first cast is too late. Prayer points
        -- are read before the fight (staged 37, nothing has drained them).
        local ptab_r, ptab_d = t.ui.tab("prayer")
        t.ticks(2)
        local pw_widget_r, pw_widget = t.ui.widget("prayerbook:prayer13")
        t.ui.invoke(pw_widget, 1)
        t.ticks(2)
        local pon_r, pon = t.var.varbit("varb4116_prayer_protectfrommagic")
        local ppts_r, ppts = t.skill.read("prayer")
        t.check("killAvatar.protectFromMagic", ptab_r == "ok" and pw_widget_r == "ok" and pon_r == "ok" and pon == 1
                and ppts_r == "ok" and ppts.level >= 30,
            "prayer tab -> " .. tostring(ptab_r) .. " " .. tostring(ptab_d) .. "; prayerbook:prayer13 -> " .. tostring(pw_widget_r)
                .. "; varb4116_prayer_protectfrommagic " .. tostring(pon) .. " (want 1); prayer points "
                .. tostring(ppts_r == "ok" and (ppts.level .. "/" .. ppts.base_level) or ppts_r) .. " (want >= 30 before the fight)")
        t.ui.tab("inventory")
        t.ticks(1)

        local approach_result, approach_detail = t.player.click_loc("dwarf_firewall_centre_diagonal", 2)
        local approach_sym = "dwarf_firewall_centre_diagonal"
        if approach_result ~= "ok" then
            approach_result, approach_detail = t.player.click_loc("dwarf_firewall_centre_straight", 2)
            approach_sym = "dwarf_firewall_centre_straight"
        end
        t.check("talkToSecondFlame", approach_result == "ok",
            "click_loc(" .. approach_sym .. ", op2 Talk-to) -> " .. tostring(approach_result) .. " " .. tostring(approach_detail))
        t.exec("talkToSecondFlame-dialog", t.chat.play, {
            "mesbox:The flames roar and a guardian of the realm steps forth",
        })

        -- dwarfrock_spawn_avatar lands the Avatar at the FIXED coord
        -- 0_37_77_7_25 (2375,4953), inside the ring.
        t.player.walk_to(2374, 4953, 20)

        local avatar_candidates = { "dwarf_rock_avatar_mage", "dwarf_rock_avatar_mage_green", "dwarf_rock_avatar_mage_yellow" }
        local avatar_sym = nil
        local avatar_present_result = "not_found"
        for _, candidate in ipairs(avatar_candidates) do
            avatar_present_result = t.npc.await_present(candidate, 10, 10)
            if avatar_present_result == "ok" then
                avatar_sym = candidate
                break
            end
        end
        t.check("avatarPresent", avatar_sym ~= nil,
            "tried " .. table.concat(avatar_candidates, ", ") .. " -> resolved " .. tostring(avatar_sym)
                .. " (" .. tostring(avatar_present_result) .. ")")
        if avatar_sym == nil then
            return
        end

        -- The kill is polled on the QUEST VARP (^dwarfrock_avatar_defeated=100),
        -- not npc-pool presence; hitpoints are sampled and food eaten between
        -- presses.
        -- Prayer is sampled with hitpoints; below 10 points a dose of the
        -- prayer potion is drunk (prayer is not assumed to regenerate).
        local prayer_low, prayer_doses = nil, 0
        local function prayer_watch()
            local rr, rp = t.skill.read("prayer")
            if rr == "ok" and type(rp) == "table" and rp.level then
                if prayer_low == nil or rp.level < prayer_low then
                    prayer_low = rp.level
                end
                if rp.level < 10 then
                    for _, dose in ipairs({ "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore" }) do
                        local cr, cnt = t.inv.count(dose)
                        if cr == "ok" and cnt >= 1 then
                            if t.player.inv_op(dose, 1) == "ok" then
                                prayer_doses = prayer_doses + 1
                            end
                            t.ticks(1)
                            break
                        end
                    end
                end
            end
        end
        local kill_attempts = 0
        local kill_stage_result = "refused"
        vitals()
        while kill_stage_result ~= "ok" and kill_attempts < 12 do
            kill_attempts = kill_attempts + 1
            local atk_result, atk_detail = t.player.attack(avatar_sym, 2, 30)
            t.check("attackAvatar-" .. kill_attempts, atk_result == "ok", tostring(atk_result) .. " " .. tostring(atk_detail))
            vitals()
            -- poll the stage one tick at a time so hitpoints are sampled
            -- (and food eaten) through the whole fight, not only around it
            for _ = 1, 15 do
                kill_stage_result = t.var.await_server("varb299_dwarfrock_quest", 100, 1)
                vitals()
                prayer_watch()
                if kill_stage_result == "ok" then break end
            end
        end
        t.check("killAvatar", kill_stage_result == "ok",
            "dwarfrock_quest var.await_server(...,100) polled up to 15 ticks per press, after " .. tostring(kill_attempts)
                .. " attackAvatar attempt(s) -> " .. tostring(kill_stage_result))
        local pend_r, pend = t.var.varbit("varb4116_prayer_protectfrommagic")
        t.note("Protect from Magic " .. tostring(pend_r == "ok" and pend or pend_r) .. " at the kill; lowest prayer "
            .. tostring(prayer_low) .. ", prayer potion doses drunk " .. prayer_doses)
        margin_row("killAvatar.margin", "Arzinian Avatar (" .. tostring(avatar_sym) .. ")")
        if kill_stage_result ~= "ok" then
            return
        end

        t.expect("quest.stage.avatar_defeated", t.quest.expect_stage("avatar_defeated"))
        t.exec("avatarDefeated-dialog", t.chat.play, {
            "mesbox:The guardian collapses!",
        })
        -- dwarfrock_avatar_death (betweenarock_realm.rs2:211) returns the
        -- player to Dondakan's side of the rock: no goto.
        local br, bt = t.world.tile()
        t.check("avatarDefeated.returned", br == "ok" and bt.x == 2823 and bt.z == 10165 and bt.level == 0,
            "after the kill -> " .. tile_text(br, bt) .. " (want 2823,10165,0: betweenarock_realm.rs2:211)")

        -- The fight is over: switch the protection off so it stops draining.
        local poff_tab = t.ui.tab("prayer")
        t.ticks(2)
        local poff_wr, poff_w = t.ui.widget("prayerbook:prayer13")
        t.ui.invoke(poff_w, 1)
        t.ticks(2)
        local poff_r, poff = t.var.varbit("varb4116_prayer_protectfrommagic")
        t.check("killAvatar.prayerOff", poff_tab == "ok" and poff_wr == "ok" and poff_r == "ok" and poff == 0,
            "prayer tab -> " .. tostring(poff_tab) .. "; prayerbook:prayer13 -> " .. tostring(poff_wr)
                .. "; varb4116_prayer_protectfrommagic " .. tostring(poff) .. " (want 0 after the kill)")
        t.ui.tab("inventory")
        t.ticks(1)

        -- ============================================================
        -- Reward snapshot before the hand-in, then finish the quest.
        -- ============================================================
        local _, reward_before = t.skill.snapshot()
        local rune_pickaxe_before_result, rune_pickaxe_before = t.inv.count("rune_pickaxe")

        dondakan_present("finishQuest")
        t.exec("finishQuest", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:It's done -- the guardian is defeated.",
            "npc:By my beard! You actually did it!",
        })

        t.quest.expect_complete()

        t.check("reward.defence", t.skill.expect_gain("defence", 5000, reward_before))
        t.check("reward.mining", t.skill.expect_gain("mining", 5000, reward_before))
        t.check("reward.smithing", t.skill.expect_gain("smithing", 5000, reward_before))
        local rune_pickaxe_after_result, rune_pickaxe_after = t.inv.count("rune_pickaxe")
        t.check("reward.rune_pickaxe",
            rune_pickaxe_before_result == "ok" and rune_pickaxe_after_result == "ok"
                and rune_pickaxe_after == rune_pickaxe_before + 1,
            string.format("rune_pickaxe %s -> %s (want +1), reads %s/%s",
                tostring(rune_pickaxe_before), tostring(rune_pickaxe_after),
                tostring(rune_pickaxe_before_result), tostring(rune_pickaxe_after_result)))

        t.finish(0)
    end,
}
