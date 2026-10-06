-- Witch's House, driven for real end to end: talk to the Boy in Taverley,
-- find the door key under the pot, cross the basement shock gate wearing
-- gloves, fetch the magnet from the basement cupboard, fit it to the mouse
-- to unlock the back door, read the witch's diary (keeps the door unlocked),
-- search the garden fountain for the shed key, use it on the shed (spawns
-- the four-form shapeshifter experiment), fight all four forms for real,
-- pick up the ball and hand it back to the Boy.
--
-- Read against OSRS-Content/osrs239-content/server/scripts/quests/quest_ball/
-- (quest_ball_locs.rs2, nora_t_hagg.rs2, witches_diary.rs2) and
-- server/scripts/areas/area_taverly/scripts/boy.rs2, cross-checked against
-- Quest Helper's WitchsHouse.java (quest-helper/.../helpers/quests/
-- witchshouse/WitchsHouse.java) for every WorldPoint and op string.
--
-- Prerequisites cheated in setup (trap 16 -- gear/tools are a prerequisite,
-- never this quest's own deliverable): combat levels for the four
-- full-health shapeshifter forms (levels 19/30/42/53, witches_house.npc),
-- a rune loadout, food and cheese ("multiple if you mess up", Quest Helper's
-- own note). leather_gloves is marked canBeObtainedDuringQuest() by Quest
-- Helper ("search the nearby boxes until you get a pair"), but this content
-- pack implements NO box-search trigger for them anywhere in
-- quest_ball_locs.rs2 or the m45_54 area spawn table (grepped; the only
-- leather_gloves ground spawns in the whole pack are in unrelated map
-- squares m50_52/m26_48/m30_147/m23_55/m48_54/m49_49) -- there is nothing to
-- click here, so they are given directly, same idiom as Heroes' Quest's
-- non-canBeObtainedDuringQuest bring-alongs (hero.lua).
--
-- Every other mid-quest item (door key, magnet, diary, shed key, ball) is
-- the quest's own deliverable and is driven for real below, never ::given.
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md standing rules; owner ruling 2026-10-05:
-- the first goto obeys it too). Every closed space is entered and left by its own
-- loc, on every visit:
--   * Taverley is behind the members' wall (reach.py 3206,3233 -> 2928,3456:
--     every walk opens membergater 2935,3450). The run leaves Lumbridge by a REAL
--     Falador Teleport (teleport_cast), travels overland to the open ground east
--     of the east gate and crosses membergater by its press (cross_gate).
--   * The witch's house: the front door witchhousedoor 2900,3473, the back door
--     witchbackdoor 2901,3465 and the shed door witchsheddoor 2934,3463 are all
--     ~ball_walk_door p_teleport walk-throughs (quest_ball_locs.rs2) that leave no
--     opened loc, so each is a cross_gate (the shed's way IN is the guide's own
--     "use the key on the door", a graded use_on). The two interior doors
--     grim_witch_house_door 2902,3474 (middle room | ladder room) and 2902,3467
--     (middle room | south room) are plain doors.loc doors (open leaf
--     grim_witch_house_door_open): pass_door both ways.
--   * The basement: grim_witch_ladder_down 2907,3476 / grim_witch_ladder_up
--     2907,9876 (maplink_0_45_54_26_20_down / maplink_0_45_154_26_20_up: 2906,3476
--     <-> 2906,9876, same level in the dungeon frame) by climb; the shock gate
--     shockgater 2902,9873 (door_selfstage: its open leaf is the same symbol one
--     tile east) by pass_door both ways.
--   * The garden is one closed component (comp.py: 101 tiles, exits the back
--     door and the shed door only). It is WALKED round its perimeter by
--     walk_route (the witch, nora_t_hagg.rs2, patrols a sealed hedge strip
--     z 3462-3464 and catches a player within 3 tiles with line of sight;
--     the lanes z=3460 and z=3466 have a hedge between them and her), never
--     goto'd across.
-- No goto lands on a loc: the pot, the fountain and the ladders are pressed
-- from the open tile beside them.
--
-- Staged levels (BRIEF "New since b63-b66" (c)): Magic 37 (the Falador
-- Teleport) and the combat stats lift the combat level; no dialogue on this
-- route reads it (boy.rs2 has no ~player_combat_level branch; nora_t_hagg says
-- nothing; the shed fight is not dialogue), so the staged player sees the only
-- branch there is.

