-- Murder Mystery, end to end through the real client.
--
-- %murdersus is rolled random 1-6 at accept time (murder_guard.rs2's own
-- `%murdersus = ~random_range(1, 6)`), and it CANNOT be read by this
-- driver: quest_murder.varp declares it with no `transmit=yes`, and
-- var.server (torirs_plugin_drive_state.c's DriveState_VarpServer) reads
-- `var_serv[]`, the client's cache of values the SERVER TRANSMITTED --
-- never populated for a varp the server never sends, so it reads 0 forever
-- (measured run 1: "murdersus -> ok 0" the tick right after murderquest
-- itself read "started").
--
-- Worked around, not blocked: the thread murder_inspect_window hands out
-- (quest_murder_window.rs2's own `~get_murder_thread` proc) is one of three
-- colours, and each colour narrows %murdersus to exactly TWO of the six
-- (green: Anna/David, red: Bob/Carol, blue: Elizabeth/Frank) -- read
-- straight off the real inventory after the window click. The fingerprint
-- comparison then lets content decide which of the two it is
-- (check_murderer_print keeps murderfingerprint1 and destroys the checked
-- print on a miss, swaps it for murderfingerprint on a match), and only the
-- MATCHED suspect is then taken through the poison chain.
--
-- THE ROUTE (fix_b59, owner rule 2026-10-03: no goto into or out of any
-- closed space). Every room is entered and left on foot through its door,
-- by pass_door below:
--   * the mansion grounds are walled in (456 tiles, comp.py 2741 3562): the
--     only way in or out is the double gate murder_qip_metalgateclosedl/r
--     (2741-2742,3555, north edge). Every goto lands on, and leaves from,
--     the road south of it (2741,3552).
--   * the mansion: front double door kr_mansion_double_door_l/r (2740-2741,
--     3572, north edge) into the hall (x 2736-2744, z 3573-3582); off the
--     hall kr_sin_poshdoor 2745,3575 (W edge) to the antechamber, and
--     2746,3576 (N edge) from it to the study (x 2746-2747, z 3577-3582)
--     where the dagger and the pungent pot lie; 2735,3575 to Anna's room,
--     2735,3578 to Bob's room, 2735,3580 to the kitchen and its flour
--     barrel (all E edge).
--   * the shed with the flypaper sacks opens only to the west yard, by
--     kr_sin_poordoor 2731,3579 (N edge).
--   * the Seers' pub: its front double door stands open in the map
--     (kr_opendoubledoor_l/r 2694-2695,3488): the goto lands on the street,
--     the open leaf is asserted, and the player walks in and back out.
--
-- BLOCKED for four of the six murderers: the mansion staircase does not
-- climb. kr_mansion.rs2:31-35 ([oploc1,murder_qip_spiralstairs], a King's
-- Ransom overlay) answers "It's just a staircase." and returns for anyone
-- not on King's Ransom, which overrides ladders.rs2's [oploc1,_climb_up]
-- ~climb(1) (ladders_stairs/configs/ladders.loc:629 puts the loc in
-- category climb_up); the top (kr_mansion.rs2:48-52) is the same. Carol's,
-- David's, Elizabeth's and Frank's barrels, Carol and Elizabeth themselves
-- and David's spiders' nest are all on level 1. A thread pair whose
-- remaining candidate needs that floor presses the stairs, reads that the
-- level did not change, and ends at t.blocked. Anna and Bob are finished
-- end to end.

-- --------------------------------------------------------------- helpers

local function txt(v)
    local kind = type(v)
    if kind == "string" or kind == "number" or kind == "boolean" or kind == "nil" then
        return tostring(v)
    end
    return "<" .. kind .. ">"
end

local function tile_text(r, tt)
    if r == "ok" and type(tt) == "table" then
        return tt.x .. "," .. tt.z .. "," .. tt.level
    end
    return tostring(r)
end

-- Walk to x,z on level 0 and grade the tile reached: within one tile of the
-- target and on the side `side_ok` names (walk_to answers ok with no detail,
-- so the tile read is the evidence).
local function walk_check(t, name, x, z, side_ok, side_desc)
    local wr = t.player.walk_to(x, z, 60)
    local tr, tt = t.world.tile()
    t.check(name,
        tr == "ok" and tt.level == 0 and math.abs(tt.x - x) <= 1 and math.abs(tt.z - z) <= 1 and side_ok(tt),
        "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. "; tile " .. tile_text(tr, tt)
            .. " (want within 1 on level 0, " .. side_desc .. ")")
end

-- Cross one door or gate on foot (the pass_door pattern of
-- docs/quest_authoring/sampler-findings.md, b56-b58). Walk to the near
-- side. Then press the CLOSED leaf by tile AND level ({ at = {x, z, 0} }):
-- kr_sin_poshdoor has a level-1 copy straight above 2735,3578 and
-- 2735,3580, and a selector never falls back to another copy. A selector
-- that matches no closed copy answers `no_row` and presses NOTHING -- an
-- earlier crossing left the door open (a door swings back after 500
-- ticks) -- so the open leaf is asserted standing within a tile of the door
-- tile on level 0 instead, a row that fails when neither leaf is there.
-- Then walk through and grade the far tile.
local function pass_door(t, prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
    walk_check(t, prefix .. ".atDoor", near_x, near_z,
        function(tt) return not far_ok(tt) end,
        "this side of " .. closed_sym .. " at " .. door_x .. "," .. door_z)
    local cr, cd = t.player.click_loc(closed_sym, 1, { at = { door_x, door_z, 0 } })
    if cr == "no_row" then
        local orr, od = t.world.loc_near(open_sym, 3)
        t.check(prefix .. ".doorStandsOpen",
            orr == "ok" and od.level == 0
                and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
            "no closed " .. closed_sym .. " at " .. door_x .. "," .. door_z .. ",0 (" .. txt(cd) .. "); "
                .. open_sym .. ": "
                .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                .. " (want the open leaf within 1 of the door tile on level 0: an earlier press left it open, so it is walked through, not pressed again)")
    else
        t.ticks(1)
        t.check(prefix .. ".openDoor", cr == "ok",
            "click_loc(" .. closed_sym .. ", 1, at " .. door_x .. "," .. door_z .. ",0) -> " .. tostring(cr) .. " " .. txt(cd))
    end
    walk_check(t, prefix .. ".throughDoor", far_x, far_z, far_ok, far_desc)
end

-- Await `await_sym` in the pack, then grade the exact counts `want` lists
-- ({ {sym, n}, ... }): a "use X on Y" row proves the used item LEFT the pack
-- and the product arrived, not just that the click answered.
local function pack_after(t, name, await_sym, want)
    local ar, ad = t.inv.await(await_sym, 1, 6)
    local ok = ar == "ok"
    local parts = { "inv.await(" .. await_sym .. ") -> " .. tostring(ar) .. " " .. txt(ad) }
    for i = 1, #want do
        local cr, c = t.inv.count(want[i][1])
        ok = ok and cr == "ok" and c == want[i][2]
        parts[#parts + 1] = want[i][1] .. "=" .. txt(c) .. " (want " .. want[i][2] .. ")"
    end
    t.check(name, ok, table.concat(parts, "; "))
end

-- Sites (maps/m42_55.jl2).
local GATE, GATE_OPEN = "murder_qip_metalgateclosedl", "murder_qip_metalgateopenl"
local FRONT, FRONT_OPEN = "kr_mansion_double_door_l", "kr_mansion_open_double_door_l"
local POSH, POSH_OPEN = "kr_sin_poshdoor", "kr_sin_poshdooropen"
local POOR, POOR_OPEN = "kr_sin_poordoor", "kr_sin_poordooropen"

local function in_grounds(tt) return tt.z >= 3556 end
local function on_road(tt) return tt.z <= 3555 end
local function in_hall(tt) return tt.x >= 2736 and tt.x <= 2744 and tt.z >= 3573 and tt.z <= 3582 end
local function outside_front(tt) return tt.z <= 3572 end
local function in_antechamber(tt) return tt.x >= 2745 and tt.z >= 3574 and tt.z <= 3576 end
local function in_study(tt) return tt.x >= 2746 and tt.z >= 3577 and tt.z <= 3582 end
local function in_kitchen(tt) return tt.x >= 2733 and tt.x <= 2735 and tt.z >= 3580 and tt.z <= 3582 end
local function in_shed(tt) return tt.x >= 2731 and tt.x <= 2732 and tt.z >= 3580 and tt.z <= 3581 end
local function west_yard(tt) return tt.z <= 3579 and tt.x <= 2732 end

return {
    id = "murder",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::give pot_empty 1",
    },

    run = function(t)
        -- Suspect data. npc/barrel/item/poison_loc symbols are quest_murder's
        -- own triggers; barrel_level is the floor the barrel stands on
        -- (maps/m42_55.jl2), proof is the line quest_murder_poisonproof.rs2's
        -- murderer arm shows. A downstairs barrel's room door is given as
        -- { door x,z, hall-side tile, room-side tile, room predicate }.
        local SUS = {
            [1] = {
                name = "anna", npc = "kr_anna_sinclair_multi",
                barrel = "murderbarrela", barrel_level = 0,
                room = { 2735, 3575, 2736, 3575, 2734, 3576,
                    function(tt) return tt.x >= 2733 and tt.x <= 2735 and tt.z >= 3574 and tt.z <= 3577 end,
                    "in Anna's room x 2733-2735 z 3574-3577" },
                talk_in_room = true,
                item = "murdernecklace", itemdust = "murdernecklacedust", print = "murderfingerprinta",
                poison_loc = "murdercompost", proof = "nobody's used poison here",
            },
            [2] = {
                name = "bob", npc = "kr_bob_sinclair_multi",
                barrel = "murderbarrelb", barrel_level = 0,
                room = { 2735, 3578, 2736, 3578, 2734, 3578,
                    function(tt) return tt.x >= 2733 and tt.x <= 2735 and tt.z >= 3578 and tt.z <= 3579 end,
                    "in Bob's room x 2733-2735 z 3578-3579" },
                talk_in_room = false,
                item = "murdercup", itemdust = "murdercupdust", print = "murderfingerprintb",
                poison_loc = "murderhive", proof = "don't seem poisoned at all",
            },
            [3] = {
                name = "carol", npc = "kr_carol_sinclair_multi",
                barrel = "murderbarrelc", barrel_level = 1,
                item = "murderbottle", itemdust = "murderbottledust", print = "murderfingerprintc",
                poison_loc = "murderdrain", proof = "nobody's cleaned it recently",
            },
            [4] = {
                name = "david", npc = "kr_david_sinclair_multi",
                barrel = "murderbarreld", barrel_level = 1,
                item = "murderbook", itemdust = "murderbookdust", print = "murderfingerprintd",
                poison_loc = "murderweb", proof = "nobody's used poison here",
            },
            [5] = {
                name = "elizabeth", npc = "kr_elizabeth_sinclair_multi",
                barrel = "murderbarrele", barrel_level = 1,
                item = "murderneedle", itemdust = "murderneedledust", print = "murderfingerprinte",
                poison_loc = "murderfountain", proof = "nobody's used poison here",
            },
            [6] = {
                name = "frank", npc = "kr_frank_sinclair_multi",
                barrel = "murderbarrelf", barrel_level = 1,
                item = "murderpot", itemdust = "murderpotdust", print = "murderfingerprintf",
                poison_loc = "murdersign", proof = "nobody's cleaned it recently",
            },
        }
        -- get_murder_thread's own switch_int (quest_murder_window.rs2):
        -- anna/david green, bob/carol red, elizabeth/frank blue. The
        -- downstairs candidate is listed first, so it is compared first.
        local THREAD_CANDIDATES = {
            murderthreadg = { 1, 4 },
            murderthreadr = { 2, 3 },
            murderthreadb = { 5, 6 },
        }

        t.quest.bind({
            varp = "varp192_murderquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Murder Mystery",
            points = 3,
        })

        -- ------------------------------------------------- route pieces
        local function grounds_in(tag)
            t.exec("goto.road." .. tag, t.player.goto_tile, 2741, 3552, 0)
            t.ticks(2)
            pass_door(t, "grounds.in." .. tag, GATE, GATE_OPEN, 2741, 3555, 2741, 3554, 2741, 3557,
                in_grounds, "inside the grounds, z >= 3556")
        end
        local function grounds_out(tag)
            pass_door(t, "grounds.out." .. tag, GATE, GATE_OPEN, 2741, 3555, 2741, 3556, 2741, 3553,
                on_road, "on the road south of the gate, z <= 3555")
        end
        local function mansion_in(tag)
            pass_door(t, "mansion.in." .. tag, FRONT, FRONT_OPEN, 2740, 3572, 2740, 3572, 2740, 3574,
                in_hall, "in the hall x 2736-2744 z 3573-3582")
        end
        local function mansion_out(tag)
            pass_door(t, "mansion.out." .. tag, FRONT, FRONT_OPEN, 2740, 3572, 2740, 3573, 2740, 3570,
                outside_front, "outside the front door, z <= 3572")
        end
        local function kitchen_in(tag)
            pass_door(t, "kitchen.in." .. tag, POSH, POSH_OPEN, 2735, 3580, 2736, 3580, 2734, 3581,
                in_kitchen, "in the kitchen x 2733-2735 z 3580-3582")
        end
        local function kitchen_out(tag)
            pass_door(t, "kitchen.out." .. tag, POSH, POSH_OPEN, 2735, 3580, 2735, 3580, 2737, 3580,
                in_hall, "back in the hall")
        end
        local function room_in(s, tag)
            local r = s.room
            pass_door(t, "room.in." .. tag .. "." .. s.name, POSH, POSH_OPEN, r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8])
        end
        local function room_out(s, tag)
            local r = s.room
            pass_door(t, "room.out." .. tag .. "." .. s.name, POSH, POSH_OPEN, r[1], r[2], r[1], r[2], r[3] + 1, r[4],
                in_hall, "back in the hall")
        end
        local function take_flour(tag)
            t.exec("flour.get." .. tag, t.player.click_loc, "flourbarrel", 2)
            t.exec("flour.close." .. tag, t.chat.drain, { stop_at = "none" })
            pack_after(t, "flour.have." .. tag, "pot_flour", { { "pot_flour", 1 }, { "pot_empty", 0 } })
        end

        -- The staircase is pressed for real wherever the upper floor is
        -- needed; the level it leaves the player on is the evidence for the
        -- block (see the header: kr_mansion.rs2:31-35).
        local function upstairs_blocked(why)
            walk_check(t, "stairs.atFoot", 2737, 3580, in_hall, "in the hall beside the staircase 2736,3581")
            local cr, cd = t.player.click_loc("murder_qip_spiralstairs", 1)
            local ar = t.await({
                level = function()
                    local lr, lv = t.world.level()
                    return lr == "ok" and lv == 1
                end,
                note = "stairs: waiting for level 1",
            }, 8)
            local tr, tt = t.world.tile()
            local mr, md = t.msg.expect("It's just a staircase")
            local evidence = "click_loc(murder_qip_spiralstairs, 1) -> " .. tostring(cr) .. " " .. txt(cd)
                .. "; level 1 await -> " .. tostring(ar) .. "; tile " .. tile_text(tr, tt)
                .. "; msg.expect(It's just a staircase) -> " .. tostring(mr) .. " " .. txt(md)
            t.shot("stairs.climb")
            if tr == "ok" and tt.level == 1 then
                t.blocked("the mansion staircase climbed (" .. evidence .. ") -- kr_mansion.rs2 was fixed; "
                    .. "this file has no level-1 route yet for " .. why .. ": author the upper floor's doors "
                    .. "(kr_sin_poshdoor 2736,3577 / 2740,3578 / 2744,3577 / 2745,3578 / 2745,3581 / 2735,3578 / "
                    .. "2735,3580 on level 1) and continue.")
                return
            end
            t.blocked("content_bug: " .. why .. " is on level 1, and the only way up, "
                .. "murder_qip_spiralstairs (2736,3581,0), does not climb: " .. evidence .. ". "
                .. "OSRS-Content/osrs239-content/server/scripts/quests/quest_kingsransom/scripts/kr_mansion.rs2:31-35 "
                .. "[oploc1,murder_qip_spiralstairs] answers mes(\"It's just a staircase.\") and returns when "
                .. "%varb3888_kr_quest ! ^kr_told_by_gossip, overriding ladders_stairs/scripts/ladders.rs2's "
                .. "[oploc1,_climb_up] ~climb(1) (ladders_stairs/configs/ladders.loc:629 category=climb_up); "
                .. "kr_mansion.rs2:48-52 does the same to murder_qip_spiralstairstop. Needed: fall through to "
                .. "~climb(1) / ~climb(-1) outside King's Ransom.")
        end

        -- --------------------------------------------------- accept
        grounds_in("accept")
        t.exec("talk.guard", t.player.talk_to, "murderguard", 1)
        -- The accept branch falls straight through into murderguard_help's
        -- own two pages after "Thanks a lot!" (murder_guard.rs2's
        -- unconditional `@murderguard_help;` at the end of the $start=1 arm).
        t.exec("accept.quest", t.chat.play, {
            "player:What's going on here?",
            "npc:Oh, it's terrible! Lord Sinclair has been murdered",
            "npc:If you can help us we will be very grateful",
            "options",
            "choose:Sure, I'll help.",
            "player:Sure, I'll help!",
            "npc:Thanks a lot!",
            "player:What should I be doing to help?",
            "npc:Look around and investigate",
        })
        t.exec("accept.close", t.chat.drain, { stop_at = "none" })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------------- thread evidence
        -- The guide's window (2748,3577) is searched from OUTSIDE, on the
        -- open strip east of the mansion (reach.py: 20 tiles from the guard
        -- with every door shut).
        walk_check(t, "window.walk", 2748, 3576,
            function(tt) return tt.x >= 2748 and tt.z <= 3577 end, "outside the east wall, x >= 2748")
        -- The window is the multiloc [murderwindow]; the op reaches
        -- murder_inspect_window through run_loc_trigger_with_base (73a4251d0).
        t.exec("window.inspect_click", t.player.click_loc, "kr_mansion_window_multi_01", 2)
        -- murder_inspect_window (quest_murder_window.rs2:63-81) opens a mesbox
        -- that SUSPENDS; only the resume runs the inv_add, so the chain is
        -- walked to its end.
        t.exec("window.mesbox", t.chat.drain, { stop_at = "none" })

        -- The grant lands a tick after the drain; which colour is the random
        -- roll this step reads, so this is a bounded poll over the three.
        local matched_thread = nil
        local candidates = nil
        local thread_reads_ok = true
        local thread_tick = nil
        for tick = 0, 8 do
            for _, thread_sym in ipairs({ "murderthreadg", "murderthreadr", "murderthreadb" }) do
                local r, c = t.inv.count(thread_sym)
                if r ~= "ok" then
                    thread_reads_ok = false
                elseif c >= 1 and matched_thread == nil then
                    matched_thread = thread_sym
                    candidates = THREAD_CANDIDATES[thread_sym]
                    thread_tick = tick
                end
            end
            if matched_thread ~= nil then
                break
            end
            t.ticks(1)
        end
        t.step("evidence.thread", (thread_reads_ok and candidates ~= nil) and "PASS" or "FAIL",
            "thread colour -> " .. tostring(matched_thread) .. " after "
                .. tostring(thread_tick) .. " tick(s), candidates -> "
                .. (candidates and (SUS[candidates[1]].name .. "/" .. SUS[candidates[2]].name)
                    or "none"))
        t.shot("evidence.thread" .. (candidates and "" or "-FAIL"))
        if not candidates then
            t.blocked(
                "murder_inspect_window (quest_murder_window.rs2:63-81) ran its " ..
                "mesbox but granted no thread within 8 ticks of the chain ending: " ..
                "none of murderthreadg/r/b turned up in the backpack. Without a " ..
                "colour, %varp195_murdersus (no transmit body) cannot be narrowed to a pair " ..
                "from real state.")
            return
        end

        -- ------------------------------------- the study: pot and dagger
        mansion_in("study")
        pass_door(t, "antechamber.in", POSH, POSH_OPEN, 2745, 3575, 2744, 3575, 2746, 3575,
            in_antechamber, "in the antechamber x 2745-2747 z 3574-3576")
        pass_door(t, "study.in", POSH, POSH_OPEN, 2746, 3576, 2746, 3576, 2746, 3577,
            in_study, "in the study x 2746-2747 z 3577-3582")

        -- The pungent pot (m42_55.spawn:47, 2747,3579): [opobj3,murderpot2]
        -- adds it and opens a mesbox.
        t.exec("pot.pickup", t.player.click_obj, "murderpot2")
        t.exec("pot.close", t.chat.drain, { stop_at = "none" })
        t.expect("pot.have", t.inv.await("murderpot2", 1, 6))

        -- The PLAIN dagger (m42_55.spawn:46, 2746,3578): it takes the same
        -- flour + flypaper trip every suspect's item takes below.
        local weapon_result, weapon_row = t.world.obj_near("murderweapon", 6)
        t.step("weapon.locate", weapon_result == "ok" and "PASS" or "FAIL",
            "world.obj_near(murderweapon, 6) -> " .. tostring(weapon_result)
                .. (type(weapon_row) == "table" and (" @" .. tostring(weapon_row.tile_x)
                    .. "," .. tostring(weapon_row.tile_z) .. "," .. tostring(weapon_row.level)) or ""))
        t.exec("weapon.pickup", t.player.click_obj, "murderweapon")
        t.exec("weapon.close", t.chat.drain, { stop_at = "none" })
        t.expect("weapon.have_weapon", t.inv.await("murderweapon", 1, 6))

        pass_door(t, "study.out", POSH, POSH_OPEN, 2746, 3576, 2746, 3577, 2746, 3575,
            in_antechamber, "back in the antechamber")
        pass_door(t, "antechamber.out", POSH, POSH_OPEN, 2745, 3575, 2745, 3575, 2743, 3575,
            in_hall, "back in the hall")
        mansion_out("study")

        -- ------------------------------------------ the shed: flypaper
        -- Three sheets: the dagger, and one per narrowed candidate. The sacks
        -- have no "already have" guard.
        pass_door(t, "shed.in", POOR, POOR_OPEN, 2731, 3579, 2731, 3579, 2731, 3581,
            in_shed, "in the shed x 2731-2732 z 3580-3581")
        for i = 1, 3 do
            t.exec("sack.search." .. i, t.player.click_loc, "murdersacks", 2)
            t.exec("sack.drain_to_options." .. i, t.chat.drain, { stop_at = "options" })
            t.exec("sack.choose." .. i, t.chat.choose, "Yes, it might be useful.")
            t.exec("sack.close." .. i, t.chat.drain, { stop_at = "none" })
        end
        t.expect("evidence.paper_count", t.inv.await("murderpaper", 3, 8))
        local paper = 3
        pass_door(t, "shed.out", POOR, POOR_OPEN, 2731, 3579, 2731, 3580, 2731, 3577,
            west_yard, "in the west yard, z <= 3579")

        -- ---------------------------------- the kitchen: the killer's print
        mansion_in("kitchen")
        kitchen_in("weapon")
        take_flour("weapon")
        -- quest_murder_prints.rs2 [opheldu,murderweapon] with pot_flour ->
        -- flour_proofobj: dagger and flour out, murderweapondust and the
        -- empty pot in.
        t.exec("weapon.flour", t.player.use_item_on_item, "pot_flour", "murderweapon")
        pack_after(t, "weapon.have_dust", "murderweapondust",
            { { "murderweapondust", 1 }, { "murderweapon", 0 }, { "pot_flour", 0 }, { "pot_empty", 1 } })
        -- [opheldu,murderweapondust] with murderpaper -> create_flourprints:
        -- dust and one sheet out, the dagger and murderfingerprint1 in.
        t.exec("weapon.fingerprint", t.player.use_item_on_item, "murderpaper", "murderweapondust")
        paper = paper - 1
        pack_after(t, "evidence.fingerprint1", "murderfingerprint1",
            { { "murderfingerprint1", 1 }, { "murderweapon", 1 }, { "murderweapondust", 0 }, { "murderpaper", paper } })

        -- ---------------------------------- the suspects' prints
        -- The downstairs candidate first. Each item: its own pot of flour
        -- from the kitchen, its barrel, flour, flypaper, then its print used
        -- on murderfingerprint1 ([opheldu,murderfingerprint1] ->
        -- check_murderer_print).
        local matched = nil
        local in_kitchen_now = true
        for _, sid in ipairs(candidates) do
            local s = SUS[sid]
            if s.barrel_level ~= 0 then
                if in_kitchen_now then
                    kitchen_out("stairs")
                    in_kitchen_now = false
                end
                upstairs_blocked(s.name .. "'s barrel (" .. s.barrel .. ")")
                return
            end
            if not in_kitchen_now then
                kitchen_in(s.name)
            end
            take_flour(s.name)
            kitchen_out(s.name)
            in_kitchen_now = false
            room_in(s, "barrel")
            t.exec("barrel.search." .. s.name, t.player.click_loc, s.barrel, 2)
            t.exec("barrel.close." .. s.name, t.chat.drain, { stop_at = "none" })
            t.expect("evidence.have_item." .. s.name, t.inv.await(s.item, 1, 6))

            t.exec("item.flour." .. s.name, t.player.use_item_on_item, "pot_flour", s.item)
            pack_after(t, "evidence.have_itemdust." .. s.name, s.itemdust,
                { { s.itemdust, 1 }, { s.item, 0 }, { "pot_flour", 0 }, { "pot_empty", 1 } })
            t.exec("item.paper." .. s.name, t.player.use_item_on_item, "murderpaper", s.itemdust)
            paper = paper - 1
            pack_after(t, "evidence.have_print." .. s.name, s.print,
                { { s.print, 1 }, { s.item, 1 }, { s.itemdust, 0 }, { "murderpaper", paper } })

            t.exec("fingerprint.compare." .. s.name, t.player.use_item_on_item, s.print, "murderfingerprint1")
            t.exec("fingerprint.close." .. s.name, t.chat.drain, { stop_at = "none" })
            local mr = t.inv.await("murderfingerprint", 1, 5)
            local c1r, c1 = t.inv.count("murderfingerprint1")
            local cpr, cp = t.inv.count(s.print)
            if mr == "ok" then
                -- A match swaps murderfingerprint1 for murderfingerprint and
                -- keeps the checked print.
                t.check("fingerprint.match." .. s.name, c1r == "ok" and c1 == 0 and cpr == "ok" and cp == 1,
                    "murderfingerprint arrived; murderfingerprint1=" .. txt(c1) .. " (want 0), "
                        .. s.print .. "=" .. txt(cp) .. " (want 1)")
                matched = s
            else
                -- A miss destroys the checked print and keeps
                -- murderfingerprint1: content cleared this suspect.
                t.check("fingerprint.cleared." .. s.name, c1r == "ok" and c1 == 1 and cpr == "ok" and cp == 0,
                    "no murderfingerprint within 5 ticks (" .. tostring(mr) .. "); murderfingerprint1="
                        .. txt(c1) .. " (want 1), " .. s.print .. "=" .. txt(cp) .. " (want 0, destroyed)")
            end
            room_out(s, "barrel")
            if matched then
                break
            end
        end
        t.step("evidence.fingerprint_match", matched and "PASS" or "FAIL",
            "murderfingerprint -> " .. (matched and matched.name or "nil") .. " (of "
                .. SUS[candidates[1]].name .. "/" .. SUS[candidates[2]].name
                .. "; content's own match/mismatch decided it)")
        t.shot("evidence.fingerprint_match" .. (matched and "" or "-FAIL"))
        if not matched then
            t.finish(1)
            return
        end
        mansion_out("prints")

        -- --------------------------------------------------- gossip
        -- gossipy_man stands on the road just outside the gate
        -- (m42_55.spawn:33, 2742,3555).
        grounds_out("gossip")
        t.exec("gossip.talk", t.player.talk_to, "gossipy_man", 1)
        t.exec("gossip.drain_to_options", t.chat.drain, { stop_at = "options" })
        t.exec("gossip.choose", t.chat.choose, "Who do you think was responsible?")
        t.exec("gossip.close", t.chat.drain, { stop_at = "none" })
        -- The gate stands open behind the player and gossipy_man wanders:
        -- talk_to followed him back into the grounds (murder_b2/b4 runs:
        -- the pub goto left from 2741,3558). Walk back out through the gate
        -- before the goto whenever the talk ended inside.
        local gr, gt = t.world.tile()
        if gr == "ok" and in_grounds(gt) then
            grounds_out("pub")
        end
        walk_check(t, "road.beforePub", 2741, 3552, on_road, "on the road south of the gate, z <= 3555")

        -- --------------------------------------------------- poison salesman
        -- The Seers' pub's front double door stands open in the map: the
        -- goto lands on the street outside, the open leaf is asserted, and
        -- the player walks in.
        t.exec("goto.pub", t.player.goto_tile, 2693, 3484, 0)
        t.ticks(2)
        local lr, ld = t.world.loc_near("kr_opendoubledoor_l", 8)
        t.check("pub.doorStandsOpen",
            lr == "ok" and ld.tile_x == 2694 and ld.tile_z == 3488 and ld.level == 0,
            "kr_opendoubledoor_l -> " .. (lr == "ok" and (ld.tile_x .. "," .. ld.tile_z .. "," .. ld.level) or tostring(lr))
                .. " (want the map's open leaf at 2694,3488,0: the doorway is open, walked through)")
        walk_check(t, "pub.in", 2693, 3490, function(tt) return tt.z >= 3488 end, "inside the pub, z >= 3488")
        t.exec("salesman.talk", t.player.talk_to, "poison_salesman", 1)
        t.exec("salesman.drain_to_options", t.chat.drain, { stop_at = "options" })
        t.exec("salesman.choose", t.chat.choose, "Who did you sell Poison to at the house?")
        t.exec("salesman.close", t.chat.drain, { stop_at = "none" })
        -- The fourth option only shows with murderpot2 in the pack
        -- (poison_salesman.rs2's inv_total(inv, murderpot2) > 0 arm).
        t.exec("salesman.pot.talk", t.player.talk_to, "poison_salesman", 1)
        t.exec("salesman.pot.drain_to_options", t.chat.drain, { stop_at = "options" })
        t.exec("salesman.pot.choose", t.chat.choose, "I have this pot I found at the murder scene...")
        t.exec("salesman.pot.close", t.chat.drain, { stop_at = "none" })
        walk_check(t, "pub.out", 2693, 3485, function(tt) return tt.z <= 3487 end, "on the street, z <= 3487")

        -- --------------------------------------------------- poison proof
        -- Only the matched suspect: their option-4 arm moves
        -- %murder_poisonproof_progress from spoken_salesman to
        -- spoken_murderer, and only then does their own loc's murderer arm
        -- write searched_loc and show the proof line.
        local s = matched
        grounds_in("poison")
        if s.talk_in_room then
            mansion_in("poison")
            room_in(s, "poison")
        end
        t.exec("suspect.talk." .. s.name, t.player.talk_to, s.npc, 1)
        t.exec("suspect.drain_to_options." .. s.name, t.chat.drain, { stop_at = "options" })
        t.exec("suspect.choose_poison." .. s.name, t.chat.choose, "Why'd you buy poison the other day?")
        t.exec("suspect.close." .. s.name, t.chat.drain, { stop_at = "none" })
        if s.talk_in_room then
            room_out(s, "poison")
            mansion_out("poison")
        end
        t.exec("poisonloc.search." .. s.name, t.player.click_loc, s.poison_loc, 2)
        local pr, pd = t.chat.expect_text(s.proof)
        t.expect("poisonloc.proof." .. s.name, pr, "chat.expect_text(" .. s.proof .. ") -> " .. txt(pd))
        t.exec("poisonloc.close." .. s.name, t.chat.drain, { stop_at = "none" })

        -- --------------------------------------------------- hand in
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        local coins_before_read, coins_before = t.inv.count("coins")
        -- Walk up to the gate guard first: from the compost, the nearest
        -- murderguard is the house guard above the hall (m42_55.spawn:41,
        -- 2739,3579,1), and talk_to answered "I can't reach that!" on it
        -- (murder_b2 run, 2026-10-04).
        walk_check(t, "guard.walk", 2741, 3559, in_grounds, "in the grounds beside the gate guard 2741,3562")
        t.exec("guard.talk", t.player.talk_to, "murderguard", 1)
        t.exec("guard.drain_to_options", t.chat.drain, { stop_at = "options" })
        -- Thread, prints and searched_loc all true -> murderguard_who's
        -- first branch, murderguard_conclusive_proof, the only branch that
        -- queues murder_quest_complete.
        t.exec("guard.accuse", t.chat.choose, "I know who did it!")
        t.exec("guard.conclusive_proof", t.chat.drain, { stop_at = "none" })

        -- Completion is asynchronous behind queue(murder_quest_complete,0,0).
        t.ticks(3)
        t.quest.expect_complete()

        -- The literal reward of murder_guard.rs2 [queue,murder_quest_complete]:
        -- `stat_advance(crafting, 14062)` -> 1406 Crafting XP,
        -- `inv_add(inv, coins, 2000)`.
        t.note("skill.snapshot before the hand-in -> " .. tostring(xp_snapshot_result))
        t.expect("reward.crafting_xp", t.skill.expect_gain("crafting", 1406, xp_snapshot))
        local coins_after_read, coins_after = t.inv.count("coins")
        local coins_ok = coins_after_read == "ok" and coins_before_read == "ok"
        t.check("reward.coins",
            coins_ok and (coins_after - coins_before) == 2000,
            "coins before=" .. tostring(coins_before) .. " after=" .. tostring(coins_after)
                .. " delta=" .. tostring(coins_ok and (coins_after - coins_before) or "n/a"))

        t.finish(0)
    end,
}
