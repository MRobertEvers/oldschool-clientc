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
    local n, k = #flat, #sums
    local used = {}
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
                    if nodes > 2500 or locked[g] ~= nil then return end
                    if count == size then
                        if sum == sums[g] then
                            local picked = {}
                            for i2 = 1, size do picked[i2] = flat[members[i2]] end
                            if size == wants[g] then
                                locked[g] = picked
                                for i2 = 1, size do used[members[i2]] = true end
                            elseif reshape == nil then
                                reshape = picked
                                reshape_big = size > wants[g]
                            end
                        end
                        return
                    end
                    for i = start, n do
                        if not used[i] and sum + flat[i].v <= sums[g] then
                            members[count + 1] = i
                            comb(i + 1, count + 1, sum + flat[i].v)
                        end
                    end
                end
                comb(1, 0, 0)
            end
        end
    end
    local leftover = {}
    for i = 1, n do
        if not used[i] then leftover[#leftover + 1] = flat[i] end
    end
    return locked, reshape, reshape_big, leftover
end

-- up to three discs worth at most 35 points, chosen at random from `list`
local function pick_exchange_subset(list, big)
    local combos = {}
    local n = #list
    for a = 1, n do
        if list[a].v <= 35 then combos[#combos + 1] = { a } end
        for b = a + 1, n do
            if list[a].v + list[b].v <= 35 then combos[#combos + 1] = { a, b } end
            for c = b + 1, n do
                if list[a].v + list[b].v + list[c].v <= 35 then combos[#combos + 1] = { a, b, c } end
            end
        end
    end
    local want_size = 0
    for _, c in ipairs(combos) do
        if big and #c > want_size then want_size = #c end
    end
    local chosen = {}
    for _, c in ipairs(combos) do
        if big then
            if #c == want_size then chosen[#chosen + 1] = c end
        elseif #c == 1 then
            chosen[#chosen + 1] = c
        end
    end
    if #chosen == 0 then chosen = combos end
    local pick = chosen[math.random(#chosen)]
    local out = {}
    for _, i in ipairs(pick) do out[#out + 1] = list[i] end
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

local function select_disc(t, sym)
    local _, slots = held_discs(t)
    if slots[sym] == nil then
        t.step("select " .. sym, "FAIL", "not held")
        return false
    end
    return press(t, "eyeglo_side:inv_layer", slots[sym])
end

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
        "::give bucket_empty 1",
        "::give knife 1",
        "::give mudrune 1",
        "::give pestle_and_mortar 1",
        "::give oak_logs 1",
        "::give maple_logs 1",
        "::give poh_saw 1",
        "::give hammer 1",
        "::give bronze_sword 1",
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
        local function talk(name, option, max)
            t.exec(name .. ".talk", t.player.talk_to, "gnome_brimstail", 1)
            t.exec(name .. ".menu", t.chat.drain, { stop_at = "options" })
            t.exec(name .. ".pick", t.chat.choose, option)
            t.exec(name .. ".tail", t.chat.drain, { max_pages = max or 100 })
        end
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ===== talkToBrimstail / enterCave (guide 1.1, 1.2)
        t.exec("goto-enterCave", t.player.goto_tile, 2404, 3421, 0)
        t.exec("enterCave", t.player.click_loc, "eyeglo_brimstails_cave_entrance", 1)
        t.ticks(4)
        t.check("enterCave.below", t.world.tile ~= nil and select(2, t.world.tile()) ~= nil, "tile after the cave entrance: " .. tostring(select(2, t.world.tile())))
        t.exec("goto-talkToBrimstail", t.player.goto_tile, 2408, 9817, 0)
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

        -- ===== inspectBowl, inspectMachine (1.3, 1.4)
        t.exec("goto-inspectBowl", t.player.goto_tile, 2388, 9811, 0)
        t.exec("inspectBowl", t.player.click_loc, "eyeglo_singing_bowl", 1)
        t.ticks(2)
        t.check("inspectBowl.seen", sv("varb2515_eyeglo_bowl_seen") == 1, "varb2515_eyeglo_bowl_seen=" .. tostring(sv("varb2515_eyeglo_bowl_seen")))
        t.exec("goto-inspectPanel", t.player.goto_tile, 2392, 9824, 0)
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
        t.exec("goto-talkToBrimstailAgain", t.player.goto_tile, 2408, 9817, 0)
        t.exec("talkToBrimstailAgain", t.player.talk_to, "gnome_brimstail", 1)
        t.exec("brim2.menu", t.chat.drain, { stop_at = "options" })
        t.exec("brim2.pick", t.chat.choose, "I've had a look in the other room now.")
        t.exec("brim2.q", t.chat.drain, { stop_at = "options" })
        t.exec("brim2.go", t.chat.choose, "Of course, I'd love to!")
        t.exec("brim2.tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.told", t.quest.expect_stage("told"))

        -- ===== goUpToHazelmere, talkToHazelmere (1.6, 1.7): needs unboosted 46 magic
        t.exec("goto-goUpToHazelmere", t.player.goto_tile, 2677, 3086, 0)
        t.exec("goUpToHazelmere", t.player.click_loc, "ladder", 1)
        t.ticks(4)
        t.check("goUpToHazelmere.level", select(2, t.world.level()) == 1, "level " .. tostring(select(2, t.world.level())))
        t.exec("talkToHazelmere", t.player.talk_to, "grandtree_hazelmere", 1)
        t.exec("hazelmere.story", t.chat.drain, { max_pages = 250 })
        t.ticks(2)
        t.expect("quest.stage.heard", t.quest.expect_stage("heard"))
        t.expect("hazelmere.disc", t.inv.expect_has("eyeglo_violet_pentagon", 1))

        -- ===== enterCaveAgain, talkToBrimstailAfterHazelmere (1.8, 1.9)
        t.exec("goto-enterCaveAgain", t.player.goto_tile, 2404, 3421, 0)
        t.exec("enterCaveAgain", t.player.click_loc, "eyeglo_brimstails_cave_entrance", 1)
        t.ticks(4)
        t.exec("goto-talkToBrimstailAfterHazelmere", t.player.goto_tile, 2408, 9817, 0)
        talk("talkToBrimstailAfterHazelmere", "I've visited Hazelmere, he told me all sorts of interesting things.")
        t.ticks(2)
        t.expect("quest.stage.sabotaged", t.quest.expect_stage("sabotaged"))
        t.check("sabotaged.broken", sv("varb2502_eyeglo_machine_broken") == 1, "varb2502_eyeglo_machine_broken=" .. tostring(sv("varb2502_eyeglo_machine_broken")))
        talk("sabotage", "The machine is broken, I suspect sabotage...!")
        t.ticks(2)
        t.expect("quest.stage.discussed", t.quest.expect_stage("discussed"))
        t.exec("goto-inspectBrokenMachine", t.player.goto_tile, 2391, 9824, 0)
        t.exec("inspectBrokenMachine", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.exec("inspectBrokenMachine.tail", t.chat.drain, { max_pages = 20 })
        t.ticks(2)
        t.expect("quest.stage.inspected", t.quest.expect_stage("inspected"))
        t.exec("inspectMissing", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.ticks(2)
        t.expect("inspectMissing.msg", t.msg.expect("You need something to stick all the bits together."))
        t.exec("goto-glueHint", t.player.goto_tile, 2408, 9817, 0)
        talk("glueHint", "I'm still a bit confused about how to fix Oaknock's machine.")
        t.ticks(2)
        t.expect("quest.stage.glue_hint", t.quest.expect_stage("glue_hint"))

        -- ===== gather the sap, grindMudRunes, useMudOnSap (1.10, 1.11): the sap comes from the nearest evergreen
        t.exec("goto-evergreen", t.player.goto_tile, 2360, 3527, 0)
        local near_ok, near_tree = t.world.loc_near("evergreen", 12)
        local tree_sym = near_ok == "ok" and "evergreen" or "evergreen_large"
        local tree = t.player.by_symbol("loc", tree_sym)
        local ur, ud = t.player.use_on("knife", tree)
        t.check("knifeOnEvergreen", ur == "ok", tostring(ur) .. " " .. tostring(ud) .. " (tree " .. tree_sym .. ")")
        t.ticks(2)
        t.expect("sap", t.inv.expect_has("ics_little_sap_bucket", 1))
        local gr, gd = t.player.use_item_on_item("pestle_and_mortar", "mudrune")
        t.check("grindMudRunes", gr == "ok", tostring(gr) .. " " .. tostring(gd))
        t.expect("ground", t.inv.expect_has("eyeglo_ground_mud_runes", 1))
        local mr, md = t.player.use_item_on_item("eyeglo_ground_mud_runes", "ics_little_sap_bucket")
        t.check("useMudOnSap", mr == "ok", tostring(mr) .. " " .. tostring(md))
        t.expect("glue", t.inv.expect_has("eyeglo_magic_glue", 1))

        -- ===== repairMachine (1.12)
        t.exec("goto-repairMachine", t.player.goto_tile, 2391, 9824, 0)
        local machine = t.player.by_symbol("loc", "eyeglo_gnome_machine_02_multiloc")
        local rr, rd = t.player.use_on("eyeglo_magic_glue", machine)
        t.check("repairMachine", rr == "ok" or rr == "refused", tostring(rr) .. " " .. tostring(rd))
        t.ticks(6)
        t.expect("quest.stage.repaired", t.quest.expect_stage("repaired"))
        t.expect("glue gone", t.inv.expect_absent("eyeglo_magic_glue"))
        t.expect("oak gone", t.inv.expect_absent("oak_logs"))
        t.expect("maple gone", t.inv.expect_absent("maple_logs"))

        -- ===== talkToBrimstailAfterRepairing, talkToBrimstailForMoreDisks (1.13, 1.14)
        t.exec("goto-talkToBrimstailAfterRepairing", t.player.goto_tile, 2408, 9817, 0)
        talk("talkToBrimstailAfterRepairing", "I think I've fixed the machine now!")
        t.ticks(2)
        t.expect("quest.stage.discs_given", t.quest.expect_stage("discs_given"))
        t.expect("red square", t.inv.expect_has("eyeglo_red_square", 1))
        t.expect("yellow triangle", t.inv.expect_has("eyeglo_yellow_triangle", 1))
        talk("talkToBrimstailForMoreDisks", "I can't work out what to do with these discs!")
        t.ticks(2)
        t.expect("quest.stage.more_discs", t.quest.expect_stage("more_discs"))

        -- ===== unlockMachine (1.15): open the window to read this player's target, then earn the disc
        t.exec("goto-unlockMachine", t.player.goto_tile, 2391, 9824, 0)
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
        local asks, exchanges = 0, 0
        local need_total = cv1 + cv2 + cv3 + cv4
        for iter = 1, 300 do
            local flat = held_discs(t)
            local locked, reshape, reshape_big, leftover = plan(flat, sums, wants)
            local nlocked = 0
            for g = 1, 4 do if locked[g] ~= nil then nlocked = nlocked + 1 end end
            t.step("round " .. iter, #flat >= 0 and "PASS" or "FAIL", "discs held " .. #flat .. " worth " .. sum_of(flat) .. " (need " .. need_total .. "), groups locked " .. nlocked .. "/4, leftover " .. #leftover .. " worth " .. sum_of(leftover))
            if nlocked == 4 then
                found = { locked[1], locked[2], locked[3], locked[4] }
                break
            end
            local subset = nil
            if reshape and sum_of(flat) >= need_total then
                subset = pick_exchange_subset(reshape, reshape_big)
            elseif sum_of(flat) < need_total and #flat < 6 then
                t.expect("moreDiscs.goto", t.player.goto_tile(2408, 9817, 0))
                t.expect("moreDiscs.talk", t.player.talk_to("gnome_brimstail", 1))
                t.expect("moreDiscs.menu", t.chat.drain({ stop_at = "options" }))
                t.expect("moreDiscs.pick", t.chat.choose("I can't work out what to do with these discs!"))
                t.expect("moreDiscs.tail", t.chat.drain({ max_pages = 40 }))
                asks = asks + 1
            else
                -- re-roll the discs that are not locked into a group (consolidate while short of value)
                if #leftover == 0 or sum_of(flat) < need_total then leftover = flat end
                subset = pick_exchange_subset(leftover, sum_of(flat) < need_total)
            end
            if subset then
                t.expect("exchange.goto", t.player.goto_tile(2391, 9824, 0))
                t.expect("exchange.click", t.player.click_loc("eyeglo_change_machine_multiloc", 1))
                t.expect("exchange.open", t.ui.await_open("eyeglo_change_machine", 10))
                t.ticks(2)
                for _, d in ipairs(subset) do
                    select_disc(t, d.sym)
                    press(t, "eyeglo_change_machine:exchange_coinslot")
                end
                press(t, "eyeglo_change_machine:exchange_button")
                press(t, "eyeglo_change_machine:take_button")
                t.key("escape")
                t.ticks(2)
                exchanges = exchanges + 1
            end
        end
        t.check("earnDiscs", found ~= nil, "discs worth " .. cv1 .. " (1), " .. cv2 .. " (1), " .. cv3 .. " (2), " .. cv4 .. " (3) assembled after " .. asks .. " Brimstail asks and " .. exchanges .. " exchanger rounds")
        if not found then
            t.blocked("could not assemble the unlock and operate discs in 250 rounds")
            return
        end
        t.exec("goto-unlockMachine2", t.player.goto_tile, 2391, 9824, 0)
        t.exec("unlockMachine.click2", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.expect("unlockMachine.open2", t.ui.await_open("eyeglo_gnome_machine_locked", 10))
        t.ticks(2)
        select_disc(t, found[1][1].sym)
        press(t, "eyeglo_gnome_machine_locked:unlock_comp_insert")
        t.check("unlockMachine.inserted", sv("varb2539_eyeglo_unlock_val") == cv1, "unlock_val=" .. tostring(sv("varb2539_eyeglo_unlock_val")) .. " want " .. cv1)
        press(t, "eyeglo_gnome_machine_locked:unlock_button")
        t.expect("unlockMachine.msg", t.msg.expect("The front panel of the machine is unlocked! Well done."))
        t.key("escape")
        t.ticks(2)
        t.expect("quest.stage.ready", t.quest.expect_stage("ready"))
        t.check("unlockMachine.state", sv("varb2502_eyeglo_machine_broken") == 2, "varb2502_eyeglo_machine_broken=" .. tostring(sv("varb2502_eyeglo_machine_broken")))

        -- ===== operate the machine (still the unlockMachine puzzle step), reveal
        t.exec("goto-ready", t.player.goto_tile, 2408, 9817, 0)
        talk("ready", "I think Oaknock's machine is now unlocked, what do I do now?", 50)
        t.exec("goto-operate", t.player.goto_tile, 2391, 9824, 0)
        t.exec("operateMachine.click", t.player.click_loc, "eyeglo_gnome_machine_02_multiloc", 1)
        t.expect("operateMachine.open", t.ui.await_open("eyeglo_gnome_machine_unlocked", 10))
        t.ticks(2)
        local rows = { found[2], found[3], found[4] }
        for row = 1, 3 do
            for _, d in ipairs(rows[row]) do
                select_disc(t, d.sym)
                press(t, "eyeglo_gnome_machine_unlocked:set_" .. row .. "_coinslot")
            end
        end
        t.check("operate.rows", sv("varb2540_eyeglo_operate1_val") == cv2 and sv("varb2541_eyeglo_operate2_val") == cv3 and sv("varb2542_eyeglo_operate3_val") == cv4,
            "row sums " .. tostring(sv("varb2540_eyeglo_operate1_val")) .. "," .. tostring(sv("varb2541_eyeglo_operate2_val")) .. "," .. tostring(sv("varb2542_eyeglo_operate3_val")) .. " want " .. cv2 .. "," .. cv3 .. "," .. cv4)
        press(t, "eyeglo_gnome_machine_unlocked:correct")
        t.ticks(4)
        t.exec("reveal.tail", t.chat.drain, { max_pages = 40 })
        t.expect("quest.stage.operated", t.quest.expect_stage("operated"))

        -- ===== talkToBrimstailAfterIllusion (1.16)
        t.exec("goto-talkToBrimstailAfterIllusion", t.player.goto_tile, 2408, 9817, 0)
        talk("talkToBrimstailAfterIllusion", "Phew! I've got that machine working now. What do I need to do now?")
        t.ticks(2)
        t.expect("quest.stage.hunting", t.quest.expect_stage("hunting"))

        -- ===== the six kills (1.17-1.22): 1 hp creatures that do not retaliate
        t.exec("equip.sword", t.player.equip, "bronze_sword")
        t.exec("goto-narnodeFirst", t.player.goto_tile, 2465, 3495, 0)
        t.exec("narnodeFirst.talk", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("narnodeFirst.tail", t.chat.drain, { max_pages = 60 })
        local kills = {
            { n = 1, at = { 2408, 9818, 0 }, name = "eyeglo_fluffie_1", var = "varb2504_eyeglo_killed_eye_1", label = "killCreature1" },
            { n = 5, at = { 2461, 3390, 0 }, name = "eyeglo_fluffie_5", var = "varb2508_eyeglo_killed_eye_5", label = "killCreature5" },
            { n = 6, at = { 2462, 3445, 0 }, name = "eyeglo_fluffie_6", var = "varb2509_eyeglo_killed_eye_6", label = "killCreature6" },
            { n = 4, at = { 2422, 3524, 0 }, name = "eyeglo_fluffie_4", var = "varb2507_eyeglo_killed_eye_4", label = "killCreature4" },
            { n = 3, at = { 2466, 3495, 3 }, name = "eyeglo_fluffie_3", var = "varb2506_eyeglo_killed_eye_3", label = "killCreature3" },
            { n = 2, at = { 2465, 3492, 0 }, name = "eyeglo_fluffie_2", var = "varb2505_eyeglo_killed_eye_2", label = "killCreature2" },
        }
        for i, k in ipairs(kills) do
            t.exec("goto-" .. k.label, t.player.goto_tile, k.at[1], k.at[2], k.at[3])
            t.ticks(2)
            local ar, ad = t.player.attack(k.name, 2, 30)
            t.check(k.label .. ".attack", ar == "ok", tostring(ar) .. " " .. tostring(ad))
            local kr, kd = t.npc.await_dead_engaged(60, 4, {})
            t.check(k.label .. ".dead", kr == "ok", tostring(kr) .. " " .. tostring(kd) .. " (1 hp creature, no retaliation; staged: bronze sword, no food needed, hp untouched)")
            t.ticks(2)
            t.check(k.label .. ".varbit", sv(k.var) == 2, k.var .. "=" .. tostring(sv(k.var)))
        end
        t.expect("quest.stage.all_dead", t.quest.expect_stage("all_dead"))
        t.exec("goto-allDead", t.player.goto_tile, 2408, 9817, 0)
        talk("allDead", "I've killed all of the spies.", 20)

        -- ===== talkToNarnode (1.23)
        t.exec("goto-talkToNarnode", t.player.goto_tile, 2465, 3495, 0)
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