return {
    id = "ball",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give cheese 3", -- "Cheese (multiple if you mess up)" -- Quest Helper's own note
        "::give leather_gloves 1", -- see header: no box-search trigger exists in this content pack
        "::give rune_scimitar 1",
        "::give adamant_platebody 1", -- rune_platebody refuses Wear without Dragon Slayer (levelrequire.rs2:179-182) -- a prerequisite this quest does not grant, so a body armour with no such gate is given instead (99 defence carries the fight either way)
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 4", -- food, same idiom as hero.lua/mortton.lua/rovingelves.lua
        -- Combat levels: prerequisite for the four full-health shapeshifter
        -- forms (witches_house.npc: glob atk18/def19, spider atk28/def29,
        -- bear atk38/def39, wolf atk48/def49hp51) -- same idiom hero.lua
        -- uses for its own level-111-class fight.
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        -- The trip out of Lumbridge is a real Falador Teleport (magic_spells.dbrow
        -- [magic_spell_teleport_falador]: level 37, waterrune 1 + airrune 3 + lawrune 1,
        -- tele_coord 0_46_52_21_50 = 2965,3378).
        "::setlevel magic 37",
        "::give waterrune 1",
        "::give airrune 3",
        "::give lawrune 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp226_ballquest",
            constants = {
                complete = 7,
                defeated_experiment = 6,
                found_magnet = 2,
                not_started = 0,
                questpoints = 4,
                read_diary_after_door = 5,
                started = 1,
                unlocked_mousedoor = 3,
            },
            row = "quest_witchshouse",
            display = "Witch's House",
            points = 4,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function count(item)
            local r, n = t.inv.count(item)
            if r == "ok" then
                return n
            end
            return nil
        end
        local function tile_now()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then
                return tt
            end
            return nil
        end
        local function tile_text(tt)
            if tt then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return "unknown"
        end

        -- The witch's house doors (all ~ball_walk_door walk-throughs, cross_gate).
        local function front_door_in(name)
            t.exec(name, t.player.cross_gate, { loc = "witchhousedoor", at = { 2900, 3473, 0 },
                near = { 2900, 3473 }, far_ok = function(tile) return tile.x >= 2901 end,
                far_desc = "inside the witch's house, x >= 2901" })
        end
        local function front_door_out(name)
            t.exec(name, t.player.cross_gate, { loc = "witchhousedoor", at = { 2900, 3473, 0 },
                near = { 2901, 3473 }, far_ok = function(tile) return tile.x <= 2900 end,
                far_desc = "outside the witch's house, x <= 2900" })
        end
        local function back_door_out(name)
            t.exec(name, t.player.cross_gate, { loc = "witchbackdoor", at = { 2901, 3465, 0 },
                near = { 2901, 3466 }, far_ok = function(tile) return tile.z <= 3465 end,
                far_desc = "in the garden, z <= 3465" })
        end
        local function back_door_in(name)
            t.exec(name, t.player.cross_gate, { loc = "witchbackdoor", at = { 2901, 3465, 0 },
                near = { 2901, 3465 }, far_ok = function(tile) return tile.z >= 3466 end,
                far_desc = "in the south room, z >= 3466" })
        end
        -- The two interior doors (plain doors.loc doors, opened leaf stays 500 ticks).
        local INTERIOR = {
            ladder = { 2902, 3474 }, -- middle room z <= 3474 | ladder room z >= 3475
            south = { 2902, 3467 }, -- south room z <= 3467 | middle room z >= 3468
        }
        local function interior_door(name, which, near_z, far_z)
            local d = INTERIOR[which]
            t.exec(name, t.player.pass_door, { closed = "grim_witch_house_door", open = "grim_witch_house_door_open",
                at = { d[1], d[2], 0 }, near = { d[1], near_z }, far = { d[1], far_z } })
        end

        -- ---- The garden: sneak past the witch (nora_t_hagg.rs2) ----
        -- She patrols z=3463 between x 2904 and 2930 (witches_house.npc patrol1-6)
        -- inside a sealed hedge strip, about a tile a tick, and every tick
        -- huntall(npc_coord, 3) catches a player within 3 tiles in line of sight
        -- unless a hedge shields him: north of her, a `hedge`/`hedgecorner` on
        -- the tile south of him; south of her, a `hedge` on the tile north of him
        -- (dz = 0 is never caught). Caught = teleported out to 2929,3456 with the
        -- shed key and the ball deleted and the shed relocked. The hedge rows
        -- have GAPS (locs_near.py, m45_54.jl2): z=3461 holds `hedge` only at x
        -- 2900 2903 2907-2909 2915-2917 2923-2925 2929-2931; z=3465 holds
        -- hedge/hedgecorner at x 2902-2909 2911-2914 2919-2921 2926-2928 2932.
        -- So the perimeter lanes (z=3460 below her, z=3466 above her, x=2901 and
        -- x=2933 at the ends) are walked in hops from one shielded or out-of-
        -- reach tile to the next, each hop started only when her LIVE tile
        -- cannot come within 3 of any exposed tile of the hop in the time the
        -- hop takes (worst case: she walks straight at it). The waits are
        -- notes; each hop is a graded walk_route row; the outcome is the shed
        -- key / ball still in the pack.
        local WITCH_MIN_X, WITCH_MAX_X, WITCH_Z = 2904, 2930, 3463
        local SHIELD_BELOW, SHIELD_ABOVE = {}, {}
        for _, x in ipairs({ 2900, 2903, 2907, 2908, 2909, 2915, 2916, 2917, 2923, 2924, 2925, 2929, 2930, 2931 }) do
            SHIELD_BELOW[x] = true -- a `hedge` at x,3461 shields x,3460
        end
        for _, x in ipairs({ 2902, 2903, 2904, 2905, 2906, 2907, 2908, 2909, 2911, 2912, 2913, 2914,
            2919, 2920, 2921, 2926, 2927, 2928, 2932 }) do
            SHIELD_ABOVE[x] = true -- a `hedge`/`hedgecorner` at x,3465 shields x,3466
        end
        local function exposed(x, z)
            local dz = z - WITCH_Z
            if dz == 0 or dz > 3 or dz < -3 then
                return false
            end
            if x <= WITCH_MIN_X - 4 or x >= WITCH_MAX_X + 4 then
                return false
            end
            if x <= 2903 and z >= 3466 then
                return false -- inside the south room: the house wall blocks her sight
            end
            if dz < 0 then
                return not (z == 3460 and SHIELD_BELOW[x])
            end
            return not (z == 3466 and SHIELD_ABOVE[x])
        end
        local function witch_x()
            local r, row = t.npc.nearest("nora_t_hagg", 30)
            if r == "ok" and type(row) == "table" and row.x then
                return row.x, row.x .. "," .. row.z
            end
            return nil, tostring(r)
        end
        -- Every tile of an axis-aligned corner chain, from (x0, z0); a corner
        -- tile carries .corner.
        local function expand(x0, z0, corners)
            local tiles, x, z = {}, x0, z0
            for _, c in ipairs(corners) do
                while x ~= c[1] or z ~= c[2] do
                    if x ~= c[1] then
                        x = x + (c[1] > x and 1 or -1)
                    else
                        z = z + (c[2] > z and 1 or -1)
                    end
                    tiles[#tiles + 1] = { x, z }
                end
                if #tiles > 0 then
                    tiles[#tiles].corner = true
                end
            end
            return tiles
        end
        -- Can she reach 3 tiles of an exposed tile of `tiles` (k-th tile entered
        -- about k ticks from now, plus `lead` ticks before the first step)?
        local SLACK, MARGIN = 3, 1
        local function hop_safe(tiles, wx, px, lead)
            -- Where she can be now: her live x, or -- not in the client's npc pool --
            -- anywhere on the strip more than 15 tiles from the player.
            local spans = {}
            if wx then
                spans[1] = { wx, wx }
            else
                if px - 16 >= WITCH_MIN_X then
                    spans[#spans + 1] = { WITCH_MIN_X, math.min(px - 16, WITCH_MAX_X) }
                end
                if px + 16 <= WITCH_MAX_X then
                    spans[#spans + 1] = { math.max(px + 16, WITCH_MIN_X), WITCH_MAX_X }
                end
            end
            for k, tile in ipairs(tiles) do
                if exposed(tile[1], tile[2]) then
                    local reach_ticks = k + SLACK + lead
                    for _, span in ipairs(spans) do
                        local lo = math.max(WITCH_MIN_X, span[1] - reach_ticks)
                        local hi = math.min(WITCH_MAX_X, span[2] + reach_ticks)
                        local nearest = math.min(math.max(tile[1], lo), hi)
                        if math.abs(tile[1] - nearest) <= 3 + MARGIN then
                            return false
                        end
                    end
                end
            end
            return true
        end
        -- Wait (at most `limit` ticks) for a window in which `tiles` are safe.
        local function await_window(what, tiles, lead, limit)
            local here = tile_now()
            local px = here and here.x or 2917
            local waited, seen = 0, "none"
            local wx
            for _ = 0, limit do
                wx, seen = witch_x()
                if hop_safe(tiles, wx, px, lead) then
                    t.note(what .. ": witch at " .. tostring(seen) .. ", window after " .. waited .. " tick(s)")
                    return true
                end
                t.ticks(1)
                waited = waited + 1
            end
            t.note(what .. ": NO safe window in " .. limit .. " tick(s), witch last at " .. tostring(seen) .. " -- walking anyway")
            return false
        end
        -- Walk a corner chain as alternating runs: a run of unexposed tiles is
        -- walked at once; a run of exposed tiles (with the safe tile that ends
        -- it) is walked only in a window. One walk_route row per run:
        -- <name>.1, <name>.2, ...
        local function sneak(name, corners)
            local here = tile_now()
            assert(here ~= nil, name .. ": no tile reading before the garden walk")
            local runs, cur = {}, nil
            for _, tile in ipairs(expand(here.x, here.z, corners)) do
                local e = exposed(tile[1], tile[2])
                if cur ~= nil and e and not cur.exposed then
                    runs[#runs + 1] = cur
                    cur = nil
                end
                cur = cur or { tiles = {}, way = {}, exposed = false }
                cur.tiles[#cur.tiles + 1] = tile
                cur.exposed = cur.exposed or e
                local last_way = cur.way[#cur.way]
                if tile.corner or (#cur.tiles - (cur.last_way_index or 0)) >= 8 then
                    cur.way[#cur.way + 1] = { tile[1], tile[2] }
                    cur.last_way_index = #cur.tiles
                end
                if cur.exposed and not e then
                    if last_way == nil or cur.way[#cur.way][1] ~= tile[1] or cur.way[#cur.way][2] ~= tile[2] then
                        cur.way[#cur.way + 1] = { tile[1], tile[2] }
                    end
                    runs[#runs + 1] = cur
                    cur = nil
                end
            end
            if cur ~= nil then
                runs[#runs + 1] = cur
            end
            for i, run in ipairs(runs) do
                local last = run.tiles[#run.tiles]
                local tail = run.way[#run.way]
                if tail == nil or tail[1] ~= last[1] or tail[2] ~= last[2] then
                    run.way[#run.way + 1] = { last[1], last[2] }
                end
                local row = name .. "." .. i
                if run.exposed then
                    await_window(row, run.tiles, 0, 160)
                end
                t.exec(row, t.player.walk_route, run.way)
            end
        end

        -- Wear the rune loadout and the gloves now -- the gloves matter at
        -- the shock gate below, the armour/weapon at the shed fight later.
        t.exec("wearGloves", t.player.equip, "leather_gloves")
        t.exec("wearWeapon", t.player.equip, "rune_scimitar")
        t.exec("wearBody", t.player.equip, "adamant_platebody")
        t.exec("wearLegs", t.player.equip, "rune_platelegs")
        t.exec("wearHelm", t.player.equip, "rune_full_helm")
        t.exec("wearShield", t.player.equip, "rune_kiteshield")

        -- ---- Lumbridge -> Taverley: Falador Teleport, then the members' east gate ----
        t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "talkToBoy.faladorTeleport",
            runes = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } },
            where = "Falador square, tele_coord 0_46_52_21_50" })
        -- Overland from Falador's square to the open ground east of the members'
        -- east gate (reach.py 2965,3378 -> 2938,3450 REACH closed-doors).
        t.exec("goto-talkToBoy.memberGate", t.player.goto_tile, 2938, 3450, 0)
        -- membergater 2935,3450 (gates.rs2 [label,member_fencegate_try], a
        -- walk-through, rot 2: Taverley is x <= 2935).
        t.exec("talkToBoy.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
            near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
            far_desc = "inside Taverley, x <= 2935" })

        -- ---- Start the quest: talk to the Boy in Taverley ----
        -- Inside Taverley, open ground (reach.py 2934,3450 -> 2928,3456 REACH len 12).
        t.exec("goto-talkToBoy", t.player.goto_tile, 2928, 3456, 0)
        t.exec("talkToBoy", t.player.talk_to, "ballboy", 1)
        t.exec("talkToBoy-dialog", t.chat.play, {
            "player:Hello young man.",
            "mesbox:The boy sobs.",
            "choose:What's the matter?",
            "player:What's the matter?",
            "npc:I've kicked my ball over that hedge, into that garden!",
            "choose:Ok, I'll see what I can do.",
            "player:Ok, I'll see what I can do.",
            "npc:Thanks",
        })
        t.ticks(2) -- trap 25: the branch's own varp write is not readable in the tick it was clicked
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---- Get the house key from under the pot ----
        -- witchpot 2900,3474 is solid: stand on the open tile west of the front
        -- door (reach.py 2928,3456 -> 2899,3473 REACH len 48, open Taverley) and
        -- press the pot from there.
        t.exec("goto-getKey", t.player.goto_tile, 2899, 3473, 0)
        -- The front door first, without the key: locked from outside.
        local locked_result, locked_detail = t.player.click_loc("witchhousedoor", 1)
        t.note("front door without the key: click_loc -> " .. tostring(locked_result) .. " " .. tostring(locked_detail))
        local locked_line, locked_line_detail = t.msg.expect("The door is locked.")
        local still_out = tile_now()
        t.check("getKey.doorLocked", locked_line == "ok" and still_out ~= nil and still_out.x <= 2900,
            tostring(locked_line) .. " " .. tostring(locked_line_detail) .. "; still at " .. tile_text(still_out)
                .. " outside (no witches_doorkey in the pack)")
        t.exec("getKey", t.player.click_loc, "witchpot", 1)
        -- trap 22: ~mesbox suspends the branch -- inv_add runs only once the
        -- page is dismissed. Dismiss before reading the count.
        t.exec("getKey-dismiss", t.chat.continue_, true)
        -- trap 24: a click/dismiss's own ok is the server's SENTENCE, not
        -- the container update -- poll, never a bare count on the next line.
        t.exec("keyFound.await", t.inv.await, "witches_doorkey", 1, 10)

        -- ---- Enter the house, through the ladder room door, down the ladder ----
        -- [oploc1,witchhousedoor] wants the key from OUTSIDE (quest_ball_locs.rs2,
        -- $entering; wiki: "the front door of the house - it is locked"): the
        -- door opens only because the key is in the pack, and keeps it.
        local key_at_door = count("witches_doorkey")
        front_door_in("enterHouse")
        local key_inside = count("witches_doorkey")
        t.check("enterHouse.key", key_at_door == 1 and key_inside == 1,
            "witches_doorkey " .. tostring(key_at_door) .. " at the door -> " .. tostring(key_inside)
                .. " inside (the key opens the door from outside and is not consumed)")
        interior_door("goDownstairs.ladderRoomDoorIn", "ladder", 3474, 3475)
        -- grim_witch_ladder_down (grim_witchhouse.rs2, seam matthew-mbp-m4-b67-seam1):
        -- while Grim Tales is unstarted it falls through to ~climb_ladder(-1), which
        -- asks maplink_0_45_54_26_20_down first (2906,3476 -> 2906,9876).
        t.exec("goDownstairs", t.player.climb, { loc = "grim_witch_ladder_down", op = 1, op_name = "Climb-down",
            at = { 2907, 3476, 0 }, src = { 2906, 3476 }, dest = { 2906, 9876, 0 } })

        -- ---- Cross the basement shock gate wearing the gloves ----
        -- shockgater 2902,9873 (east edge of its tile): ball_irongate_open shocks a
        -- bare-handed player, else ~door_selfstage_open swings it -- the open leaf
        -- is the same symbol one tile east (2903,9873), standing 500 ticks.
        local _, hp_before_gate = t.skill.read("hitpoints")
        t.exec("enterGate", t.player.pass_door, { closed = "shockgater", open = "shockgater",
            at = { 2902, 9873, 0 }, near = { 2903, 9873 }, far = { 2901, 9873 } })
        local _, hp_after_gate = t.skill.read("hitpoints")
        local hp_b = type(hp_before_gate) == "table" and hp_before_gate.level or nil
        local hp_a = type(hp_after_gate) == "table" and hp_after_gate.level or nil
        t.check("enterGate.no_shock", hp_b ~= nil and hp_a ~= nil and hp_a == hp_b,
            "hitpoints " .. tostring(hp_b) .. " -> " .. tostring(hp_a)
                .. " (gloves worn -- ball_irongate_open should take the ~door_selfstage_open arm, not the shock)")

        -- ---- Open the cupboard, search it for the magnet ----
        t.exec("openCupboardAndLoot", t.player.click_loc, "magnetcbshut", 1)
        t.ticks(1) -- trap 8: let the loc_change land before the second click sees the new form
        t.exec("openCupboardAndLoot2", t.player.click_loc, "magnetcbopen", 1)
        t.exec("openCupboardAndLoot2-dismiss", t.chat.continue_, true) -- trap 22
        t.exec("magnetFound.await", t.inv.await, "magnet", 1, 10) -- trap 24
        t.expect("quest.stage.found_magnet", t.quest.expect_stage("found_magnet"))

        -- ---- Back through the gate, up the ladder ----
        t.exec("goBackUpstairs.gateOut", t.player.pass_door, { closed = "shockgater", open = "shockgater",
            at = { 2902, 9873, 0 }, near = { 2902, 9873 }, far = { 2904, 9874 } })
        -- grim_witch_ladder_up falls through to ~climb_ladder(1) while Grim Tales is
        -- unstarted: maplink_0_45_154_26_20_up lands 2906,3476, the ladder room.
        t.exec("goBackUpstairs", t.player.climb, { loc = "grim_witch_ladder_up", op = 1, op_name = "Climb-up",
            at = { 2907, 9876, 0 }, src = { 2906, 9876 }, dest = { 2906, 3476, 0 } })

        -- ---- Through the ladder room door and the south room door ----
        interior_door("useCheeseOnHole.ladderRoomDoorOut", "ladder", 3475, 3474)
        interior_door("useCheeseOnHole.southRoomDoorIn", "south", 3468, 3467)

        -- ---- Cheese the mouse hole, fit the magnet to the mouse ----
        local cheese_before = count("cheese")
        local mousehole = t.player.by_symbol("loc", "witchmousehole")
        t.exec("useCheeseOnHole", t.player.use_on, "cheese", mousehole)
        t.exec("useCheeseOnHole-dismiss", t.chat.continue_, true) -- trap 22
        local cheese_after = count("cheese")
        t.check("useCheeseOnHole.cheese", cheese_before ~= nil and cheese_after ~= nil
            and cheese_after == cheese_before - 1,
            "cheese " .. tostring(cheese_before) .. " -> " .. tostring(cheese_after)
                .. " ([oplocu,witchmousehole] -> @ball_cheese: inv_del cheese 1, npc_add witchrat)")
        -- trap 12: npc.await_present is hollow (ok, no detail) -- call it
        -- directly and read the pool back with npc.nearest for the row.
        local ratout_result = t.npc.await_present("witchrat", 5, 10)
        local rat_result, witchrat_row = t.npc.nearest("witchrat", 5)
        t.check("ratOut", ratout_result == "ok" and rat_result == "ok",
            "npc.await_present -> " .. tostring(ratout_result) .. "; npc.nearest(witchrat) -> " .. tostring(rat_result)
                .. (type(witchrat_row) == "table" and (" at " .. tostring(witchrat_row.x) .. "," .. tostring(witchrat_row.z)) or ""))
        local witchrat = t.player.by_symbol("npc", "witchrat")
        local magnet_before = count("magnet")
        t.exec("fitMagnetToMouse", t.player.use_on, "magnet", witchrat)
        t.ticks(2) -- trap 25
        local magnet_after = count("magnet")
        t.check("fitMagnetToMouse.magnet", magnet_before == 1 and magnet_after == 0,
            "magnet " .. tostring(magnet_before) .. " -> " .. tostring(magnet_after)
                .. " ([opnpcu,witchrat]: inv_del magnet 1)")
        t.expect("quest.stage.unlocked_mousedoor", t.quest.expect_stage("unlocked_mousedoor"))

        -- ---- Pick up and read the witch's diary (keeps the door unlocked
        -- if read while at exactly ball_unlocked_mousedoor) ----
        -- trap 12/section 8: click_obj is hollow (ok, nil detail) -- grade the
        -- row on the backpack count landing.
        local pickup_diary_result = t.player.click_obj("witches_diary", 3)
        t.note("click_obj witches_diary -> " .. tostring(pickup_diary_result))
        t.exec("pickupDiary.await", t.inv.await, "witches_diary", 1, 10) -- trap 24
        t.exec("readDiary", t.player.inv_op, "witches_diary", 1)
        t.exec("readDiary-drain", t.chat.drain, { stop_at = "none" })
        t.ticks(2) -- trap 25
        t.expect("quest.stage.read_diary_after_door", t.quest.expect_stage("read_diary_after_door"))

        -- ---- Out the back door, round the garden to the fountain ----
        -- The press lands on 2901,3465 (exposed), so it waits for a window that
        -- also covers the walk on to 2901,3463 (dz = 0, never caught).
        await_window("searchFountain.backDoorOut", { { 2901, 3465 }, { 2901, 3464 }, { 2901, 3463 } }, 6, 160)
        back_door_out("searchFountain.backDoorOut")
        -- The garden perimeter (comp.py: one component, 101 tiles): down the west
        -- lane, east along z=3460, up x=2933, west along z=3466 and into the
        -- fountain's corner (z >= 3467: out of her reach).
        t.exec("searchFountain.toLane", t.player.walk_route, { { 2901, 3463 } })
        sneak("searchFountain.garden", { { 2901, 3460 }, { 2933, 3460 }, { 2933, 3466 }, { 2912, 3466 },
            { 2912, 3467 }, { 2911, 3467 }, { 2911, 3470 } })
        -- witchfountain 2909,3470 is a 2x2 (2909-2910,3470-3471): checked from 2911,3470.
        t.exec("searchFountain", t.player.click_loc, "witchfountain", 2)
        t.exec("searchFountain-dismiss", t.chat.continue_, true) -- trap 22
        t.exec("searchFountain.await", t.inv.await, "witches_shedkey", 1, 10) -- trap 24

        -- ---- Back round to the shed, use the shed key on its door ----
        sneak("enterShed.garden", { { 2911, 3467 }, { 2912, 3467 }, { 2912, 3466 }, { 2933, 3466 }, { 2933, 3463 } })
        local key_on_arrival = count("witches_shedkey")
        t.check("enterShed.keyKept", key_on_arrival == 1,
            "witches_shedkey " .. tostring(key_on_arrival) .. " at the shed door (nora_t_hagg.rs2 deletes it if she sees you)")
        -- [oplocu,witchsheddoor] from the garden side (not on the door's column):
        -- unlocks the shed (varp6660), spawns the experiment and p_teleports the
        -- player onto the door tile, inside. The key is not consumed; the use's
        -- effect is the landing inside and the experiment standing there.
        local shed_door = t.player.by_symbol("loc", "witchsheddoor")
        local key_before = count("witches_shedkey")
        -- A pushed crossing: the use can answer `timeout settle_after_click` though
        -- it landed (b67 probe run 4), so the row is graded on the landing.
        local use_result, use_detail = t.player.use_on("witches_shedkey", shed_door)
        local in_shed = tile_now()
        t.check("enterShed", in_shed ~= nil and in_shed.level == 0 and in_shed.x >= 2934
            and in_shed.z >= 3459 and in_shed.z <= 3466,
            "use witches_shedkey on witchsheddoor -> " .. tostring(use_result) .. " " .. tostring(use_detail)
                .. "; landed " .. tile_text(in_shed) .. " (shed interior x >= 2934), shed key "
                .. tostring(key_before) .. " -> " .. tostring(count("witches_shedkey")) .. " (not consumed)")
        local glob_result = t.npc.await_present("shapeshifterglob", 6, 6)
        t.check("enterShed.experiment", glob_result == "ok",
            "npc.await_present(shapeshifterglob) -> " .. tostring(glob_result) .. " (~ball_experiment_spawn)")
        -- entering the shed does not itself change ballquest -- it stays at
        -- read_diary_after_door until the experiment is defeated.
        t.expect("quest.stage.read_diary_after_door.still", t.quest.expect_stage("read_diary_after_door"))

        -- ---- Fight all four forms of the shapeshifter (one live uid that
        -- changetypes through glob -> spider -> bear -> wolf on each kill,
        -- quest_ball_locs.rs2's [ai_queue3,...] chain). Real combat blocks:
        -- witches_house.npc (hp 21/31/41/51, max hits 2/3/4/5); all four in
        -- docs/bosses/quest_combat_manifest.json. ----
        local EAT = { item = "shark", below = 50 }
        local food_before = count("shark")
        local _, attack_detail = t.exec("killWitchsExperiment", t.player.attack, "shapeshifterglob", 2, 10, { eat = EAT })
        local _, dead_detail = t.exec("killWitchsExperiment.dead", t.npc.await_dead_engaged, 240, 12, { eat = EAT })
        dead_detail = tostring(dead_detail)
        -- Each form, read from the kill wait's own changetype chain and the
        -- server's transformation line for it.
        local FORMS = {
            { "killWitchsExperiment.form1", "Witch's experiment (npc", "the glob (level 19)", nil },
            { "killWitchsExperiment.form2", "(second form)", "the spider (level 30)", "The shapeshifter turns into a spider!" },
            { "killWitchsExperiment.form3", "(third form)", "the bear (level 42)", "The shapeshifter turns into a bear!" },
            { "killWitchsExperiment.form4", "(fourth form)", "the wolf (level 53)", "The shapeshifter turns into a wolf!" },
        }
        for _, f in ipairs(FORMS) do
            local held = dead_detail:find(f[2], 1, true) ~= nil
            local line_result, line_detail = "ok", "(the first form needs no line)"
            if f[4] then
                line_result, line_detail = t.msg.expect(f[4])
            end
            t.check(f[1], held and line_result == "ok",
                f[3] .. ": the kill wait " .. (held and "held it" or "never held it") .. " (" .. f[2] .. "); "
                    .. tostring(line_result) .. " " .. tostring(line_detail))
        end
        t.exec("killWitchsExperiment.finalLine", t.msg.expect, "You finally kill the shapeshifter once and for all.")
        -- Margin (BRIEF: lowest hp at least a quarter of the maximum AND food left).
        -- The lowest hp the eaters read over the press and the whole four-form wait
        -- bounds every form's own lowest.
        local low_press = tonumber(tostring(attack_detail):match("lowest hp (%d+)/"))
        local low_wait = tonumber(dead_detail:match("lowest hp (%d+)/"))
        local lowest = low_wait
        if low_press and (lowest == nil or low_press < lowest) then
            lowest = low_press
        end
        local _, hp_now = t.skill.read("hitpoints")
        local max_hp = type(hp_now) == "table" and hp_now.base_level or nil
        local food_after = count("shark")
        t.check("killWitchsExperiment.margin", lowest ~= nil and max_hp ~= nil and food_after ~= nil
            and lowest * 4 >= max_hp and food_after >= 1,
            "all four forms: lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp)
                .. " (press " .. tostring(low_press) .. ", wait " .. tostring(low_wait) .. "), sharks "
                .. tostring(food_before) .. " -> " .. tostring(food_after)
                .. " (margin: lowest hp >= a quarter of max AND at least one shark left)")
        t.expect("quest.stage.defeated_experiment", t.quest.expect_stage("defeated_experiment"))

        -- ---- Pick up the ball off the crate ----
        local pickup_ball_result = t.player.click_obj("ball", 3) -- trap 12: click_obj is hollow
        t.note("click_obj ball -> " .. tostring(pickup_ball_result))
        t.exec("pickupBall.await", t.inv.await, "ball", 1, 10) -- trap 24

        -- ---- Leave the shed, back round the garden, through the house ----
        t.exec("returnToBoy.shedDoorOut", t.player.cross_gate, { loc = "witchsheddoor", at = { 2934, 3463, 0 },
            near = { 2934, 3463 }, far_ok = function(tile) return tile.x <= 2933 end,
            far_desc = "in the garden, x <= 2933" })
        sneak("returnToBoy.garden", { { 2933, 3460 }, { 2901, 3460 }, { 2901, 3463 } })
        local ball_in_garden = count("ball")
        t.check("returnToBoy.ballKept", ball_in_garden == 1,
            "ball " .. tostring(ball_in_garden) .. " after the garden (nora_t_hagg.rs2 deletes it if she sees you)")
        -- From 2901,3463 (dz = 0) the press walks two exposed tiles and waits on
        -- the door: one window covers it.
        await_window("returnToBoy.backDoorIn", { { 2901, 3464 }, { 2901, 3465 }, { 2901, 3465 }, { 2901, 3465 } }, 3, 160)
        back_door_in("returnToBoy.backDoorIn")
        interior_door("returnToBoy.southRoomDoorOut", "south", 3467, 3468)
        front_door_out("returnToBoy.frontDoorOut")

        -- ---- Return the ball to the Boy ----
        -- Open Taverley from the front door to his spawn (reach.py 2899,3473 ->
        -- 2928,3456 REACH).
        t.exec("goto-returnToBoy", t.player.goto_tile, 2928, 3456, 0)
        local snapshot_result, snapshot = t.skill.snapshot()
        t.note("skill.snapshot -> " .. tostring(snapshot_result))
        local ball_before = count("ball")
        -- The Boy is an ordinary wandering npc: walk to his LIVE tile each
        -- attempt (never a goto onto it), then talk until the press opens his
        -- dialogue. Attempts are notes; the outcome is the one check.
        local returnToBoy_result, returnToBoy_detail
        for attempt = 1, 6 do
            local boy_result, boy_row = t.npc.nearest("ballboy", 20)
            if boy_result == "ok" and type(boy_row) == "table" then
                local wr, wd = t.player.walk_to(boy_row.x, boy_row.z)
                t.note("returnToBoy attempt " .. attempt .. ": boy at " .. tostring(boy_row.x) .. "," .. tostring(boy_row.z)
                    .. "; walk_to -> " .. tostring(wr) .. " " .. tostring(wd))
            else
                t.note("returnToBoy attempt " .. attempt .. ": npc.nearest(ballboy) -> " .. tostring(boy_result))
            end
            returnToBoy_result, returnToBoy_detail = t.player.talk_to("ballboy", 1)
            if returnToBoy_result == "ok" and not tostring(returnToBoy_detail):find("no dialogue") then
                break
            end
            t.note("returnToBoy attempt " .. attempt .. ": talk_to -> " .. tostring(returnToBoy_result) .. " " .. tostring(returnToBoy_detail))
        end
        t.check("returnToBoy", returnToBoy_result == "ok"
            and not tostring(returnToBoy_detail):find("no dialogue"),
            tostring(returnToBoy_result) .. " " .. tostring(returnToBoy_detail))
        t.exec("returnToBoy-dialog", t.chat.play, {
            "player:Hi, I have got your ball back.",
            "mesbox:You give the ball back.",
            "npc:Thank you so much!",
        })
        t.ticks(3) -- section 8: completion is asynchronous, the queued ball_quest_complete lands behind the dialogue

        t.quest.expect_complete()
        t.exec("reward.hitpoints_xp", t.skill.expect_gain, "hitpoints", 6325, snapshot)
        local ball_after = count("ball")
        t.check("reward.ballHandedOver", ball_before == 1 and ball_after == 0,
            "ball " .. tostring(ball_before) .. " -> " .. tostring(ball_after) .. " (boy.rs2: inv_del ball 1)")

        t.finish(0)
        return
    end,
}
