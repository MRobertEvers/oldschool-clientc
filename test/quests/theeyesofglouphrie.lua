-- The Eyes of Glouphrie: Quest Helper guide steps talkToBrimstail .. talkToNarnode (23 steps, 1 leg).
-- Port is wiki + Quest Helper (no LostCity). Puzzle: the discs are earned from Brimstail and the
-- exchanger (interface 111), the unlock window (145) and operate window (189) are driven by widget.
-- Notes: docs/quests/ladders/theeyesofglouphrie.notes.md
local COLOURS = { "red", "orange", "yellow", "green", "blue", "indigo", "violet" }
local SHAPES = { "circle", "triangle", "square", "pentagon" }
local SIDES = { 1, 3, 4, 5 }
local VALUE = {}
for ci, c in ipairs(COLOURS) do
    for si, s in ipairs(SHAPES) do
        VALUE["eyeglo_" .. c .. "_" .. s] = ci * SIDES[si]
    end
end

-- every disc in the backpack, one entry per disc (stacks expanded)
local function held_discs(t)
    local flat, slots = {}, {}
    for slot = 0, 27 do
        local r, cell = t.inv.slot(slot)
        if r == "ok" and cell.name ~= "" and cell.count ~= 0 and VALUE[cell.name] then
            for _ = 1, cell.count do
                flat[#flat + 1] = { sym = cell.name, v = VALUE[cell.name] }
            end
            slots[cell.name] = slot
        end
    end
    return flat, slots
end

local function sum_of(list)
    local s = 0
    for _, d in ipairs(list) do s = s + d.v end
    return s
end

-- split `flat` into disjoint groups with the exact sums `sums` (extras left over);
-- returns groups (lists of discs) and the count mismatch against `wants`
-- greedy plan: lock every group whose exact sum is already held in exactly the wanted
-- disc count; for an unlocked group note a subset with the right sum and the wrong count
-- (reshape) so the exchanger can be pointed at it. Returns locked[g] (disc list or nil),
-- reshape (disc list, wanted count smaller?) and the leftover discs.
local function plan(flat, sums, wants)
    -- b70: search over DISTINCT disc kinds with their counts (the hand can hold dozens of duplicates after many
    -- exchanges; a search over every disc hit its node cap and never saw a solution)
    local n, k = #flat, #sums
    local kinds, kind_of = {}, {}
    for i = 1, n do
        local sym = flat[i].sym
        if not kind_of[sym] then
            kinds[#kinds + 1] = { v = flat[i].v, discs = {}, taken = 0 }
            kind_of[sym] = #kinds
        end
        local kd = kinds[kind_of[sym]]
        kd.discs[#kd.discs + 1] = flat[i]
    end
    local locked, reshape, reshape_big = {}, nil, false
    local nodes = 0
    local order = {}
    for g = k, 1, -1 do order[#order + 1] = g end
    for _, g in ipairs(order) do
        local sizes = { wants[g] }
        for size = 1, 4 do if size ~= wants[g] then sizes[#sizes + 1] = size end end
        for _, size in ipairs(sizes) do
            if locked[g] == nil then
                local members = {}
                local function comb(start, count, sum)
                    nodes = nodes + 1
                    if nodes > 40000 or locked[g] ~= nil then return end
                    if count == size then
                        if sum == sums[g] then
                            local picked, seen = {}, {}
                            for i2 = 1, size do
                                local ki = members[i2]
                                seen[ki] = (seen[ki] or 0) + 1
                                picked[i2] = kinds[ki].discs[kinds[ki].taken + seen[ki]]
                            end
                            if size == wants[g] then
                                locked[g] = picked
                                for ki, c in pairs(seen) do kinds[ki].taken = kinds[ki].taken + c end
                            elseif reshape == nil then
                                reshape = picked
                                reshape_big = size > wants[g]
                            end
                        end
                        return
                    end
                    for ki = start, #kinds do
                        local kd = kinds[ki]
                        local already = 0
                        for i2 = 1, count do if members[i2] == ki then already = already + 1 end end
                        if kd.taken + already < #kd.discs and sum + kd.v <= sums[g] then
                            members[count + 1] = ki
                            comb(ki, count + 1, sum + kd.v)
                        end
                    end
                end
                comb(1, 0, 0)
            end
        end
    end
    local leftover = {}
    for _, kd in ipairs(kinds) do
        for j = kd.taken + 1, #kd.discs do leftover[#leftover + 1] = kd.discs[j] end
    end
    return locked, reshape, reshape_big, leftover
end

-- up to three discs worth at most 35 points, chosen at random from `list`
local function pick_exchange_subset(list, big)
    -- b70: bounded pick (was an O(n^3) enumeration that exhausted the 400000-instruction budget on long lists).
    -- Same rules: 1..3 discs whose values sum <= 35; big = the largest such size, else a single disc.
    local order = {}
    for i = 1, #list do order[#order + 1] = i end
    table.sort(order, function(x, y) return list[x].v < list[y].v end)
    local max_size, acc = 0, 0
    for k = 1, math.min(3, #order) do
        acc = acc + list[order[k]].v
        if acc <= 35 then max_size = k end
    end
    if max_size == 0 then return nil end
    local size = big and max_size or 1
    local pick
    if size == 1 then
        local singles = {}
        for i = 1, #list do
            if list[i].v <= 35 then singles[#singles + 1] = i end
        end
        pick = { singles[math.random(#singles)] }
    else
        for _ = 1, 40 do
            local chosen, used, sum = {}, {}, 0
            while #chosen < size do
                local i = math.random(#list)
                if not used[i] then used[i] = true; chosen[#chosen + 1] = i; sum = sum + list[i].v end
            end
            if sum <= 35 then pick = chosen; break end
        end
        if not pick then
            pick = {}
            for k = 1, size do pick[k] = order[k] end
        end
    end
    local out = {}
    for _, i in ipairs(pick) do out[#out + 1] = list[i] end
    return out
end

-- b70: choose 1..3 discs (sum <= 35, the exchanger's cap) from `list` whose total is closest to `target`
-- (at or just above it preferred): the exchanger re-splits what goes in at random, so feeding it the missing
-- group's own total is the only way to make that group appear; random tie-break so a repeat tries another set.
local function pick_toward(list, target)
    local kinds = {}
    local by_sym = {}
    for i = 1, #list do
        local sym = list[i].sym
        if not by_sym[sym] then
            kinds[#kinds + 1] = { v = list[i].v, discs = {} }
            by_sym[sym] = #kinds
        end
        local kd = kinds[by_sym[sym]]
        kd.discs[#kd.discs + 1] = list[i]
    end
    local goal = math.min(target, 35)
    local best, best_score, ties = nil, nil, 0
    local function consider(ia, ib, ic)
        local sum = kinds[ia].v + (ib and kinds[ib].v or 0) + (ic and kinds[ic].v or 0)
        if sum > 35 then return end
        local counts, need = {}, { ia, ib, ic }
        for _, ki in ipairs(need) do
            counts[ki] = (counts[ki] or 0) + 1
            if counts[ki] > #kinds[ki].discs then return end
        end
        local score = (sum >= goal) and (sum - goal) or (100 + goal - sum)
        if best_score == nil or score < best_score then
            best_score, best, ties = score, { ia, ib, ic }, 1
        elseif score == best_score then
            ties = ties + 1
            if math.random(ties) == 1 then best = { ia, ib, ic } end
        end
    end
    for a = 1, #kinds do
        consider(a)
        for b = a, #kinds do
            consider(a, b)
            for c = b, #kinds do consider(a, b, c) end
        end
    end
    if not best then return nil end
    local out, taken = {}, {}
    for _, ki in ipairs(best) do
        taken[ki] = (taken[ki] or 0) + 1
        out[#out + 1] = kinds[ki].discs[taken[ki]]
    end
    return out
end

local function widget_wait(t, sym, sub)
    for _ = 1, 12 do
        local r, w = t.ui.widget(sym, sub)
        if r == "ok" and w then return w end
        t.ticks(1)
    end
    return nil
end

local function press(t, sym, sub)
    local w = widget_wait(t, sym, sub)
    if not w then t.step("widget " .. sym, "FAIL", "never mounted"); return false end
    t.ui.invoke(w, 1)
    t.ticks(2)
    return true
end

-- The side panel's slot click TOGGLES the disc in hand (eyeglo_puzzle.rs2 [if_button1,eyeglo_side:inv_layer]:
-- a press on the disc already held puts it down), and an insert keeps the disc in hand while another copy
-- is carried (eyeglo_operate_insert / exchange_coinslot clear varp856 only when the last copy goes in).
-- So a second copy of the disc just inserted is already in hand: pressing it again would put it down and
-- the next coinslot press would hand the row's last disc BACK (b69: row 3 read 9 for 59). Track the hand.
local HAND = { sym = nil }

local function hand_reset() HAND.sym = nil end

local function hand_after_insert(t)
    local _, cur = t.var.server("varp856_eyeglo_coin_selected")
    if cur == nil or cur < 0 then HAND.sym = nil end
end

local function select_disc(t, sym)
    local _, slots = held_discs(t)
    if slots[sym] == nil then
        t.step("select " .. sym, "FAIL", "not held")
        return false
    end
    if HAND.sym == sym then
        local _, cur = t.var.server("varp856_eyeglo_coin_selected")
        if cur ~= nil and cur >= 0 then return true end
    end
    local ok = press(t, "eyeglo_side:inv_layer", slots[sym])
    HAND.sym = sym
    return ok
end

-- Door rule (owner 2026-10-03): every closed space is crossed on foot, in and out, on every visit.
--   * Lumbridge -> Kandarin: only through a members' gate on foot, so a real Camelot Teleport first
--     (magic_spell_teleport_camelot: 45 Magic, 5 air + 1 law), then the overland hop Camelot -> south of
--     the Stronghold gate (reach.py 2757,3478 -> 2461,3379 closed-doors len 411).
--   * The Stronghold: gnome_areagate 2459,3383 is the only way in on foot (grandtree.lua's note);
--     cross_gate on every trip, Femi's boxes on a first press from the south (gnome_gate.rs2:31-32).
--   * Brimstail's cave: in by its entrance (eyeglo_quest.rs2:22 p_teleport 0_37_153_41_20 = 2409,9812, the tunnel mouth), out by the
--     tunnel (maplink.dbrow maplink_0_37_153_41_20 -> 0_37_53_34_27); the cave is one open space.
--   * Hazelmere's hut: elfdoor 2677,3088 by pass_door, his ladder by climb (grandtree.lua's route).
--   * The evergreen 2360,3527 is OUTSIDE the Stronghold (reach.py 2461,3386 -> 2360,3527 UNREACHABLE;
--     2461,3380 -> REACH len 250): the sap is cut on the way back from Hazelmere.
--   * The Grand Tree: treedoorl 2464,3492 by cross_gate on every visit, its ladders by climb.

local TREE_LADDERS_UP = {
    { "climbUpToF1Tree", "grandtree_ladderbottom", 1, 0 },
    { "climbUpToF2Tree", "grandtree_laddermiddle_bottom", 2, 1 },
    { "climbUpToF3Tree", "grandtree_laddermiddle_top", 2, 2 },
}
local TREE_LADDERS_DOWN = {
    { "climbDownToF2Tree", "grandtree_laddertop", 1, 3 },
    { "climbDownToF1Tree", "grandtree_laddermiddle_top", 3, 2 },
    { "climbDownToF0Tree", "grandtree_laddermiddle_bottom", 3, 1 },
}

return {
    id = "theeyesofglouphrie",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::eyesofglouphrie",
        "::complete quest_grandtree",
        "::setlevel magic 46",
        "::setlevel construction 5",
        "::setlevel hitpoints 40",
        "::give lobster 8",
        "::give bucket_empty 1",
        "::give knife 1",
        "::give mudrune 1",
        "::give pestle_and_mortar 1",
        "::give oak_logs 1",
        "::give maple_logs 1",
        "::give poh_saw 1",
        "::give hammer 1",
        "::give bronze_sword 1",
        -- one Camelot Teleport (Lumbridge -> Kandarin past the members' wall)
        "::give airrune 5",
        "::give lawrune 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb2497_eyeglo_quest",
            constants = {
                not_started = 0, started = 1, told = 2, listened = 5, heard = 15, sabotaged = 20,
                discussed = 21, inspected = 22, glue_hint = 23, repaired = 25, discs_given = 27,
                more_discs = 30, ready = 35, operated = 36, hunting = 40, all_dead = 45, finished = 50,
                complete = 60,
            },
            display = "The Eyes of Glouphrie",
            points = 2,
        })
        t.ticks(3)
        local function sv(n) local _, v = t.var.server(n); return v end
        local function tile_text(r, tl)
            if r ~= "ok" or type(tl) ~= "table" then return tostring(r) end
            return tostring(tl.x) .. "," .. tostring(tl.z) .. "," .. tostring(tl.level)
        end
        local function talk(name, option, max)
            t.exec(name .. ".talk", t.player.talk_to, "gnome_brimstail", 1)
            t.exec(name .. ".menu", t.chat.drain, { stop_at = "options" })
            t.exec(name .. ".pick", t.chat.choose, option)
            t.exec(name .. ".tail", t.chat.drain, { max_pages = max or 100 })
        end

        -- ---- travel helpers ----
        local function gate_in(name)
            t.exec(name, t.player.cross_gate, { loc = "gnome_areagate", at = { 2459, 3383, 0 }, near = { 2461, 3382 },
                far_ok = function(tile) return tile.z >= 3385 end, far_desc = "inside the Stronghold, z >= 3385" })
        end
        local function gate_out(name)
            t.exec(name, t.player.cross_gate, { loc = "gnome_areagate", at = { 2459, 3383, 0 }, near = { 2461, 3385 },
                far_ok = function(tile) return tile.z <= 3382 end, far_desc = "south of the Stronghold gate, z <= 3382" })
        end
        -- from south of the gate: while %varp5856_femi_help = 0 a press with Femi within 6 tiles opens her
        -- boxes instead of the gate (gnome_gate.rs2:31-32 -> femi.rs2 @grandtree_femi_boxes); helping sets 2.
        local function enter_stronghold(pfx)
            t.exec("goto-" .. pfx .. ".gate", t.player.goto_tile, 2461, 3379, 0)
            t.exec(pfx .. ".gateApproach", t.player.walk_route, { { 2461, 3382 } })
            if sv("varp5856_femi_help") ~= 0 then
                gate_in(pfx .. ".gateIn")
                return
            end
            local press_result, press_detail = t.player.click_loc("gnome_areagate", 1, { at = { 2459, 3383 } })
            t.await({ level = function()
                if t.chat.kind() ~= "none" then return true end
                local r, tl = t.world.tile()
                return r == "ok" and tl.z >= 3385
            end, note = "Femi's boxes page or the gate's walk-through" }, 12)
            if t.chat.kind() ~= "none" then
                t.exec(pfx .. ".femiBoxes-dialog", t.chat.play, {
                    "npc:Hello there", "player:Hi!", "npc:Could you help me lift", "options", "choose:OK then.",
                    "player:OK then", "npc:Thanks traveller" })
                t.expect(pfx .. ".femiBoxes.thanks_page", t.await({ level = function() return t.chat.kind() == "npc" end,
                    note = "Femi's thanks after the boxes" }, 20))
                t.exec(pfx .. ".femiBoxes-dialog-2", t.chat.play, { "npc:Thanks again friend", "end" })
                local fr, fh = t.var.server("varp5856_femi_help")
                t.check(pfx .. ".femiHelped", fr == "ok" and fh == 2,
                    "click_loc gnome_areagate -> " .. tostring(press_result) .. "; varp5856_femi_help = " .. tostring(fh)
                        .. " (" .. tostring(fr) .. "), want 2: the boxes lifted (femi.rs2 @grandtree_femi_boxes)")
                gate_in(pfx .. ".gateIn")
            else
                local r, tl = t.world.tile()
                t.check(pfx .. ".gateIn", r == "ok" and tl.level == 0 and tl.z >= 3385,
                    "click_loc gnome_areagate -> " .. tostring(press_result) .. " " .. tostring(press_detail)
                        .. "; no Femi page; after the press " .. tile_text(r, tl) .. " (want through the gate from 2461,3382: z >= 3385)")
            end
        end
        local function leave_stronghold(pfx)
            t.exec("goto-" .. pfx .. ".gate", t.player.goto_tile, 2461, 3386, 0)
            gate_out(pfx .. ".gateOut")
        end
        -- the cave entrance's own trigger lands inside on 2409,9812, the tunnel mouth (eyeglo_quest.rs2:22-31, ^eyeglo_cave_inside_coord;
        -- maplink_0_37_53_* dest 0_37_153_41_20 and transports.tsv agree)
        local function cave_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2405, 3419, 0)
            t.exec(name, t.player.climb, { loc = "eyeglo_brimstails_cave_entrance", op = 1, op_name = "Enter",
                at = { 2403, 3418, 0 }, dest = { 2409, 9812, 0 }, slack = 1 })
            t.ticks(3)
        end
        -- the tunnel out: maplink_0_37_153_41_20 keys on the player's tile 2409,9812 -> 2402,3419
        local function cave_out(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2409, 9813, 0)
            t.exec(name, t.player.climb, { loc = "eyeglo_dungeon_exit_left", op = 1, op_name = "Exit",
                at = { 2409, 9811, 0 }, src = { 2409, 9812 }, dest = { 2402, 3419, 0 }, slack = 2 })
            t.ticks(3)
        end
        local function tree_door_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2465, 3489, 0)
            t.exec(name, t.player.cross_gate, { loc = "treedoorl", at = { 2464, 3492, 0 }, near = { 2465, 3491 },
                far_ok = function(tile) return tile.z >= 3493 and tile.z <= 3498 and tile.x >= 2463 and tile.x <= 2468 end,
                far_desc = "inside the Grand Tree's ground floor, z 3493..3498" })
        end
        local function tree_door_out(name)
            t.exec(name, t.player.cross_gate, { loc = "treedoorl", at = { 2464, 3492, 0 }, near = { 2465, 3493 },
                far_ok = function(tile) return tile.z <= 3491 end, far_desc = "outside the Grand Tree, z <= 3491" })
        end
        local function goto_brimstail(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2410, 9818, 0)
        end
        local function goto_machine(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2391, 9824, 0)
        end

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ===== to the Stronghold, talkToBrimstail / enterCave (guide 1.1, 1.2)
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "toStronghold.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        enter_stronghold("toStronghold")
        cave_in("enterCave")
        goto_brimstail("talkToBrimstail")
        t.exec("talkToBrimstail", t.player.talk_to, "gnome_brimstail", 1)
        t.exec("brim.menu", t.chat.drain, { stop_at = "options" })
        t.exec("brim.pick", t.chat.choose, "What's that cute creature wandering around?")
        t.exec("brim.q1", t.chat.drain, { stop_at = "options" })
        t.exec("brim.fascinating", t.chat.choose, "Yes, that sounds fascinating...")
        t.exec("brim.q2", t.chat.drain, { stop_at = "options" })
        t.exec("brim.history", t.chat.choose, "Oh, yes I love a bit of History.")
        t.exec("brim.q3", t.chat.drain, { stop_at = "options" })
        t.exec("brim.start", t.chat.choose, "Yes.")
        t.exec("brim.tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ===== inspectBowl, inspectMachine (1.3, 1.4): the bowl sits in the rift; 2391,9813 is its open edge
        t.exec("goto-inspectBowl", t.player.goto_tile, 2391, 9813, 0)
        t.exec("inspectBowl", t.player.click_loc, "eyeglo_singing_bowl", 1)
        t.ticks(2)
        t.check("inspectBowl.seen", sv("varb2515_eyeglo_bowl_seen") == 1, "varb2515_eyeglo_bowl_seen=" .. tostring(sv("varb2515_eyeglo_bowl_seen")))
        goto_machine("inspectPanel")
        t.exec("inspectPanel", t.player.click_loc, "eyeglo_machine_clue_panel", 1)
        t.expect("inspectPanel.open", t.ui.await_open("eyeglo_clue_panel", 10))
        t.key("escape")
        t.ticks(2)
        t.exec("inspectMachine", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.expect("inspectMachine.open", t.ui.await_open("eyeglo_gnome_machine_locked", 10))
        local unlock_btn = widget_wait(t, "eyeglo_gnome_machine_locked:unlock_button")
        t.check("inspectMachine.button", unlock_btn ~= nil, "unlock button widget " .. tostring(unlock_btn))
        t.ui.invoke(unlock_btn, 1)
        t.ticks(2)
        t.expect("inspectMachine.refused", t.msg.expect("You have not unlocked the machine yet"))
        t.key("escape")
        t.ticks(2)
        t.check("inspectMachine.seen", sv("varb2516_eyeglo_machine_seen") == 1, "varb2516_eyeglo_machine_seen=" .. tostring(sv("varb2516_eyeglo_machine_seen")))

        -- ===== talkToBrimstailAgain (1.5)
        goto_brimstail("talkToBrimstailAgain")
        t.exec("talkToBrimstailAgain", t.player.talk_to, "gnome_brimstail", 1)
        t.exec("brim2.menu", t.chat.drain, { stop_at = "options" })
        t.exec("brim2.pick", t.chat.choose, "I've had a look in the other room now.")
        t.exec("brim2.q", t.chat.drain, { stop_at = "options" })
        t.exec("brim2.go", t.chat.choose, "Of course, I'd love to!")
        t.exec("brim2.tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.told", t.quest.expect_stage("told"))

        -- ===== goUpToHazelmere, talkToHazelmere (1.6, 1.7): needs unboosted 46 magic.
        -- Out of the cave and the Stronghold on foot, overland (reach.py 2461,3380 -> 2677,3090) to his door.
        cave_out("toHazelmere.caveExit")
        leave_stronghold("toHazelmere")
        t.exec("goto-toHazelmere.door", t.player.goto_tile, 2677, 3090, 0)
        t.exec("toHazelmere.doorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2677, 3088, 0 }, near = { 2677, 3089 }, far = { 2677, 3088 } })
        t.exec("goUpToHazelmere", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
            at = { 2677, 3087, 0 }, src = { 2677, 3086 }, dest = { 2677, 3086, 1 } })
        t.ticks(2)
        t.exec("talkToHazelmere", t.player.talk_to, "grandtree_hazelmere", 1)
        t.exec("hazelmere.story", t.chat.drain, { max_pages = 250 })
        t.ticks(2)
        t.expect("quest.stage.heard", t.quest.expect_stage("heard"))
        t.expect("hazelmere.disc", t.inv.expect_has("eyeglo_violet_pentagon", 1))
        t.exec("leaveHazelmere.ladderDown", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2677, 3087, 1 }, src = { 2677, 3086 }, dest = { 2677, 3086, 0 } })
        t.exec("leaveHazelmere.doorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2677, 3088, 0 }, near = { 2677, 3088 }, far = { 2677, 3090 } })

        -- ===== the sap (an item the guide lists for 1.8): knife on the nearest evergreen, outside the Stronghold
        -- (the 239 cache places none near the cave; sap is ungated, icthlarin_embalm.rs2:142)
        t.exec("goto-gatherSap", t.player.goto_tile, 2360, 3527, 0)
        local near_ok = t.world.loc_near("evergreen", 12)
        local tree_sym = near_ok == "ok" and "evergreen" or "evergreen_large"
        local tree = t.player.by_symbol("loc", tree_sym)
        local ur, ud = t.player.use_on("knife", tree)
        t.check("gatherSap.knifeOnEvergreen", ur == "ok", tostring(ur) .. " " .. tostring(ud) .. " (tree " .. tree_sym .. ")")
        t.ticks(2)
        t.expect("sap", t.inv.expect_has("ics_little_sap_bucket", 1))

        -- ===== enterCaveAgain, talkToBrimstailAfterHazelmere (1.8, 1.9)
        enter_stronghold("toCaveAgain")
        cave_in("enterCaveAgain")
        goto_brimstail("talkToBrimstailAfterHazelmere")
        talk("talkToBrimstailAfterHazelmere", "I've visited Hazelmere, he told me all sorts of interesting things.")
        t.ticks(2)
        t.expect("quest.stage.sabotaged", t.quest.expect_stage("sabotaged"))
        t.check("sabotaged.broken", sv("varb2502_eyeglo_machine_broken") == 1, "varb2502_eyeglo_machine_broken=" .. tostring(sv("varb2502_eyeglo_machine_broken")))
        talk("sabotage", "The machine is broken, I suspect sabotage...!")
        t.ticks(2)
        t.expect("quest.stage.discussed", t.quest.expect_stage("discussed"))
        goto_machine("inspectBrokenMachine")
        t.exec("inspectBrokenMachine", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.exec("inspectBrokenMachine.tail", t.chat.drain, { max_pages = 20 })
        t.ticks(2)
        t.expect("quest.stage.inspected", t.quest.expect_stage("inspected"))
        t.exec("inspectMissing", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.ticks(2)
        t.expect("inspectMissing.msg", t.msg.expect("You need something to stick all the bits together."))
        goto_brimstail("glueHint")
        talk("glueHint", "I'm still a bit confused about how to fix Oaknock's machine.")
        t.ticks(2)
        t.expect("quest.stage.glue_hint", t.quest.expect_stage("glue_hint"))

        -- ===== grindMudRunes, useMudOnSap (1.10, 1.11)
        local gr, gd = t.player.use_item_on_item("pestle_and_mortar", "mudrune")
        t.check("grindMudRunes", gr == "ok", tostring(gr) .. " " .. tostring(gd))
        t.expect("ground", t.inv.expect_has("eyeglo_ground_mud_runes", 1))
        local mr, md = t.player.use_item_on_item("eyeglo_ground_mud_runes", "ics_little_sap_bucket")
        t.check("useMudOnSap", mr == "ok", tostring(mr) .. " " .. tostring(md))
        t.expect("glue", t.inv.expect_has("eyeglo_magic_glue", 1))

        -- ===== repairMachine (1.12)
        goto_machine("repairMachine")
        local machine = t.player.by_symbol("loc", "eyeglo_gnome_machine_02_multiloc")
        local rr, rd = t.player.use_on("eyeglo_magic_glue", machine)
        t.check("repairMachine", rr == "ok", tostring(rr) .. " " .. tostring(rd))
        t.ticks(6)
        t.expect("quest.stage.repaired", t.quest.expect_stage("repaired"))
        t.expect("glue gone", t.inv.expect_absent("eyeglo_magic_glue"))
        t.expect("oak gone", t.inv.expect_absent("oak_logs"))
        t.expect("maple gone", t.inv.expect_absent("maple_logs"))

        -- ===== talkToBrimstailAfterRepairing, talkToBrimstailForMoreDisks (1.13, 1.14)
        goto_brimstail("talkToBrimstailAfterRepairing")
        talk("talkToBrimstailAfterRepairing", "I think I've fixed the machine now!")
        t.ticks(2)
        t.expect("quest.stage.discs_given", t.quest.expect_stage("discs_given"))
        t.expect("red square", t.inv.expect_has("eyeglo_red_square", 1))
        t.expect("yellow triangle", t.inv.expect_has("eyeglo_yellow_triangle", 1))
        talk("talkToBrimstailForMoreDisks", "I can't work out what to do with these discs!")
        t.ticks(2)
        t.expect("quest.stage.more_discs", t.quest.expect_stage("more_discs"))

        -- ===== unlockMachine (1.15): open the window to read this player's target, then earn the disc
        goto_machine("unlockMachine")
        t.exec("unlockMachine.click", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.expect("unlockMachine.open", t.ui.await_open("eyeglo_gnome_machine_locked", 10))
        t.ticks(3)
        local cv1 = sv("varb2510_eyeglo_coin_value_1")
        local cv2 = sv("varb2511_eyeglo_coin_value_2")
        local cv3 = sv("varb2512_eyeglo_coin_value_3")
        local cv4 = sv("varb2513_eyeglo_coin_value_4")
        t.check("unlockMachine.targets", cv1 > 0 and cv2 > 0 and cv3 > 0 and cv4 > 0, "targets " .. cv1 .. "," .. cv2 .. "," .. cv3 .. "," .. cv4)
        t.key("escape")
        t.ticks(2)

        -- earn the unlock disc (value cv1) and then the six operate discs (cv2 x1, cv3 x2, cv4 x3)
        local sums = { cv1, cv2, cv3, cv4 }
        local wants = { 1, 1, 2, 3 }
        local found = nil
        local asks, exchanges, dead_asks = 0, 0, 0
        local need_total = cv1 + cv2 + cv3 + cv4
        local function signature(list)
            local counts, keys = {}, {}
            for _, d in ipairs(list) do
                if not counts[d.sym] then counts[d.sym] = 0; keys[#keys + 1] = d.sym end
                counts[d.sym] = counts[d.sym] + 1
            end
            table.sort(keys)
            local parts = {}
            for _, k2 in ipairs(keys) do parts[#parts + 1] = k2 .. "x" .. counts[k2] end
            return table.concat(parts, ",")
        end
        -- Every round does ONE action and then checks its own effect:
        --   exchange: points preserved, 1..4 discs came back, and the hand changed (unchanged 3 times running = stuck)
        --   ask:      exactly three more discs held (Brimstail gives three; he gives none to a hand of 6 or more)
        --   drop:     the dropped disc left the pack
        -- A hand short of points can only be topped up by Brimstail, who gives nothing while 6+ discs are carried
        -- (eyeglo_quest.rs2:386): drop the lowest-value discs down to 5, then ask. Bounded by short_cycles.
        local unchanged, short_cycles = 0, 0
        for iter = 1, 300 do
            local flat = held_discs(t)
            local locked, reshape, reshape_big, leftover = plan(flat, sums, wants)
            local nlocked = 0
            for g = 1, 4 do if locked[g] ~= nil then nlocked = nlocked + 1 end end
            t.note("earn round " .. iter .. ": " .. #flat .. " discs worth " .. sum_of(flat) .. " (need " .. need_total .. "), groups locked " .. nlocked .. "/4, leftover " .. #leftover .. " worth " .. sum_of(leftover))
            if nlocked == 4 then
                found = { locked[1], locked[2], locked[3], locked[4] }
                break
            end
            local short = sum_of(flat) < need_total
            if short and #flat >= 6 then
                -- drop the lowest-value discs until 5 remain, then the next round asks
                short_cycles = short_cycles + 1
                if short_cycles > 25 then
                    t.check("earnDiscs.progress", false, "round " .. iter .. ": still short after 25 drop-and-ask cycles: " .. #flat .. " discs worth " .. sum_of(flat) .. " (need " .. need_total .. ")")
                    break
                end
                -- a drop puts the whole stack of that disc kind on the ground
                local cur = flat
                for _ = 1, 30 do
                    if #cur <= 5 then break end
                    local low = cur[1]
                    for _, d in ipairs(cur) do if d.v < low.v then low = d end end
                    local stack = 0
                    for _, d in ipairs(cur) do if d.sym == low.sym then stack = stack + 1 end end
                    t.exec("drop " .. low.sym, t.player.drop, low.sym)
                    t.ticks(1)
                    local nxt = held_discs(t)
                    t.check("drop.left", #nxt == #cur - stack, "dropped " .. stack .. " x " .. low.sym .. ": held " .. #cur .. " -> " .. #nxt)
                    cur = nxt
                end
            elseif short then
                local before = flat
                t.expect("moreDiscs.goto", t.player.goto_tile(2410, 9818, 0))
                t.expect("moreDiscs.talk", t.player.talk_to("gnome_brimstail", 1))
                t.expect("moreDiscs.menu", t.chat.drain({ stop_at = "options" }))
                t.expect("moreDiscs.pick", t.chat.choose("I can't work out what to do with these discs!"))
                t.expect("moreDiscs.tail", t.chat.drain({ max_pages = 40 }))
                asks = asks + 1
                local after = held_discs(t)
                t.check("moreDiscs.gave", #after == #before + 3, "Brimstail's three discs: held " .. #before .. " -> " .. #after .. ", worth " .. sum_of(before) .. " -> " .. sum_of(after))
            else
                local subset = nil
                if reshape then subset = pick_exchange_subset(reshape, reshape_big) end
                if not subset then
                    local pool = leftover
                    if #pool == 0 then pool = flat end
                    local missing = 0
                    for g2 = 1, 4 do
                        if locked[g2] == nil and sums[g2] > missing then missing = sums[g2] end
                    end
                    -- aim the exchanger at the largest group still unlocked
                    subset = pick_toward(pool, missing) or pick_toward(flat, missing)
                end
                if not subset then
                    t.check("earnDiscs.progress", false, "round " .. iter .. ": no held disc is worth <= 35 (the exchanger's cap): " .. #flat .. " discs worth " .. sum_of(flat) .. ", locked " .. nlocked .. "/4")
                    break
                end
                local inserted = sum_of(subset)
                local before_sig = signature(flat)
                t.expect("exchange.goto", t.player.goto_tile(2391, 9824, 0))
                t.expect("exchange.click", t.player.click_loc("eyeglo_change_machine_multiloc", 1))
                t.expect("exchange.open", t.ui.await_open("eyeglo_change_machine", 10))
                hand_reset()
                t.ticks(2)
                for _, d in ipairs(subset) do
                    select_disc(t, d.sym)
                    press(t, "eyeglo_change_machine:exchange_coinslot")
                    hand_after_insert(t)
                end
                press(t, "eyeglo_change_machine:exchange_button")
                press(t, "eyeglo_change_machine:take_button")
                t.key("escape")
                t.ticks(2)
                exchanges = exchanges + 1
                local after = held_discs(t)
                local returned = #after - (#flat - #subset)
                t.check("exchange.points", sum_of(after) == sum_of(flat), "points held " .. sum_of(flat) .. " -> " .. sum_of(after) .. " after inserting " .. #subset .. " discs worth " .. inserted)
                t.check("exchange.returned", returned >= 1 and returned <= 4, "returned " .. returned .. " discs for " .. #subset .. " inserted (the offer holds 1..4)")
                if signature(after) == before_sig then
                    unchanged = unchanged + 1
                    if unchanged >= 3 then
                        t.check("exchange.changed", false, "the hand did not change in 3 exchanges running (" .. #flat .. " discs worth " .. sum_of(flat) .. ")")
                        break
                    end
                else
                    unchanged = 0
                end
            end
        end
        t.check("earnDiscs", found ~= nil, "discs worth " .. cv1 .. " (1), " .. cv2 .. " (1), " .. cv3 .. " (2), " .. cv4 .. " (3) assembled after " .. asks .. " Brimstail asks and " .. exchanges .. " exchanger rounds")
        if not found then
            t.blocked("earnDiscs: no disc set found in 300 rounds")
            return
        end
        goto_machine("unlockMachine2")
        t.exec("unlockMachine.click2", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.expect("unlockMachine.open2", t.ui.await_open("eyeglo_gnome_machine_locked", 10))
        hand_reset()
        t.ticks(2)
        select_disc(t, found[1][1].sym)
        press(t, "eyeglo_gnome_machine_locked:unlock_comp_insert")
        hand_after_insert(t)
        t.check("unlockMachine.inserted", sv("varb2539_eyeglo_unlock_val") == cv1, "unlock_val=" .. tostring(sv("varb2539_eyeglo_unlock_val")) .. " want " .. cv1)
        press(t, "eyeglo_gnome_machine_locked:unlock_button")
        t.expect("unlockMachine.msg", t.msg.expect("The front panel of the machine is unlocked! Well done."))
        t.key("escape")
        t.ticks(2)
        t.expect("quest.stage.ready", t.quest.expect_stage("ready"))
        t.check("unlockMachine.state", sv("varb2502_eyeglo_machine_broken") == 2, "varb2502_eyeglo_machine_broken=" .. tostring(sv("varb2502_eyeglo_machine_broken")))

        -- ===== operate the machine (still the unlockMachine puzzle step), reveal
        goto_brimstail("ready")
        talk("ready", "I think Oaknock's machine is now unlocked, what do I do now?", 50)
        goto_machine("operate")
        t.exec("operateMachine.click", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.expect("operateMachine.open", t.ui.await_open("eyeglo_gnome_machine_unlocked", 10))
        hand_reset()
        t.ticks(2)
        local rows = { found[2], found[3], found[4] }
        local inserted = {}
        for row = 1, 3 do
            for _, d in ipairs(rows[row]) do
                select_disc(t, d.sym)
                press(t, "eyeglo_gnome_machine_unlocked:set_" .. row .. "_coinslot")
                hand_after_insert(t)
                inserted[#inserted + 1] = row .. ":" .. d.sym
            end
        end
        t.check("operate.rows", sv("varb2540_eyeglo_operate1_val") == cv2 and sv("varb2541_eyeglo_operate2_val") == cv3 and sv("varb2542_eyeglo_operate3_val") == cv4,
            "row sums " .. tostring(sv("varb2540_eyeglo_operate1_val")) .. "," .. tostring(sv("varb2541_eyeglo_operate2_val")) .. "," .. tostring(sv("varb2542_eyeglo_operate3_val")) .. " want " .. cv2 .. "," .. cv3 .. "," .. cv4
                .. " (inserted " .. table.concat(inserted, " ") .. ")")
        press(t, "eyeglo_gnome_machine_unlocked:correct")
        t.ticks(4)
        t.exec("reveal.tail", t.chat.drain, { max_pages = 40 })
        t.expect("quest.stage.operated", t.quest.expect_stage("operated"))

        -- ===== talkToBrimstailAfterIllusion (1.16)
        goto_brimstail("talkToBrimstailAfterIllusion")
        talk("talkToBrimstailAfterIllusion", "Phew! I've got that machine working now. What do I need to do now?")
        t.ticks(2)
        t.expect("quest.stage.hunting", t.quest.expect_stage("hunting"))

        -- ===== the six kills (1.17-1.22).
        -- Wiki Evil_Creature (oldid 15349482): 1 hp, max hit 1, crush; the bar's 30 is its width, not hitpoints.
        -- The creatures DO hit back, so every kill row records the measured numbers.
        -- Staged: hitpoints 40 and 8 lobsters in setup. Route: out of the cave, 5 (gate), 6 (spirit tree),
        -- 4 (north-west), into the Grand Tree for 3 (top floor) and 2 (ground floor), then back into the cave,
        -- whose entrance lands on the tunnel mouth 2409,9812, south of creature 1 (2409,9820, north of Brimstail's table).
        t.exec("equip.sword", t.player.equip, "bronze_sword")
        local function kill(k)
            local _, hp0 = t.skill.read("hitpoints")
            local before = hp0 and (hp0.current or hp0.level or hp0.boosted)
            local _, food0 = t.inv.count("lobster")
            local ar, ad = t.player.attack(k.name, 2, 30)
            t.check(k.label .. ".attack", ar == "ok", tostring(ar) .. " " .. tostring(ad))
            local _, kd = t.exec(k.label .. ".dead", t.npc.await_dead_engaged, 150, 10, { eat = { item = "lobster", below = 25 } })
            local _, hp1 = t.skill.read("hitpoints")
            local after = hp1 and (hp1.current or hp1.level or hp1.boosted)
            local _, food1 = t.inv.count("lobster")
            local lowest = tonumber(tostring(kd):match("lowest hp (%d+)/"))
            local ticks = tonumber(tostring(kd):match("dead after (%d+) tick"))
            local low = lowest or math.min(before or 0, after or 0)
            t.check(k.label .. ".margin", low >= 10 and (food1 or 0) >= 1,
                "staged hitpoints 40 + 8 lobsters; lobsters at start " .. tostring(food0) .. ", eaten " .. tostring((food0 or 0) - (food1 or 0))
                .. ", left " .. tostring(food1) .. ", lowest hp " .. tostring(lowest or "never eaten") .. ", hp before " .. tostring(before)
                .. ", hp after " .. tostring(after) .. ", ticks to death " .. tostring(ticks) .. " (margin: lowest hp " .. tostring(low) .. " >= 10 = 25% of 40 AND a lobster left)")
            t.ticks(2)
            if t.chat.kind() ~= "none" then
                -- the sixth kill's "tell the King" box (eyeglo_quest.rs2 @eyeglo_check_creatures_done)
                t.exec(k.label .. ".allDeadBox", t.chat.drain, { max_pages = 5 })
            end
            t.check(k.label .. ".varbit", sv(k.var) == 2, k.var .. "=" .. tostring(sv(k.var)))
        end

        cave_out("hunt.caveExit")
        t.exec("goto-killCreature5", t.player.goto_tile, 2461, 3390, 0)
        kill({ name = "eyeglo_fluffie_5", var = "varb2508_eyeglo_killed_eye_5", label = "killCreature5" })
        t.exec("goto-killCreature6", t.player.goto_tile, 2462, 3444, 0)
        kill({ name = "eyeglo_fluffie_6", var = "varb2509_eyeglo_killed_eye_6", label = "killCreature6" })
        t.exec("goto-killCreature4", t.player.goto_tile, 2422, 3524, 0)
        kill({ name = "eyeglo_fluffie_4", var = "varb2507_eyeglo_killed_eye_4", label = "killCreature4" })
        tree_door_in("hunt.treeDoorIn")
        for _, l in ipairs(TREE_LADDERS_UP) do
            t.exec(l[1], t.player.climb, { loc = l[2], op = l[3], op_name = "Climb-up",
                at = { 2466, 3495, l[4] }, src = { 2466, 3494 }, dest = { 2466, 3494, l[4] + 1 } })
        end
        t.ticks(3)
        kill({ name = "eyeglo_fluffie_3", var = "varb2506_eyeglo_killed_eye_3", label = "killCreature3" })
        for _, l in ipairs(TREE_LADDERS_DOWN) do
            t.exec(l[1], t.player.climb, { loc = l[2], op = l[3], op_name = "Climb-down",
                at = { 2466, 3495, l[4] }, src = { 2466, 3494 }, dest = { 2466, 3494, l[4] - 1 } })
        end
        t.ticks(3)
        kill({ name = "eyeglo_fluffie_2", var = "varb2505_eyeglo_killed_eye_2", label = "killCreature2" })
        tree_door_out("hunt.treeDoorOut")
        cave_in("hunt.enterCave")
        kill({ name = "eyeglo_fluffie_1", var = "varb2504_eyeglo_killed_eye_1", label = "killCreature1" })
        t.expect("quest.stage.all_dead", t.quest.expect_stage("all_dead"))
        goto_brimstail("allDead")
        talk("allDead", "I've killed all of the spies.", 20)

        -- ===== talkToNarnode (1.23)
        cave_out("toNarnode.caveExit")
        tree_door_in("toNarnode.treeDoorIn")
        local _, snap = t.skill.snapshot()
        t.exec("talkToNarnode", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("narnode.tail", t.chat.drain, { max_pages = 80 })
        t.ticks(3)
        t.expect("crystal seed", t.inv.expect_has("crystal_seed_old_small", 1))
        t.quest.expect_complete()
        t.expect("reward.magic", t.skill.expect_gain("magic", 12000, snap))
        t.expect("reward.runecraft", t.skill.expect_gain("runecraft", 6000, snap))
        t.expect("reward.woodcutting", t.skill.expect_gain("woodcutting", 2500, snap))
        t.expect("reward.construction", t.skill.expect_gain("construction", 250, snap))
        t.finish(0)
    end,
}
