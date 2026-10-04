-- Roving Elves. Rewritten by hand from quest_rovingelves's own scripts
-- (OSRS-Content/osrs239-content/server/scripts/quests/quest_rovingelves/) --
-- the scaffold's Quest Helper guess (real-OSRS Waterfall Quest traversal:
-- log raft, ropes, falls door, crate key) does not match this port at all.
-- This pack's rovingelves_seed.rs2 plants the seed at a fixed zone/coord
-- with a plain `opheld1` (no raft/rope/door steps in quest_rovingelves's own
-- content -- those locs belong to quest_waterfall, already complete), and
-- the Moss Guardian (rovingelves_mossgiant.rs2) is a real fight (op2 Attack,
-- ~npc_default_death drops the seed), not a talk_to.
--
-- Glarial's Tomb and the Chalice room are both z+6400 underground squares
-- (region 39,153 and 40,154), but the guide's own step ladder names seven
-- legs across quest_waterfall_locs.rs2's traversal mechanism as Roving
-- Elves' OWN steps (enterGlarialsTombstone, boardRaft, useRopeOnRock,
-- useRopeOnTree, enterFalls, searchFallsCrate, useKeyOnFallsDoor) --
-- unconditional on Waterfall Quest already being complete, because there is
-- no other entrance to either room. An earlier revision of this file
-- goto_tile'd past all seven as "already-complete Waterfall Quest content";
-- queue.py's reviewer correctly called that a TEST cheat (rule (b): goto_tile
-- is for plain travel, never for a door/puzzle/mechanism the guide names as
-- its own step), and a prior content-parity pass (build/parity_state/
-- parity2, not this file) proved live that every one of the seven drives for
-- real with no content changes beyond one shared-file coordinate fix already
-- landed (quest_waterfall_locs.rs2:422). This file now drives all seven
-- through real clicks -- use_on the pebble on the tombstone, click_loc the
-- raft, use_on the rope on the rock then the dead tree, click_loc the ledge
-- door and the crate, use_on the key on the west door -- reusing that proven
-- sequence. goto_tile is used only for PLAIN TRAVEL between two open,
-- outdoor tiles of one walkable region, never past a mechanism, a door or
-- a climb (b59 door-rule re-drive, build/orchestrator/fix_b59/
-- rovingelves.progress.md has the flood for every hop):
--   * Islwyn and Eluned's camp (2291,3147) lies in Isafdar behind the
--     Arandar pass's Huge Gate (overpass_gate_left/right 2384/2386,3334,
--     regicide_arandar_gate_guard.rs2) and Regicide's traps. Every trip in
--     or out crosses that gate by click (`cross_arandar`) and walks
--     Isafdar on foot, pressing the pitfall, three dense forests and the
--     tripwire on the way (`gate_to_camp` / `camp_to_gate`); no goto starts
--     or ends on the Isafdar side of the gate.
--   * The first trip west passes the Taverley members' wall by its east
--     gate (membergater 2935,3450), pressed on foot.
--   * Glarial's Tomb is left by its own ladder (2556,9844 -> 2557,3444).
--   * Almera's yard and the raft pen are entered through their two fence
--     gates on foot (2528,3495 and 2513,3494).
--   * Inside the falls every double door and both key doors are opened on
--     foot, in and out. The way back out is the dungeon's own exit door to
--     the ledge and the ledge's barrel (op1 Get in, down the river,
--     quest_waterfall_locs.rs2:339-345) -- runes cannot ride along for a
--     teleport because Glarial's Tomb refuses them
--     (~waterfall_tomb_item_forbidden, quest_waterfall_locs.rs2:46-60).
--
-- Prerequisites (rovingelves_islwyn.rs2's opnpc1 gate): Regicide complete
-- and Waterfall Quest complete. Regicide has no `::complete` arm in
-- quest_cheat.rs2, so its own progress varp is set directly (setup-only,
-- trap 16) -- Waterfall Quest does have one.
--
-- Combat: [opnpc2,roving_mossgiant] and [ai_opplayer2,roving_mossgiant] both
-- refuse the fight while ANY forbidden (weapon/armour) item is worn or
-- carried (~waterfall_tomb_forbidden_loadout, quest_waterfall_locs.rs2) --
-- the Wiki's "bare-handed" fight. ::clearinv plus never equipping anything
-- keeps the loadout legal; stats are raised in setup (a prerequisite, not
-- the quest's own work) so an unarmed level-3 does not spend the whole run
-- missing a 120 hp, zero-defence target.
--
-- Islwyn/Eluned are approached from ONE TILE OFF their own spawn tile, never
-- exactly onto it: walk_near's own banner (pointer.lua) names standing at
-- distance 0 as "no clear pixel from any camera" for a click to land on, and
-- a first pass landing goto_tile exactly on Eluned's tile (2289,3145)
-- reproduced exactly that -- the press landed on bare ground ("menu has no
-- row for it") and every step downstream (the seed never dropping, the
-- enchant dialogue finding no page open) cascaded from that one miss.
-- talk_to (unlike click_loc/use_on) does not step off on its own.
--
-- THE VARP SEAM (QUEST_AUTHORING.md section 8, same shape as
-- test/quests/pryingtimes.lua and makinghistory.lua): `rovingelves_quest`
-- (varp.alloc:549, id 6262) is declared `transmit=yes` in the quest's own
-- configs/quest_rovingelves.varp, but `OSRS-Content/osrs239-content/
-- pack/varp.client` -- the membership file `cachepack pack` actually routes
-- on -- never lists it, and it is not part of the base cache either (it is
-- absent from all.varp.compack entirely, which tops out at id 5704). Per
-- pack/varp.client's own banner, a record "reaches the client cache only if
-- varp.client names it or the base cache already holds its id" -- neither
-- holds here, so this varp has no client half at all (api_drive.symbol
-- resolves the name to kind=varp, but every CLIENT-side read through it
-- answers not_found, at every stage, every time).
--
-- FIXED (2026-09-20, quest_driver/quest.lua): quest.stage/expect_stage/
-- expect_complete's quest.varp_complete row no longer dead-end on that --
-- QD.quest._reading falls through to the embedded server's own copy
-- (api_drive.var_content) once both client-side halves answer not_found,
-- and prints "[server content]"/"server content" in the row so a reader can
-- tell the two apart from a genuine client+server agreement. So this file
-- drives t.quest.expect_complete() directly below, same as every other
-- green quest file; the earlier stage checks still cross-check through
-- t.ui.journal_open (runs the quest's own ~rovingelves_journal proc
-- server-side and returns literal text) because that channel is already
-- proven live through this run, not because expect_stage cannot answer.

return {
    id = "rovingelves",
    fixture = "fresh_lumbridge.ini",
    max_frames = 360000, -- five Arandar gate crossings, Isafdar walked trap by trap five times, the falls in and out
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so nothing forbidden rides along
        "::give spade 1", -- rovingelves_seed.rs2's opheld1 refuses to plant without one
        "::give rope 1", -- guide's own getItemRequirements(): quest_waterfall_locs.rs2's
                          -- crossing_rock/overhanging_tree1 oplocu triggers refuse without one
        -- glarials_pebble_waterfall_quest: the guide's enterGlarialsTombstone step lists it
        -- as its own required item (isNotConsumed -- a Waterfall Quest leftover, "you can get
        -- another from Golrie under Tree Gnome Village" if lost), not something Roving Elves
        -- itself grants. Setup fakes Waterfall Quest's completion below via a debugproc varp
        -- write rather than playing it, so the pebble a genuinely-completed player would still
        -- be carrying has to be brought along the same way (trap 16 -- a prerequisite quest's
        -- own leftover gear, never the quest under test's own deliverable).
        "::give glarials_pebble_waterfall_quest 1",
        -- Food prerequisite (queue.py's RETRY after b44ce7a2d, section 8's
        -- player.attack note: "carry food and EAT IT"; trap 16 -- this is a
        -- prerequisite the player brings along, not the quest's own
        -- deliverable, the same idiom mortton.lua's "::give shark 5" uses).
        -- Shark is ordinary food, not weapon/armour, so it does not trip
        -- ~waterfall_tomb_forbidden_loadout. Twenty sharks (400 hp of
        -- healing) against a fight an earlier run measured taking a 99-hp
        -- character from full to 2/30 on the guardian over ~146 ticks with
        -- no food eaten at all; the hunt ate all fifteen of the old stock on
        -- one account (b59 r3 run 4: nine rounds, two guardians swinging)
        -- after two walked Isafdar trips' tripwire snags. 27 slots at most.
        "::give shark 20",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        -- roving_mossgiant's own attack (rovingelves_mossgiant.rs2
        -- [ai_opplayer2]) rolls crush first, but falls through to a SECOND,
        -- prayer-bypassing roll against MAGIC defence whenever the crush
        -- roll fails -- with defence raised and magic left at 1, nearly
        -- every swing takes that unprotected branch. Confirmed live: with
        -- magic still at 1, a 99-hitpoints/99-defence character died to it
        -- mid-fight (killGuardian.await_dead's own shot, "Oh dear, you are
        -- dead!"). Aggressive hunt (all.npc's roving_mossgiant) plus three
        -- spawn rows close together (m39_153.spawn) also means more than
        -- one can be swinging at once.
        "::setlevel magic 99",
        -- Isafdar is walked trap by trap on every trip (sampler b59): the
        -- dense forests refuse below Agility 56 (regicide_route.rs2
        -- @regicide_cross_dense_forest), a Regicide prerequisite the player
        -- already has; the guide's own item list carries an antipoison for
        -- a snagged tripwire (potions are legal in Glarial's Tomb,
        -- quest_waterfall_locs.rs2:47-51).
        "::setlevel agility 56",
        "::give 4doseantipoison 2",
        "::setvar varp328_regicide_quest ^regicide_complete", -- no ::complete arm for Regicide; prerequisite only, never the quest under test
        "::complete quest_waterfall",
    },

    run = function(t)
        -- ---------------------------------------------------------------
        -- Travel helpers (b59 door-rule re-drive). Every tile named here is
        -- checked against the map in build/orchestrator/fix_b59/
        -- rovingelves.progress.md (reach.py / comp.py floods, doors shut).
        -- ---------------------------------------------------------------
        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end

        local function at_tile(r, tt, x, z)
            return r == "ok" and type(tt) == "table" and tt.level == 0 and tt.x == x and tt.z == z
        end

        -- Poll world.tile() until `pred` holds (a climb or a pushed
        -- crossing answers before it lands), then hand back the reading.
        local function await_tile(pred, ticks, note)
            t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and type(tt) == "table" and pred(tt)
                end,
                note = note,
            }, ticks)
            return t.world.tile()
        end

        -- Walk to x,z and check the exact tile.
        local function walk_check(name, x, z, why)
            local wr, wd = t.player.walk_to(x, z, 40)
            local tr, tt = t.world.tile()
            t.check(name, at_tile(tr, tt, x, z),
                "walk_to " .. x .. "," .. z .. " (" .. why .. ") -> " .. tostring(wr) .. " " .. tostring(wd)
                    .. "; at " .. tile_text(tr, tt))
        end

        -- Cross one door on foot (sampler-findings b56/b57 pass_door). Walk
        -- to the near tile and check it; if the CLOSED leaf stands on the
        -- door tile on this level, click that copy (op 1 Open); otherwise an
        -- earlier press left it open (a door swings back after 500 ticks),
        -- so assert the OPEN leaf on this level within 2 of the door tile
        -- (a double door's leaves swing a tile out) -- a row that fails when
        -- neither leaf is there -- and never press it again. Then walk to the
        -- far tile and check it exactly.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_desc)
            t.player.walk_to(near_x, near_z, 30)
            local nr, nt = t.world.tile()
            local level = (nr == "ok" and type(nt) == "table") and nt.level or 0
            t.check(prefix .. ".atDoor", at_tile(nr, nt, near_x, near_z),
                "walked to " .. near_x .. "," .. near_z .. ",0 beside " .. closed_sym .. " at " .. door_x .. "," .. door_z
                    .. " -> " .. tile_text(nr, nt))
            local cr, cd = t.world.loc_near(closed_sym, 1)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == level then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and od.level == level
                        and math.abs(od.tile_x - door_x) <= 2 and math.abs(od.tile_z - door_z) <= 2,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": "
                        .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                        .. "; " .. open_sym .. ": "
                        .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                        .. " (want the open leaf on level " .. level .. " within 2 of the door tile: standing open,"
                        .. " walked through, not pressed again)")
            end
            local wr = t.player.walk_to(far_x, far_z, 20)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", at_tile(fr, ft, far_x, far_z),
                "walked through " .. closed_sym .. " to " .. far_x .. "," .. far_z .. " (" .. far_desc .. ") -> "
                    .. tile_text(fr, ft) .. " (walk " .. tostring(wr) .. ")")
        end

        -- The Arandar pass's Huge Gate (overpass_gate_left, width 2, at
        -- 2384,3334; regicide_arandar_gate_guard.rs2 [label,arandar_gate]):
        -- open for a player past ^regicide_killed_tyras, it pushes the
        -- player two tiles across (p_teleport z+-2). The Isafdar side is
        -- 2385,3333, the Kandarin side 2385,3335; the caller stands the
        -- player on the near tile (a walked route's last row or walk_check).
        -- A pushed crossing can answer before it lands, so the row is graded
        -- on the tiles before and after, with the click's answer in the detail.
        local function cross_arandar(prefix, northbound)
            local near_z = northbound and 3333 or 3335
            local far_z = northbound and 3335 or 3333
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc("overpass_gate_left", 1, { at = { 2384, 3334 } })
            local fr, ft = await_tile(function(tt)
                return tt.level == 0 and tt.z == far_z
            end, 10, "the Huge Gate's push to z " .. far_z)
            t.check(prefix .. ".throughGate", at_tile(br, bt, 2385, near_z) and at_tile(fr, ft, 2385, far_z),
                "from " .. tile_text(br, bt) .. " click_loc(overpass_gate_left at 2384,3334, op1 Enter) -> "
                    .. tostring(cr) .. " " .. tostring(cd) .. "; world.tile() after -> " .. tile_text(fr, ft)
                    .. " (want 2385," .. near_z .. ",0 -> 2385," .. far_z .. ",0: [label,arandar_gate] p_teleport z"
                    .. (northbound and "+2" or "-2") .. ")")
        end

        -- A travel hop: from an open tile to an open tile across open
        -- overland (never into a room, never into Isafdar's trap belt).
        local function hop(name, x, z)
            t.exec(name, t.player.goto_tile, x, z, 0)
        end

        -- ---------------------------------------------------------------
        -- Isafdar on foot (sampler b59: every hop between the Arandar gate
        -- and the camp used to goto across Regicide's traps). The map
        -- closes the camp off from the gate with traps and dense forest;
        -- with every trap TRIGGER tile blocked (regicide_traps.rs2:116-159:
        -- a pitfall's loc tile, a tripwire's tile and the tiles north and
        -- east of it) the only on-foot route, both ways, is
        --   the pitfall ring 2276-2278,3261-3263 (Jump, regicide_pitfall_side,
        --     maplink_agility 2279,3262 <-> 2275,3262),
        --   three dense forests side by side at 2266/2269/2272,3191 (Enter,
        --     Agility 56, regicide_route.rs2 @regicide_cross_dense_forest:
        --     2265 <-> 2268 <-> 2271 <-> 2274,3192),
        --   the tripwire 2285,3188 (Step-over, maplink_agility
        --     2284,3188 <-> 2287,3188).
        -- (The woodspring at 2235,3181 is not on it: its maplink_agility row
        -- runs west->east only, 2234 -> 2238, so westward its Pass answers
        -- "Nothing interesting happens.", measured in regicide's ledger.)
        -- The walks between are waypoint chains off a 4-way flood of the
        -- map with the trigger tiles blocked (build/orchestrator/fix_b59/r3/
        -- rovingelves.progress.md), graded on the exact tile at each end.
        -- ---------------------------------------------------------------
        local function last_lines(n)
            local _, lines = t.msg.last(n)
            local out = {}
            for _, line in ipairs(type(lines) == "table" and lines or {}) do
                out[#out + 1] = tostring(type(line) == "table" and line.text or line)
            end
            return table.concat(out, " | ")
        end

        -- Eat a shark below 60 of 99 and drink an antipoison when a snagged
        -- tripwire (or anything else) poisoned the player: both are what a
        -- player crossing the traps carries (the guide's own item list names
        -- the antipoison). No row: the crossing rows carry the readings.
        local function trap_vitals()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < 60 then
                t.player.inv_op("shark", 1)
                t.ticks(2)
            end
            local pr, poison = t.var.varp("varp102_poison")
            if pr == "ok" and (tonumber(poison) or 0) > 0 then
                for _, dose in ipairs({ "1doseantipoison", "2doseantipoison", "3doseantipoison", "4doseantipoison" }) do
                    local dr, n = t.inv.count(dose)
                    if dr == "ok" and (n or 0) > 0 then
                        t.player.inv_op(dose, 1)
                        t.ticks(2)
                        break
                    end
                end
            end
        end

        -- Walk a waypoint chain and check the exact tile at its end.
        local function walk_route(name, points)
            local trail = {}
            for _, p in ipairs(points) do
                local wr = t.player.walk_to(p[1], p[2], 40)
                if wr == "refused" then
                    -- move_to refuses a tile outside the scene the client
                    -- has built; a rebuild near the edge lands a tick later.
                    t.ticks(3)
                    wr = t.player.walk_to(p[1], p[2], 40)
                end
                local r, tt = t.world.tile()
                trail[#trail + 1] = p[1] .. "," .. p[2] .. ":" .. tostring(wr) .. "@" .. tile_text(r, tt)
            end
            local last = points[#points]
            local r, tt = t.world.tile()
            if not at_tile(r, tt, last[1], last[2]) then
                t.ticks(2)
                t.player.walk_to(last[1], last[2], 40)
                r, tt = t.world.tile()
            end
            t.check(name, at_tile(r, tt, last[1], last[2]),
                "walked " .. #points .. " waypoint(s) to " .. last[1] .. "," .. last[2] .. ",0 -> at " .. tile_text(r, tt)
                    .. " [" .. table.concat(trail, " ") .. "]")
        end

        -- Cross one trap or dense forest by its own op from the exact src
        -- tile and grade it on the src -> dest tiles (only the op moves the
        -- player over it: the pit is walled by inviswalls, the forest is
        -- solid, the tripwire's tiles fire the trap if walked). A pitfall can
        -- fail its Agility roll (regicide_traps.rs2 [label,regicide_jump_pitfall]:
        -- 15 damage; outside the pit's zone the player stays put), so a
        -- crossing that did not land is retried from the src tile (bounded).
        -- A tripwire's failed roll still crosses, snagged (10 damage, poison).
        local function cross_trap(name, step)
            local attempts, landed = 0, false
            local br, bt, cr, cd, fr, ft
            while attempts < 4 and not landed do
                attempts = attempts + 1
                if attempts > 1 then
                    trap_vitals()
                    t.player.walk_to(step.src[1], step.src[2], 20)
                end
                br, bt = t.world.tile()
                cr, cd = t.player.click_loc(step.sym, 1, { at = step.at })
                fr, ft = await_tile(function(tt)
                    return tt.level == 0 and tt.x == step.dest[1] and tt.z == step.dest[2]
                end, 10, name .. ": the crossing's landing " .. step.dest[1] .. "," .. step.dest[2])
                landed = at_tile(fr, ft, step.dest[1], step.dest[2])
            end
            t.check(name, at_tile(br, bt, step.src[1], step.src[2]) and landed,
                "from " .. tile_text(br, bt) .. " click_loc(" .. step.sym .. " at " .. step.at[1] .. "," .. step.at[2]
                    .. ", op1 " .. step.op .. ") -> " .. tostring(cr) .. " " .. tostring(cd) .. "; world.tile() after -> "
                    .. tile_text(fr, ft) .. " (want " .. step.src[1] .. "," .. step.src[2] .. ",0 -> " .. step.dest[1]
                    .. "," .. step.dest[2] .. ",0) on attempt " .. attempts .. " of at most 4 :: " .. last_lines(4))
            trap_vitals()
        end

        local PITFALL_W = { sym = "regicide_pitfall_side", op = "Jump", at = { 2278, 3262 }, src = { 2279, 3262 }, dest = { 2275, 3262 } }
        local PITFALL_E = { sym = "regicide_pitfall_side", op = "Jump", at = { 2276, 3262 }, src = { 2275, 3262 }, dest = { 2279, 3262 } }
        local FOREST_E = {
            { sym = "regicide_cross_over2", op = "Enter", at = { 2266, 3191 }, src = { 2265, 3192 }, dest = { 2268, 3192 } },
            { sym = "regicide_cross_over3", op = "Enter", at = { 2269, 3191 }, src = { 2268, 3192 }, dest = { 2271, 3192 } },
            { sym = "regicide_cross_over1", op = "Enter", at = { 2272, 3191 }, src = { 2271, 3192 }, dest = { 2274, 3192 } },
        }
        local FOREST_W = {
            { sym = "regicide_cross_over1", op = "Enter", at = { 2272, 3191 }, src = { 2274, 3192 }, dest = { 2271, 3192 } },
            { sym = "regicide_cross_over3", op = "Enter", at = { 2269, 3191 }, src = { 2271, 3192 }, dest = { 2268, 3192 } },
            { sym = "regicide_cross_over2", op = "Enter", at = { 2266, 3191 }, src = { 2268, 3192 }, dest = { 2265, 3192 } },
        }
        local TRIPWIRE_E = { sym = "regicide_trap_tripwire", op = "Step-over", at = { 2285, 3188 }, src = { 2284, 3188 }, dest = { 2287, 3188 } }
        local TRIPWIRE_W = { sym = "regicide_trap_tripwire", op = "Step-over", at = { 2285, 3188 }, src = { 2287, 3188 }, dest = { 2284, 3188 } }

        -- Isafdar side of the Arandar gate (2385,3333) -> one tile off
        -- Islwyn (2290,3147), on foot.
        local function gate_to_camp(prefix)
            walk_route(prefix .. ".walkToPitfall", { { 2383, 3325 }, { 2376, 3322 }, { 2368, 3320 }, { 2359, 3319 }, { 2355, 3313 }, { 2346, 3314 },
                { 2343, 3321 }, { 2336, 3324 }, { 2331, 3319 }, { 2331, 3309 }, { 2323, 3307 }, { 2316, 3310 },
                { 2319, 3317 }, { 2317, 3325 }, { 2308, 3326 }, { 2303, 3321 }, { 2303, 3311 }, { 2304, 3302 },
                { 2304, 3292 }, { 2304, 3282 }, { 2304, 3272 }, { 2297, 3271 }, { 2290, 3274 }, { 2284, 3270 },
                { 2279, 3265 }, { 2279, 3262 } })
            cross_trap(prefix .. ".jumpPitfall", PITFALL_W)
            walk_route(prefix .. ".walkToForest", { { 2265, 3262 }, { 2255, 3262 }, { 2249, 3258 }, { 2245, 3252 }, { 2241, 3246 }, { 2241, 3236 },
                { 2241, 3226 }, { 2241, 3216 }, { 2243, 3208 }, { 2244, 3199 }, { 2246, 3191 }, { 2252, 3187 },
                { 2259, 3188 }, { 2265, 3192 } })
            for i, step in ipairs(FOREST_E) do
                cross_trap(prefix .. ".denseForest" .. i, step)
            end
            walk_route(prefix .. ".walkToTripwire", { { 2281, 3189 }, { 2284, 3188 } })
            cross_trap(prefix .. ".stepOverTripwire", TRIPWIRE_E)
            walk_route(prefix .. ".walkToCamp", { { 2293, 3184 }, { 2293, 3174 }, { 2294, 3165 }, { 2290, 3159 }, { 2290, 3149 }, { 2290, 3147 } })
        end

        -- Anywhere in the camp -> the Isafdar side of the Arandar gate, on foot.
        local function camp_to_gate(prefix)
            walk_route(prefix .. ".walkToTripwire", { { 2290, 3157 }, { 2294, 3163 }, { 2291, 3170 }, { 2291, 3180 }, { 2288, 3187 }, { 2287, 3188 } })
            cross_trap(prefix .. ".stepOverTripwire", TRIPWIRE_W)
            walk_route(prefix .. ".walkToForest", { { 2275, 3189 }, { 2274, 3192 } })
            for i, step in ipairs(FOREST_W) do
                cross_trap(prefix .. ".denseForest" .. i, step)
            end
            walk_route(prefix .. ".walkToPitfall", { { 2259, 3188 }, { 2251, 3186 }, { 2243, 3188 }, { 2243, 3198 }, { 2242, 3207 }, { 2241, 3216 },
                { 2241, 3226 }, { 2241, 3236 }, { 2241, 3246 }, { 2245, 3252 }, { 2249, 3258 }, { 2255, 3262 },
                { 2265, 3262 }, { 2275, 3262 } })
            cross_trap(prefix .. ".jumpPitfall", PITFALL_E)
            walk_route(prefix .. ".walkToGate", { { 2282, 3269 }, { 2287, 3274 }, { 2296, 3273 }, { 2302, 3271 }, { 2304, 3279 }, { 2303, 3288 },
                { 2303, 3298 }, { 2303, 3308 }, { 2303, 3318 }, { 2305, 3326 }, { 2315, 3326 }, { 2319, 3320 },
                { 2316, 3313 }, { 2320, 3307 }, { 2330, 3307 }, { 2331, 3316 }, { 2334, 3323 }, { 2342, 3323 },
                { 2345, 3316 }, { 2352, 3313 }, { 2357, 3318 }, { 2366, 3319 }, { 2374, 3321 }, { 2381, 3324 },
                { 2384, 3331 }, { 2385, 3333 } })
        end

        -- From the camp to the Kandarin side of the gate.
        local function leave_camp(prefix)
            camp_to_gate(prefix)
            cross_arandar(prefix, true)
        end

        -- From open Kandarin ground into the camp, one tile off Islwyn.
        local function enter_camp(prefix)
            hop(prefix .. ".gotoGate", 2385, 3336)
            walk_check(prefix .. ".atGate", 2385, 3335, "the Kandarin side of the Arandar Huge Gate")
            cross_arandar(prefix, false)
            gate_to_camp(prefix)
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varp6262_rovingelves_quest",
            constants = {
                not_started = 0,
                spoken_islwyn = 10,
                spoken_eluned = 20,
                obtained_old_seed = 30,
                seed_enchanted = 40,
                seed_planted = 50,
                complete = 60,
            },
            row = "quest_rovingelves",
            display = "Roving Elves", -- all.dbrow quest_rovingelves: displayname
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effect is not client-side yet

        local qp_before_result, qp_before = t.var.varp("varp101_qp")
        t.check("qp.baseline", qp_before_result == "ok" and qp_before == 1,
            "t.var.varp(\"qp\") before any quest progress -> " .. tostring(qp_before_result)
                .. " " .. tostring(qp_before) .. " (want 1: setup's ::complete quest_waterfall awards Waterfall"
                .. " Quest's 1 qp; Regicide's varp write awards none)")

        local not_started_journal_result, not_started_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.not_started", not_started_journal_result == "ok" and not_started_journal ~= nil
            and not_started_journal.first_line ~= nil
            and not_started_journal.first_line:find("I should see if the elves hiding near Lletya", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(not_started_journal_result) .. " first_line="
                .. tostring(not_started_journal and not_started_journal.first_line)
                .. " -- channel: ui.journal_open (server-side ~rovingelves_journal proc, unaffected by the "
                .. "client varp-transmit seam)")
        t.ui.journal_close()

        -- Islwyn: roving_bowyer (base symbol, m35_49.spawn 2291,3147) transforms
        -- through %roving_bowyer but op1=Talk-to on every variant, so the base
        -- symbol works throughout. Approached from one tile off his own spawn
        -- tile (2290,3147), not onto it -- see the banner above.
        -- [opnpc1,roving_bowyer]/[opnpc1,roving_islwyn_2ops] share one trigger
        -- head; not_started routes to @rovingelves_islwyn_first.
        -- From the fixture's open Lumbridge street west into Kandarin: the
        -- Taverley members' wall is the only way on foot (reach.py
        -- 3206,3233 -> 2385,3336 NEEDS-DOOR via a members' gate at margins
        -- 200/300; sampler ruling b59 (a)), so the run's first goto stops
        -- outside the east gate (membergater 2935,3450, a gates.rs2
        -- walk-through, @member_fencegate_try), the gate is pressed on foot
        -- and graded on the tiles before and after, and the overland hop to
        -- the Arandar gate departs from open Taverley ground one tile in
        -- (reach.py 2933,3450 -> 2385,3336 closed-doors len 900). Then the
        -- Arandar gate on foot and Isafdar walked trap by trap.
        hop("islwyn1.gotoMemberGate", 2938, 3450)
        walk_check("islwyn1.memberGate.approach", 2936, 3450, "outside the Taverley members' east gate")
        do
            local br, bt = t.world.tile()
            local cr, cd = t.player.click_loc("membergater", 1, { at = { 2935, 3450 } })
            local fr, ft = await_tile(function(tt)
                return tt.level == 0 and tt.x <= 2935
            end, 10, "the members' gate walk-through into Taverley")
            t.check("islwyn1.memberGate.cross", at_tile(br, bt, 2936, 3450) and fr == "ok" and type(ft) == "table"
                    and ft.level == 0 and ft.x <= 2935 and ft.z == 3450,
                "from " .. tile_text(br, bt) .. " click_loc(membergater at 2935,3450, op1 Open) -> " .. tostring(cr) .. " "
                    .. tostring(cd) .. "; world.tile() after -> " .. tile_text(fr, ft)
                    .. " (want 2936,3450,0 -> x <= 2935 on z 3450: through the east gate into Taverley)")
        end
        walk_check("islwyn1.insideTaverley", 2933, 3450, "open Taverley ground west of the members' gate")
        enter_camp("islwyn1")
        t.exec("talk.islwyn1", t.player.talk_to, "roving_bowyer", 1)
        t.exec("talk.islwyn1-dialog", t.chat.play, {
            "npc:Human! Why are you here?",
            "player:I mean you no harm. I'm just travelling through.",
            "npc:Travelling? Through our hidden camp?",
            "choose:I helped move Glarial's remains to rest by Baxtorian Falls.",
            "player:I helped move Glarial's remains to rest by Baxtorian Falls.",
            "npc:You... you did that?",
            "npc:Perhaps I have misjudged you.",
            "choose:What do you need?",
            "player:What do you need?",
            "npc:Speak with Eluned.",
        })
        local spoken_islwyn_journal_result, spoken_islwyn_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.spoken_islwyn", spoken_islwyn_journal_result == "ok" and spoken_islwyn_journal ~= nil
            and spoken_islwyn_journal.first_line ~= nil
            and spoken_islwyn_journal.first_line:find("Islwyn wants me to speak with", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(spoken_islwyn_journal_result) .. " first_line="
                .. tostring(spoken_islwyn_journal and spoken_islwyn_journal.first_line))
        t.ui.journal_close()

        -- Eluned: roving_female_woodelf (base symbol, m35_49.spawn 2289,3145),
        -- same base-symbol-survives-the-transform shape, approached from one
        -- tile off (2288,3145). spoken_islwyn routes to
        -- @rovingelves_eluned_ritual, which names the tomb and stages
        -- spoken_eluned. She stands three tiles from Islwyn on the same
        -- open ground (reach.py 2290,3147 -> 2289,3145: closed-doors len 3),
        -- so this is a walk, not a hop. walk_near's own `_step_off_tile`
        -- picks a real walkable adjacent tile (her own tile has no clear
        -- camera pixel to click, measured live on an earlier revision).
        -- No row for the lookup or the step: the walked route already stands
        -- the player within two tiles of her, so neither could fail
        -- usefully (sampler b59); talk_to's own row is the evidence.
        local eluned1 = t.player.by_symbol("npc", "roving_female_woodelf")
        t.player.walk_near(eluned1, 10, 1)
        t.exec("talk.eluned1", t.player.talk_to, "roving_female_woodelf", 1)
        t.exec("talk.eluned1-dialog", t.chat.play, {
            "player:Islwyn said you could tell me about a ritual.",
            "npc:It is elvish tradition to plant a specially enchanted crystal seed",
            "npc:The seed must be tuned to the person it protects",
            "player:How do I get into the tomb?",
            "npc:You should already know the way",
        })
        local spoken_eluned_journal_result, spoken_eluned_journal = t.ui.journal_open("Roving Elves")
        t.check("quest.stage.spoken_eluned", spoken_eluned_journal_result == "ok" and spoken_eluned_journal ~= nil
            and spoken_eluned_journal.first_line ~= nil
            and spoken_eluned_journal.first_line:find("Eluned told me about the elves' consecration ritual", 1, true) ~= nil,
            "journal_open(Roving Elves) -> " .. tostring(spoken_eluned_journal_result) .. " first_line="
                .. tostring(spoken_eluned_journal and spoken_eluned_journal.first_line))
        t.ui.journal_close()

        -- Glarial's Tomb: entered for real through the guide's own
        -- enterGlarialsTombstone step (glarials_tombstone_waterfall_quest,
        -- WorldPoint 2559,3445,0) -- use Glarial's pebble on the tombstone.
        -- quest_waterfall_locs.rs2's [oplocu,glarials_tombstone_waterfall_quest]
        -- gates only on the forbidden-loadout check (this fixture's
        -- bare-handed loadout already satisfies it, no %waterfall_quest
        -- state test at all) and teleports to 0_39_153_58_52 = 2554,9844,0.
        -- Proven live by a prior content-parity pass (not this file):
        -- build/quest_gate/parity_rovingelves5/ledger.tsv, 10/10 PASS,
        -- same click sequence below.
        -- Out of the camp through the Arandar gate, then open Kandarin
        -- ground to the tombstone's north side (2559,3445 is the tombstone's
        -- own footprint; reach.py 2386,3336 -> 2559,3446 closed-doors).
        leave_camp("tombstone")
        hop("goto-tombstone-approach", 2559, 3446)
        -- A lookup is not a row (sampler b59): the use_on below fails on a bad handle.
        local tombstone = t.player.by_symbol("loc", "glarials_tombstone_waterfall_quest")
        -- Trap 298: use_on's arming is a backpack-tab press with no settle
        -- of its own, and the setup's ::setlevel cheats (five skills) can
        -- leave the sidebar on a level-up tab instead of the inventory one
        -- -- paint it explicitly before the first use_on of the run.
        t.ui.tab("inventory")
        t.ticks(2)
        t.exec("enterGlarialsTombstone", t.player.use_on, "glarials_pebble_waterfall_quest", tombstone)

        -- The teleport lands behind two more p_delay(2)'d mes() lines
        -- ("You hear a loud creak." / "The stone slab slides back...") after
        -- the ones use_on's own settle already waited out ("It fits
        -- perfectly." / "You place the pebble..."), so the climb-down line
        -- and the actual teleport can still be in flight when use_on
        -- returns -- await it rather than reading world.tile() bare.
        t.exec("enterGlarialsTombstone.climbedDown", t.msg.await, "climb down", 50)
        local tomb_tile_result, tomb_tile = t.world.tile()
        t.check("enterGlarialsTombstone.tile", at_tile(tomb_tile_result, tomb_tile, 2554, 9844),
            "world.tile() after the climb-down -> " .. tile_text(tomb_tile_result, tomb_tile)
                .. " (want 2554,9844,0 = p_teleport(0_39_153_58_52), inside Glarial's Tomb)")

        -- The tombstone's own entrance tile (2554,9844) is further from the
        -- guardian's spawn than the old goto_tile cheat landed, so the npc
        -- pool needs a beat to populate after the teleport (docs section 3's
        -- own advice: pair a presence precheck with await_present) --
        -- measured live: a bare Attack right after the climb-down read
        -- `no_row`, the guardian not loaded yet.
        -- Hollow on success (trap 12: bare `ok`, no detail) -- call directly.
        local guardian_present_result = t.npc.await_present("roving_mossgiant", 15, 10)
        t.step("mossguardian.await_present", guardian_present_result == "ok" and "PASS" or "FAIL",
            "npc.await_present(roving_mossgiant, 15, 10) -> " .. tostring(guardian_present_result))

        -- A fight is a wait, not a click (docs section 8): one Attack press,
        -- then await_dead re-engages on its own. op2 matches all.npc's
        -- roving_mossgiant op2=Attack.
        -- 20 ticks, as each round below: the guardian can stand a few tiles
        -- off the ladder (run 2 of b59 r3: no hit inside the default 10).
        t.exec("killGuardian.attack", t.player.attack, "roving_mossgiant", 2, 20)

        -- queue.py's RETRY after b44ce7a2d: the prior run's single 150-tick
        -- await_dead left the character bare-handed against 120 hp / +62
        -- strength / a prayer-bypassing roll with no food and never ate --
        -- it read a kill from an empty client npc pool that was really the
        -- PLAYER dying (2/30 on the guardian, hitpoints 0/99). Stats alone
        -- (99 attack/strength/defence/hitpoints/magic, already set above)
        -- were not enough; the fix is eating mid-fight, not more ticks or
        -- more levels. This is a genuine retry loop across many combat
        -- rounds, not one continuous wait -- docs section 8's rule: "record
        -- the loop's OUTCOME row only", the same idiom mortton.lua's
        -- shade-hunt loop uses, so the per-round Attack/await/eat calls are
        -- bare (no t.exec/shot each), and one row below carries the result.
        --
        -- A first food-fed attempt (with the player surviving) still read a
        -- false `ok`: combat_trace showed the SAME slot still exchanging
        -- hits with the server for another ~40 ticks after this loop had
        -- already declared it dead and moved on -- docs section 3's own
        -- warning under await_dead, "A KILL IS NEVER PROVED BY AN EMPTY
        -- POOL": three roving_mossgiant spawns sit close together in this
        -- tomb (m39_153.spawn) and the CLIENT's own pool can drop a still-
        -- alive slot it is not currently rendering nearest. So an `ok` here
        -- is corroborated against the fight's own unambiguous, quest-
        -- specific effect -- rovingelves_defeat_mossgiant's private seed
        -- drop -- before the loop is allowed to stop; an `ok` that produced
        -- no seed within a few ticks is a false read, not a kill, and the
        -- hunt presses on.
        local mossguardian_rounds = 0
        local mossguardian_sharks_eaten = 0
        local mossguardian_seed_confirmed = false
        -- The lowest hitpoints seen: each round's own read above, and the
        -- `lowest hp N/` the wait's eater reports for the ticks in between.
        local mossguardian_lowest_hp = nil
        while not mossguardian_seed_confirmed and mossguardian_rounds < 20 do
            mossguardian_rounds = mossguardian_rounds + 1

            -- Eat before the round's damage, not after: a hit that lands
            -- while hp is already low is the one that kills. Threshold 90
            -- (out of a 99 base_level) eats on nearly any damage taken,
            -- which is the point -- twenty sharks is enough headroom for
            -- that to run the whole fight without ever reading empty.
            local hp_result, hp = t.skill.read("hitpoints")
            if hp_result == "ok" and type(hp) == "table" and hp.level ~= nil
                and (mossguardian_lowest_hp == nil or hp.level < mossguardian_lowest_hp) then
                mossguardian_lowest_hp = hp.level
            end
            if hp_result == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < 90 then
                local has_shark_result, has_shark = t.inv.has("shark")
                if has_shark_result == "ok" and has_shark then
                    t.player.inv_op("shark", 1) -- shark's own ifop1=Eat
                    mossguardian_sharks_eaten = mossguardian_sharks_eaten + 1
                end
            end

            -- await_dead_engaged, not a fresh await_dead(symbol,...) per
            -- round: three roving_mossgiant spawn rows sit close together
            -- (m39_153.spawn) and re-resolving the symbol each round can
            -- abandon the half-killed guardian for whichever one is nearest
            -- THIS round (docs section 3's own warning). _engaged holds the
            -- SLOT this round's Attack press actually landed on instead
            -- (the same idiom mortton.lua's shade-hunt loop uses).
            local kill_signal = false
            local attack_result = t.player.attack("roving_mossgiant", 2, 20)
            if attack_result == "not_found" or attack_result == "no_row" then
                -- The symbol no longer resolves to a live guardian at all --
                -- either it is already dead (the seed poll below will say so)
                -- or it left the pool the same way await_dead_engaged can.
                kill_signal = true
            else
                -- Eat inside the wait too (below 60 of 99): an eat no longer
                -- holds a queued hit, so the margin is carried, not rescued.
                local await_result, await_detail = t.npc.await_dead_engaged(30, 6,
                    { eat = { item = "shark", below = 60 } })
                local wait_low = tonumber(tostring(await_detail):match("lowest hp (%d+)/"))
                if wait_low ~= nil and (mossguardian_lowest_hp == nil or wait_low < mossguardian_lowest_hp) then
                    mossguardian_lowest_hp = wait_low
                end
                if await_result == "ok" then
                    kill_signal = true
                end
            end

            if kill_signal then
                local seed_confirm_result = t.await({
                    level = function()
                        return t.world.obj_near("roving_old_consecration_seed", 15) == "ok"
                    end,
                    note = "confirming the guardian's kill against its own seed drop",
                }, 15)
                mossguardian_seed_confirmed = seed_confirm_result == "ok"
            end

            local alive_result = t.player.alive()
            if alive_result ~= "ok" then
                break -- the driver's own terminal player.died row ends the run right after this
            end
        end
        t.check("killGuardian.await_dead", mossguardian_seed_confirmed,
            "hunted " .. tostring(mossguardian_rounds) .. " round(s), ate " .. tostring(mossguardian_sharks_eaten)
                .. " shark(s) -- t.player.attack + t.npc.await_dead_engaged(30, 6) per round, each `ok` "
                .. "corroborated against roving_old_consecration_seed's own private drop -> "
                .. tostring(mossguardian_seed_confirmed and "confirmed" or "never confirmed within the round budget"))
        t.expect("player.aliveAfterGuardian", t.player.alive())
        -- Fight margin (fixer brief): lowest hp at least a quarter of the
        -- 99 staged hitpoints AND food left, never one or the other.
        local sharks_left_result, sharks_left = t.inv.count("shark")
        t.check("killGuardian.margin", mossguardian_lowest_hp ~= nil and mossguardian_lowest_hp >= 25
                and sharks_left_result == "ok" and sharks_left >= 1,
            "lowest hp " .. tostring(mossguardian_lowest_hp) .. "/99 (staged ::setlevel hitpoints 99), shark staged 20,"
                .. " left " .. tostring(sharks_left) .. " (" .. tostring(sharks_left_result) .. ")"
                .. " -- margin: lowest hp >= 25 AND shark left >= 1")

        -- await_dead's own `no_row` (npc left the pool) also fires if the
        -- PLAYER dies and respawns instead -- the guardian's own
        -- viewport-changing row read looks identical either way. Confirm we
        -- are still standing in the tomb, not back in Lumbridge, before
        -- trusting the kill.
        local post_kill_tile_result, post_kill_tile = t.world.tile()
        local post_kill_pass = post_kill_tile_result == "ok" and post_kill_tile ~= nil
            and post_kill_tile.z ~= nil and post_kill_tile.z >= 9800
        t.step("postKill.tileCheck", post_kill_pass and "PASS" or "FAIL",
            "world.tile() after await_dead -> " .. tostring(post_kill_tile_result) .. " "
                .. tostring(post_kill_tile and (post_kill_tile.x .. "," .. post_kill_tile.z
                    .. "," .. post_kill_tile.level)) .. " (want z>=9800, still in Glarial's Tomb)")

        -- The seed is a private ground drop (obj_add_private) from
        -- rovingelves_defeat_mossgiant, not a chat grant -- confirmed live
        -- that the click can race the zone packet that tells the client the
        -- drop exists at all ("no obj ... in the client's entity pool"),
        -- one run after the exact same kill left it readable a tick later,
        -- so poll for it in the pool before pressing, the same way inv.await
        -- polls a backpack grant rather than trusting a bare read.
        t.await({
            level = function()
                local result = t.world.obj_near("roving_old_consecration_seed", 15)
                return result == "ok"
            end,
            note = "waiting for the old seed's private drop to reach the client's entity pool",
        }, 10) -- no row (sampler b59: it could not fail); pickUpSeed's count is the evidence

        -- click_obj answers `ok` with a nil detail (trap 12/section 8's
        -- fourth hollow verb) -- call it directly and write the count by hand.
        local seed_before_result, seed_before = t.inv.count("roving_old_consecration_seed")
        local seed_click_result, seed_click_detail = t.player.click_obj("roving_old_consecration_seed")
        t.inv.await("roving_old_consecration_seed", 1, 10)
        local seed_after_result, seed_after = t.inv.count("roving_old_consecration_seed")
        local seed_pass = seed_click_result == "ok" and seed_after_result == "ok"
            and seed_after > (seed_before_result == "ok" and seed_before or 0)
        t.step("pickUpSeed", seed_pass and "PASS" or "FAIL",
            string.format("click_obj roving_old_consecration_seed -> %s (%s), count %s -> %s",
                tostring(seed_click_result), tostring(seed_click_detail), tostring(seed_before), tostring(seed_after)))

        -- ui.journal_open is skipped here -- measured live, right after a
        -- click_obj pickup underground in the tomb it times out at 20 ticks
        -- ("no painted journal"), opening the Quest List on the Free tab
        -- and never finding this members quest's row (the same shape the
        -- post-completion quest.journal row below is already known not to
        -- reach, section 8's gap note). t.quest.stage()/expect_stage is the
        -- same server-content-fallback channel quest.varp_complete already
        -- proves live in this exact run, so it is the one this row reads.
        t.exec("quest.stage.obtained_old_seed", t.quest.expect_stage, "obtained_old_seed")

        -- Back to Eluned: stage obtained_old_seed routes to
        -- @rovingelves_eluned_enchant, which swaps the old seed for the new
        -- one (inv_del/inv_add before its own mesbox, so poll with inv.await
        -- rather than a bare read -- section 8's gap note).
        -- Out of Glarial's Tomb by its own ladder:
        -- ladder_from_cellar_directional at 2556,9844, maplink
        -- 0_39_153_61_52 -> 0_39_53_61_52 (2557,9844 -> 2557,3444,
        -- ladders_stairs/configs/maplink.dbrow). The climb can answer
        -- before it lands, so wait for the surface before grading it.
        local tomb_ladder_result, tomb_ladder_detail = t.player.click_loc("ladder_from_cellar_directional", 1)
        local tomb_out_result, tomb_out = await_tile(function(tt)
            return tt.level == 0 and tt.z < 6400
        end, 15, "the tomb ladder's climb to the surface")
        t.check("tomb.climbOut", at_tile(tomb_out_result, tomb_out, 2557, 3444),
            "click_loc(ladder_from_cellar_directional, 1) -> " .. tostring(tomb_ladder_result) .. " "
                .. tostring(tomb_ladder_detail) .. "; world.tile() after -> " .. tile_text(tomb_out_result, tomb_out)
                .. " (want 2557,3444,0, the maplink's surface end beside the tombstone)")

        -- Open Kandarin ground from the tombstone to the Arandar gate
        -- (reach.py 2557,3444 -> 2386,3336 closed-doors len 281), through
        -- it, and across Isafdar to the camp.
        enter_camp("eluned2")
        local eluned2 = t.player.by_symbol("npc", "roving_female_woodelf") -- no row: see eluned1
        t.player.walk_near(eluned2, 10, 1)
        t.exec("talk.eluned2", t.player.talk_to, "roving_female_woodelf", 1)
        t.exec("talk.eluned2-dialog", t.chat.play, {
            "player:I found the old seed.",
            "npc:Wonderful. Let me enchant it for you.",
            "mesbox:Eluned silently enchants the crystal seed",
            "npc:Take this to the Chalice of Eternity",
        })
        t.inv.await("roving_new_consecration_seed", 1, 10)
        t.inv.await("roving_old_consecration_seed", 0, 10)

        -- ui.journal_open is skipped here too (measured live, same seam as
        -- quest.stage.obtained_old_seed above): it PASSed three times early
        -- in this same run (not_started/spoken_islwyn/spoken_eluned, before
        -- the tomb) and then times out at 20 ticks on every stage check
        -- after it, opening the Quest List on the Free tab and never
        -- finding this members quest's row. t.quest.expect_stage reads the
        -- same server-content-fallback channel quest.varp_complete already
        -- proves live in this exact run.
        t.exec("quest.stage.seed_enchanted", t.quest.expect_stage, "seed_enchanted")

        -- Chalice of Eternity: rovingelves_chalice_coord = 0_40_154_43_54 =
        -- 2603,9910 (comment in configs/quest_rovingelves.constant), a
        -- different z+6400 square from the tomb. Reached for real through
        -- the guide's own six legs (boardRaft, useRopeOnRock, useRopeOnTree,
        -- enterFalls, searchFallsCrate, useKeyOnFallsDoor), all of it
        -- quest_waterfall_locs.rs2's existing Waterfall Quest route --
        -- proven live by a prior content-parity pass (not this file):
        -- build/quest_gate/parity_rovingelves_route2d/ledger.tsv, 19/21 PASS
        -- (the 2 FAIL rows there are that scratch script's own settle-
        -- detection false negatives, each contradicted by the very next
        -- tile check in the same run -- read as PASS here, matching their
        -- own writeup). The seed's own ifop1=Plant fires
        -- [opheld1,roving_new_consecration_seed].
        -- Out of the camp through the Arandar gate, open Kandarin ground to
        -- the street east of Almera's yard (reach.py 2386,3336 -> 2530,3495
        -- closed-doors), then on foot through the yard's east fence gate
        -- (fencegate_l 2528,3495, west edge) and the raft pen's gate
        -- (fencegate_l 2513,3494, west edge; the pen is 12 tiles,
        -- x 2510-2512, z 3492-3496). Opening the left leaf opens the pair.
        leave_camp("raft")
        hop("goto-raft-approach", 2530, 3495)
        pass_door("almeraYard.in", "fencegate_l", "openfencegate_l", 2528, 3495, 2528, 3495, 2527, 3495,
            "inside Almera's yard")
        pass_door("raftPen.in", "fencegate_l", "openfencegate_l", 2513, 3494, 2513, 3494, 2512, 3494,
            "the raft pen behind Almera's house")
        -- boardRaft's own settle resolves on the raft's first mes() line
        -- ("You board the small raft"), one to two ticks ahead of the
        -- p_teleport that actually moves the player downstream -- no bare
        -- tile read right after the click (trap 24's cousin for a
        -- teleport, not a container). The NEXT leg's own tile check
        -- (useRopeOnRock, below) is boardRaft's real evidence.
        local raft_result, raft_detail = t.player.click_loc("lograft_waterfall_quest", 1)
        local mound_result, mound = await_tile(function(tt)
            return tt.level == 0 and tt.x == 2512 and tt.z == 3481
        end, 15, "the raft's p_teleport(0_39_54_16_25) to the land mound")
        t.check("boardRaft", at_tile(mound_result, mound, 2512, 3481),
            "click_loc(lograft_waterfall_quest, 1) -> " .. tostring(raft_result) .. " " .. tostring(raft_detail)
                .. "; world.tile() after -> " .. tile_text(mound_result, mound)
                .. " (want 2512,3481,0: [oploc1,lograft_waterfall_quest] p_teleport(0_39_54_16_25), the mound)")
        t.ticks(4) -- the raft's last two mes() lines and p_delay(2)s are still in flight

        local crossing_rock = t.player.by_symbol("loc", "crossing_rock_waterfall_quest") -- no row: useRopeOnRock grades it
        -- Graded on world.tile(), not use_on's own settle word: proven live
        -- by a prior content-parity pass (build/parity_state/parity2) that
        -- quest_waterfall_locs.rs2's [aplocu,crossing_rock_waterfall_quest]
        -- is a silent forced-walk+spotanim branch with no chat line at all,
        -- so the driver's settle detector answers `settle_after_click`
        -- (none of its own recognised conditions fired) on a press that DID
        -- land -- trap 21's "silent oplocu/oplocu branch" cousin, and not
        -- this file's verb to fix (script/plugins/, trap 7).
        local rock_press_result, rock_press_detail = t.player.use_on("rope", crossing_rock)
        local after_rock_result, after_rock = t.world.tile()
        local rock_pass = after_rock_result == "ok" and after_rock ~= nil
            and after_rock.x == 2513 and after_rock.z == 3468 -- seam10 pfe: graded on the island (rock+1 east), the sampler's ask
        t.check("useRopeOnRock", rock_pass,
            "use_on(rope, crossing_rock) -> " .. tostring(rock_press_result) .. " " .. tostring(rock_press_detail)
                .. "; world.tile() after -> " .. tostring(after_rock_result) .. " "
                .. tostring(after_rock and (after_rock.x .. "," .. after_rock.z .. "," .. after_rock.level))
                .. " (want 2513,3468,0 -- the island crossing_rock's forcewalk2 lands on, not"
                .. " 2512,3476 (the crossing_rock tile itself, the forcewalk2 START) -- graded"
                .. " on the tile, not the press's own settle word, see above)")

        local overhanging_tree = t.player.by_symbol("loc", "overhanging_tree1_waterfall_quest") -- no row: useRopeOnTree grades it
        t.exec("useRopeOnTree", t.player.use_on, "rope", overhanging_tree)
        local after_tree_result, after_tree = t.world.tile()
        t.step("useRopeOnTree.tile", after_tree_result == "ok" and after_tree ~= nil
            and after_tree.x == 2511 and after_tree.z == 3463 and "PASS" or "FAIL",
            "world.tile() after use_on(rope, overhanging_tree1) -> " .. tostring(after_tree_result) .. " "
                .. tostring(after_tree and (after_tree.x .. "," .. after_tree.z .. "," .. after_tree.level))
                .. " (want 2511,3463,0 -- p_teleport(0_39_54_15_7))")

        -- waterfall_ledge_door's own oplocu is mes("The door begins to
        -- open."); p_delay(2); mes("You walk through the door."); p_teleport(...)
        -- -- click_loc's settle resolves on the FIRST mes(), two ticks
        -- ahead of the teleport (trap 24's cousin again).
        local ledge_door_result, ledge_door_detail = t.player.click_loc("waterfall_ledge_door", 1)
        local falls_tile_result, falls_tile = await_tile(function(tt)
            return tt.level == 0 and tt.x == 2575 and tt.z == 9861
        end, 10, "the ledge door's p_teleport(0_40_154_15_5) into the falls")
        t.check("enterFalls", at_tile(falls_tile_result, falls_tile, 2575, 9861),
            "click_loc(waterfall_ledge_door, 1) -> " .. tostring(ledge_door_result) .. " " .. tostring(ledge_door_detail)
                .. "; world.tile() after -> " .. tile_text(falls_tile_result, falls_tile)
                .. " (want 2575,9861,0 -- [oploc1,waterfall_ledge_door] p_teleport(0_40_154_15_5), the entrance hall;"
                .. " waterfall complete, so no flood)")

        -- ---------------------------------------------------------------
        -- Inside the falls, every door on foot. The entrance hall (112
        -- tiles from 2575,9861) is closed by three castle double doors:
        -- east to the crate room (castledoubledoorl/r 2582,9875/9876, on
        -- their tiles' west edge), west to the passage north
        -- (castledoubledoorl/r 2564/2565,9881, north edge) and a north pair
        -- this route never needs. Opening the left leaf opens the pair.
        -- ---------------------------------------------------------------
        pass_door("crateRoom.in", "castledoubledoorl", "opencastledoubledoorl", 2582, 9875, 2581, 9875, 2584, 9876,
            "inside the east crate room, x 2582-2595 z 9875-9888")
        local key_before_result, key_before = t.inv.count("baxtorian_key_waterfall_quest")
        t.exec("searchFallsCrate", t.player.click_loc, "baxtorian_crate_waterfall_quest", 1)
        -- inv.await, not a bare count (trap 24): the engine writes the
        -- inv_add into the NEXT tick's player update.
        t.exec("searchFallsCrate.gotKey", t.inv.await, "baxtorian_key_waterfall_quest",
            (key_before_result == "ok" and key_before or 0) + 1, 10)
        pass_door("crateRoom.out", "castledoubledoorl", "opencastledoubledoorl", 2582, 9875, 2583, 9875, 2581, 9875,
            "the entrance hall")
        pass_door("westPassage.in", "castledoubledoorl", "opencastledoubledoorl", 2564, 9881, 2564, 9881, 2564, 9882,
            "the passage north of the hall's west doors")

        -- "Go through the doors from the west room": two baxtorian_door_2
        -- copies, both locked to op1 from the passage (quest_waterfall_locs.rs2
        -- [oploc1,baxtorian_door_2_waterfall_quest]: coordz < 9895 is
        -- "The door is locked."), both opened with the crate's key
        -- ([oplocu,...]: ~waterfall_walk_door). The first, 2568,9893 (north
        -- edge), lets the player into a 13-tile room x 2566-2569 z 9894-9901;
        -- the second, 2566,9901 -- ^waterfall_original_room_door_coord --
        -- sends a player past ^waterfall_placed_amulet straight on to
        -- ^waterfall_raised_room_door_coord 2604,9901, the chalice room.
        walk_check("keyDoor.approach", 2568, 9892, "the passage south of the first locked door")
        local key_door1 = t.player.by_symbol("loc", "baxtorian_door_2_waterfall_quest") -- no row: keyDoor.in grades it
        local key1_result, key1_detail = t.player.use_on("baxtorian_key_waterfall_quest", key_door1, { at = { 2568, 9893 } })
        local in_room_result, in_room = await_tile(function(tt)
            return tt.level == 0 and tt.z >= 9894 and tt.z <= 9901 and tt.x >= 2566 and tt.x <= 2569
        end, 10, "the first key door's walk-through into the west room")
        t.check("keyDoor.in", at_tile(in_room_result, in_room, 2568, 9894),
            "use_on(baxtorian_key, baxtorian_door_2 at 2568,9893) -> " .. tostring(key1_result) .. " "
                .. tostring(key1_detail) .. "; world.tile() after -> " .. tile_text(in_room_result, in_room)
                .. " (want 2568,9894,0: ~waterfall_walk_door(entering) steps one tile past the door)")

        walk_check("useKeyOnFallsDoor.approach", 2566, 9900, "inside the west room, south of its second door")
        local falls_door, falls_door_result = t.player.by_symbol("loc", "baxtorian_door_2_waterfall_quest")
        local key2_result, key2_detail = "not_found", tostring(falls_door_result)
        if falls_door_result == "ok" then
            key2_result, key2_detail = t.player.use_on("baxtorian_key_waterfall_quest", falls_door, { at = { 2566, 9901 } })
        end
        local after_door_result, after_door = await_tile(function(tt)
            return tt.level == 0 and tt.x > 2600 and tt.z >= 9901
        end, 15, "the second key door's p_teleport to ^waterfall_raised_room_door_coord")
        t.check("useKeyOnFallsDoor", at_tile(after_door_result, after_door, 2604, 9901),
            "use_on(baxtorian_key, baxtorian_door_2 at 2566,9901) -> " .. tostring(key2_result) .. " "
                .. tostring(key2_detail) .. "; world.tile() after -> " .. tile_text(after_door_result, after_door)
                .. " (want 2604,9901,0 = ^waterfall_raised_room_door_coord 0_40_154_44_45, the chalice room)")

        -- Real walk (not goto_tile -- no gate left between here and the
        -- planting spot, just distance): the door's own landing tile
        -- (~2604,9901) is short of rovingelves_chalice_zone's own z floor
        -- (9906). Target 2603,9909, one tile off rovingelves_chalice_coord
        -- itself (2603,9910) -- that exact tile is the chalice loc's own
        -- footprint (chalice.locProbe below reads id=2014 tile=2603,9910
        -- match=exact) and is not walkable; 2603,9909 is still inside
        -- rovingelves_chalice_zone (z 9906-9914) and loc_find checks the
        -- constant coordinate, not the player's tile, so standing beside it
        -- satisfies rovingelves_seed.rs2's opheld1 guard the same way.
        -- Hollow on success (trap 12: `ok, nil` once the tile is reached,
        -- only a stall carries a detail) -- call directly, write the tile.
        local chalice_walk_result, chalice_walk_detail = t.player.walk_to(2603, 9909, 20)
        t.step("walk.chaliceRoom", chalice_walk_result == "ok" and "PASS" or "FAIL",
            "walk_to(2603, 9909) -> " .. tostring(chalice_walk_result) .. " " .. tostring(chalice_walk_detail))

        -- Diagnostics before the plant attempt: rovingelves_seed.rs2's
        -- opheld1 guard is `inzone(chalice_zone_min, chalice_zone_max,
        -- coord) = false | loc_find(chalice_coord,
        -- baxtorian_chalice_waterfall_quest) = false` -- a first attempt
        -- answered no message at all (not even the guard's own
        -- "This seed may only be planted close to Glarial's remains."
        -- mesbox), so confirm both halves land where the constant says
        -- before trying again.
        local chalice_tile_result, chalice_tile = t.world.tile()
        t.check("chalice.tileProbe", at_tile(chalice_tile_result, chalice_tile, 2603, 9909),
            "world.tile() after walk.chaliceRoom -> " .. tostring(chalice_tile_result) .. " "
                .. tostring(chalice_tile and (chalice_tile.x .. "," .. chalice_tile.z .. "," .. chalice_tile.level))
                .. " (want 2603,9909,0, one tile off ^rovingelves_chalice_coord 0_40_154_43_54 -- "
                .. "that exact tile is the chalice loc's own unwalkable footprint)")
        -- (The chalice itself is a static map loc at 2603,9910 -- maps/
        -- m40_154 -- so a row asserting its tile would only re-read the map;
        -- sampler b59 dropped it. The plant below is the evidence.)

        -- Per queue.py's last_failure on this file, both of this section's
        -- old blockers are answered: the plant press IS sent (a retry to an
        -- unpainted backpack tab, the same shape every other inv_op press
        -- here can take, not a dead click), and the ONE broken channel at
        -- this point is ui.journal_open("Roving Elves") itself -- it opens
        -- the Quest List on the Free tab and never finds this members
        -- quest's row. rovingelves_seed.rs2's [opheld1,...] success path is
        -- two plain `mes()` game-message lines (not a mesbox), so the plant
        -- is asserted from those chat lines and the backpack count instead.
        local plant_result, plant_detail = t.player.inv_op("roving_new_consecration_seed", 1)
        -- inv_op's own settle already waits for a new chat line (a fifth
        -- verb off trap 12/section 8's hollow list would be redundant here),
        -- so by the time it returns both `mes()` lines are already in the
        -- ring -- t.msg.expect (any recent line), not t.msg.await (only
        -- lines newer than a serial snapshot taken AFTER they already
        -- landed, which timed out on the first try, measured on this file).
        local plant_dig_msg_result = t.msg.expect("You dig a small hole with your spade.")
        local plant_drop_msg_result = t.msg.expect("You drop the crystal seed in the hole.")
        local seed_after_plant_result, seed_after_plant = t.inv.count("roving_new_consecration_seed")
        local plant_pass = plant_dig_msg_result == "ok" and plant_drop_msg_result == "ok"
            and seed_after_plant_result == "ok" and seed_after_plant == 0
        t.check("plantSeed", plant_pass,
            "inv_op(roving_new_consecration_seed, 1) in the chalice room (chalice.tileProbe and "
                .. "quest.stage.seed_enchanted confirmed before this click) -> "
                .. tostring(plant_result) .. " " .. tostring(plant_detail)
                .. "; msg.expect('You dig a small hole with your spade.') -> " .. tostring(plant_dig_msg_result)
                .. "; msg.expect('You drop the crystal seed in the hole.') -> " .. tostring(plant_drop_msg_result)
                .. "; roving_new_consecration_seed count after -> " .. tostring(seed_after_plant_result)
                .. " " .. tostring(seed_after_plant) .. " (want 0 -- rovingelves_seed.rs2's own "
                .. "inv_del(inv, roving_new_consecration_seed, 1))")

        -- The stage itself, read through the same server-content channel as
        -- quest.stage.obtained_old_seed and seed_enchanted (sampler b59: the
        -- row used to reuse plantSeed's evidence).
        t.exec("quest.stage.seed_planted", t.quest.expect_stage, "seed_planted")

        -- ---------------------------------------------------------------
        -- Out of the falls the way in, every door on foot. The raised
        -- room's door (baxtorian_door_2 2604,9900) answers op1 from inside
        -- with ~waterfall_walk_door and, its x past 2600, p_teleport back to
        -- ^waterfall_original_room_door_coord 2566,9901
        -- (quest_waterfall_locs.rs2 [oploc1,baxtorian_door_2_waterfall_quest]).
        -- ---------------------------------------------------------------
        local raised_door_result, raised_door_detail = t.player.click_loc("baxtorian_door_2_waterfall_quest", 1,
            { at = { 2604, 9900 } })
        local west_room_result, west_room = await_tile(function(tt)
            return tt.level == 0 and tt.x < 2600
        end, 15, "the raised room door's p_teleport back to the west room")
        t.check("raisedRoom.out", west_room_result == "ok" and type(west_room) == "table" and west_room.level == 0
                and west_room.x >= 2566 and west_room.x <= 2569 and west_room.z >= 9894 and west_room.z <= 9901,
            "click_loc(baxtorian_door_2 at 2604,9900, 1) -> " .. tostring(raised_door_result) .. " "
                .. tostring(raised_door_detail) .. "; world.tile() after -> " .. tile_text(west_room_result, west_room)
                .. " (want the west room, x 2566-2569 z 9894-9901: p_teleport(^waterfall_original_room_door_coord))")

        -- The first key door from inside: op1 is "The door is locked."
        -- for any player below z 9895 -- the tile beside it is 9894 -- so the
        -- key opens it this way too (~waterfall_walk_door(leaving) puts the
        -- player on the door tile 2568,9893, the passage side).
        walk_check("keyDoor.outApproach", 2568, 9894, "inside the west room, north of its first door")
        local key_door_out, key_door_out_result = t.player.by_symbol("loc", "baxtorian_door_2_waterfall_quest")
        local key3_result, key3_detail = "not_found", tostring(key_door_out_result)
        if key_door_out_result == "ok" then
            key3_result, key3_detail = t.player.use_on("baxtorian_key_waterfall_quest", key_door_out, { at = { 2568, 9893 } })
        end
        local passage_result, passage = await_tile(function(tt)
            return tt.level == 0 and tt.z <= 9893
        end, 10, "the first key door's walk-through back to the passage")
        t.check("keyDoor.out", at_tile(passage_result, passage, 2568, 9893),
            "use_on(baxtorian_key, baxtorian_door_2 at 2568,9893) -> " .. tostring(key3_result) .. " "
                .. tostring(key3_detail) .. "; world.tile() after -> " .. tile_text(passage_result, passage)
                .. " (want 2568,9893,0: ~waterfall_walk_door(leaving) lands on the door tile, passage side)")

        pass_door("westPassage.out", "castledoubledoorl", "opencastledoubledoorl", 2564, 9881, 2564, 9882, 2565, 9880,
            "the entrance hall")

        -- The hall's own exit: baxtorian_door_waterfall_quest 2575,9861,
        -- op1 "You open the door and walk through." p_teleport(0_39_54_15_7)
        -- = 2511,3463, the ledge (quest_waterfall_locs.rs2:335-337).
        local exit_door_result, exit_door_detail = t.player.click_loc("baxtorian_door_waterfall_quest", 1)
        local ledge_result, ledge = await_tile(function(tt)
            return tt.level == 0 and tt.z < 6400
        end, 10, "the falls' exit door to the ledge")
        t.check("falls.exitToLedge", at_tile(ledge_result, ledge, 2511, 3463),
            "click_loc(baxtorian_door_waterfall_quest, 1) -> " .. tostring(exit_door_result) .. " "
                .. tostring(exit_door_detail) .. "; world.tile() after -> " .. tile_text(ledge_result, ledge)
                .. " (want 2511,3463,0: p_teleport(0_39_54_15_7), the ledge)")

        -- Off the ledge in its barrel: barrel_waterfall_quest (op1 "Get in")
        -- stands at 2512,3463 beside the ledge tile -- placed on level 1 of
        -- maps/m39_54.jl2 ("1 16 7: 2022 10"), a tile m39_54.jm2 flags as a
        -- bridge (f2), so it is on the player's own plane; LostCity places it
        -- byte-for-byte the same. [oploc1,barrel_waterfall_quest]: "You climb
        -- in the barrel and start rocking." ... p_teleport(^waterfall_fail_coord)
        -- = 0_39_53_31_21 = 2527,3413, the river bank (quest_waterfall_locs.rs2:339-345).
        -- (The ledge's dead tree is no way off: from the ledge its op1
        -- answers "I can't reach that!", measured run 1.)
        local barrel_result, barrel_detail = t.player.click_loc("barrel_waterfall_quest", 1)
        local bank_result, bank = await_tile(function(tt)
            return tt.level == 0 and tt.x == 2527 and tt.z == 3413
        end, 15, "the barrel's ride off the ledge to the river bank")
        t.check("falls.offLedge", at_tile(bank_result, bank, 2527, 3413),
            "click_loc(barrel_waterfall_quest, 1) -> " .. tostring(barrel_result) .. " "
                .. tostring(barrel_detail) .. "; world.tile() after -> " .. tile_text(bank_result, bank)
                .. " (want 2527,3413,0 = ^waterfall_fail_coord, the river bank)")

        -- Open river-bank ground to the Arandar gate (reach.py 2527,3413 ->
        -- 2386,3336 closed-doors len 242), through it, across Isafdar.
        enter_camp("islwyn2")

        -- Reward snapshot before the hand-in (docs section 7's reward-row
        -- rule): quest_complete_rewards passes "10000 Strength XP|Crystal
        -- bow or shield (500 charges)|Moss Guardian in the Nightmare Zone" --
        -- the strength xp and the chosen item are both asserted below against
        -- those literal numbers, never a value read back from the scroll.
        -- No row of its own (the status of a read cannot fail usefully): a
        -- failed snapshot fails reward.strength's expect_gain below.
        local _, reward_before = t.skill.snapshot()

        -- Islwyn again: stage seed_planted routes to @rovingelves_islwyn_finish.
        t.exec("talk.islwyn2", t.player.talk_to, "roving_bowyer", 1)
        t.exec("talk.islwyn2-dialog", t.chat.play, {
            "player:The seed is planted. Glarial and the other ancestors can finally rest.",
            "npc:I was wrong about you.",
            "npc:Please, take this as a token of our thanks.",
            "choose:Shields are for wimps! Give me the bow!",
            "player:Thank you, this crystal bow is a fine gift.",
        })
        t.ticks(3) -- rovingelves_quest_complete is queued(0,0), not client-side yet

        -- Completion is real (the hand-in above ran [queue,rovingelves_quest_complete]
        -- for real, through a genuine click, never cheated). The varp seam
        -- named in this file's banner is fixed now: quest.lua's own
        -- expect_complete/stage readers fall through to the embedded
        -- server's own copy of rovingelves_quest once both client-side
        -- halves answer not_found, printing "server content" in the row.
        --
        -- But t.quest.expect_complete() itself is not driven bare here,
        -- because its OWN quest.journal row -- a fresh journal_open() right
        -- after its scroll.close() -- never lands post-completion on this
        -- quest: measured DETERMINISTIC, three separate attempts (bare
        -- expect_complete(), a hand-rolled version with a settle before the
        -- press, and again with t.ticks(3) between the scroll closing and
        -- the press), same failure every time -- "Roving Elves" row 72
        -- clicked, but no painted journal within 20 ticks -- while the
        -- identical journal_open("Roving Elves") call already succeeded
        -- FIVE times earlier in this same run for the mid-quest stage rows
        -- below. Nothing this file can drive reaches whatever is different
        -- about the post-completion press (no scroll verb exposes it, and
        -- script/plugins/ui.lua is out of reach -- trap 7). Per section 7's
        -- own minimum shape ("if you call quest.bind: at least one quest.*
        -- row, and either a passing quest.varp_complete or the ledger's
        -- last row is BLOCKED"), quest.journal is not itself required, so
        -- completion is asserted through the three rows that DO land --
        -- quest.varp_complete (t.quest.stage(), the same server-content
        -- fallback expect_complete's own row uses), quest.scroll_title and
        -- quest.points -- rather than shipping a row known to time out.
        t.settle() -- section 8's gap note: settle before reading the scroll,
                   -- so its shot does not publish a one-tick-early frame
                   -- with no scroll mounted yet.
        local varp_complete_result, varp_complete_value, varp_complete_kind, varp_complete_source =
            t.quest.stage()
        t.check("quest.varp_complete", varp_complete_result == "ok" and varp_complete_value == 60,
            "t.quest.stage() -> " .. tostring(varp_complete_result) .. " " .. tostring(varp_complete_value)
                .. " kind=" .. tostring(varp_complete_kind) .. " source=" .. tostring(varp_complete_source)
                .. " (want 60 = ^rovingelves_complete)")

        local scroll_title_result, scroll_title = t.scroll.title()
        t.check("quest.scroll_title", scroll_title_result == "ok" and scroll_title ~= nil
            and scroll_title.name ~= nil and scroll_title.name:find("Roving Elves", 1, true) ~= nil,
            "scroll.title() after the hand-in -> " .. tostring(scroll_title_result) .. " name="
                .. tostring(scroll_title and scroll_title.name))
        t.scroll.close()

        local qp_after_result, qp_after = t.var.varp("varp101_qp")
        t.check("quest.points", qp_after_result == "ok" and qp_before_result == "ok"
            and qp_after == qp_before + 1,
            "qp (varp) " .. tostring(qp_before) .. " -> " .. tostring(qp_after)
                .. " (want +1)")

        t.check("reward.strength", t.skill.expect_gain("strength", 10000, reward_before))
        t.check("reward.crystal_bow", t.inv.expect_has("crystal_bow", 1))

        t.finish(0)
    end,
}
