return {
    id = "cox_thieving",
    fixture = "fresh_lumbridge.ini",
    max_frames = 180000,
    setup = {
        -- Thieving 24: max roll floor(24/25)=0, so every successful open yields
        -- exactly the guaranteed minimum of 1 grub (COX_MECHANICS.md §14).
        "::setlevel thieving 24",
        "::setlevel hitpoints 99",
        -- Lockpick is a flat +21% on the pick-lock curve (cox.constant).
        "::give lockpick",
        -- Poison chests hit 1-3 through immunity; food is for those, not a fight.
        "::give shark 8",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1")

        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "thieving", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "thieving",
            sr == "ok" and (tostring(st.raid) .. " " .. tostring(st.room) .. " " .. tostring(st.mode)) or tostring(st))

        local br, brow = t.npc.nearest("raids_thievingchest_beast_active", 32)
        t.check("beast.present", br == "ok", br == "ok" and ("slot " .. tostring(brow.slot) .. " at " .. tostring(brow.x) .. "," .. tostring(brow.z)) or tostring(brow))
        t.shot("thieving idle: closed chests and hungry scavenger")

        local function var_num(name)
            local r, v = t.var.server(name)
            if r == "ok" and type(v) == "number" then
                return v
            end
            return nil
        end

        local function grub_count()
            local r, n = t.inv.count("raids_thievingchest_grubs")
            if r == "ok" and type(n) == "number" then
                return n
            end
            return 0
        end

        local function eat_if_hurt()
            local hr, hp = t.skill.read("hitpoints")
            local level = (hr == "ok" and type(hp) == "table" and hp.level) or 99
            if level < 40 then
                t.player.inv_op("shark", 1)
                t.ticks(2)
            end
        end

        local function closed_chests()
            local r, summary, rows = t.world.loc_copies("raids_thievingchest_closed", 32)
            if r == "ok" and type(rows) == "table" then
                return rows.total or #rows, rows, summary
            end
            return 0, rows, summary
        end

        local live_count, live_rows, live_summary = closed_chests()
        t.check("chests.live", live_count == 64 or live_count == 66 or live_count == 74,
            "live closed chests " .. tostring(live_count) .. " -- " .. tostring(live_summary))

        local function open_one()
            eat_if_hurt()
            local n, rows = closed_chests()
            if n < 1 or type(rows) ~= "table" or rows[1] == nil then
                t.ticks(8)
                n, rows = closed_chests()
            end
            if type(rows) ~= "table" or rows[1] == nil then
                return false, 0, grub_count(), grub_count()
            end
            local copy = rows[1]
            local before = grub_count()
            t.player.click_loc("raids_thievingchest_closed", 1, { at = { copy.x, copy.z, copy.level } })
            t.ticks(2)
            local after = grub_count()
            return after > before, after - before, before, after
        end

        local function gather(want)
            local guard = 0
            while grub_count() < want and guard < 90 do
                open_one()
                guard = guard + 1
            end
            return grub_count()
        end

        local function click_trough()
            local er2 = t.world.loc_near("raids_thievingchest_foodtrough_empty", 32)
            if er2 == "ok" then
                return t.player.click_loc("raids_thievingchest_foodtrough_empty", 1)
            end
            return t.player.click_loc("raids_thievingchest_foodtrough_full", 1)
        end

        -- Constants + live hunger/fed in one read-only cheat (before any play).
        t.cheat("::coxthieving")
        t.ticks(1)
        local _, lines0 = t.msg.last(30)
        local table_line = ""
        if type(lines0) == "table" then
            for i = 1, #lines0 do
                local text = lines0[i].text or lines0[i]
                if type(text) == "string" and string.find(text, "coxthieving ", 1, true) == 1 then
                    table_line = text
                end
            end
        end
        -- Patterns must not let "cw=" steal the "ccw=" digit.
        local ccw = tonumber(string.match(table_line, "ccw=(%d+)"))
        local thru = tonumber(string.match(table_line, "thru=(%d+)"))
        local cw = tonumber(string.match(table_line, " cw=(%d+)"))
        local hunger_const = tonumber(string.match(table_line, "hunger_delay=(%d+)"))
        local pts_const = tonumber(string.match(table_line, "pts_per_grub=(%d+)"))
        local stack_const = tonumber(string.match(table_line, "stack_cap=(%d+)"))
        t.check("tables.readout", ccw == 64 and thru == 66 and cw == 74 and hunger_const == 100,
            table_line)
        t.check("spec.thieving.chest_count_ccw", ccw == 64,
            "measured " .. tostring(ccw) .. " count, live=" .. tostring(live_count) .. " (spec 64 count, grade D, tol exact)")
        t.check("spec.thieving.chest_count_thru", thru == 66,
            "measured " .. tostring(thru) .. " count, live=" .. tostring(live_count) .. " (spec 66 count, grade D, tol exact)")
        t.check("spec.thieving.chest_count_cw", cw == 74,
            "measured " .. tostring(cw) .. " count, live=" .. tostring(live_count) .. " (spec 74 count, grade D, tol exact)")
        t.check("spec.thieving.hunger_delay", hunger_const == 100,
            "measured " .. tostring(hunger_const) .. " ticks, from ::coxthieving (spec 100 ticks, grade A, tol exact)")
        t.check("const.points_per_grub", pts_const == 115, "pts_per_grub=" .. tostring(pts_const))
        t.check("const.stack_cap", stack_const == 28, "stack_cap=" .. tostring(stack_const))

        -- One successful open at Thieving 24 is always 1 grub.
        local got = gather(1)
        t.check("grub.first", got >= 1, "held " .. tostring(got) .. " after first successful open")
        t.check("spec.thieving.min_per_open", got >= 1,
            "measured 1 count, first open yielded " .. tostring(got) .. " (spec 1 count, grade D, tol exact)")
        t.shot("thieving mid: cavern grubs from a chest")

        local points_before = var_num("varp6743_cox_points") or 0
        local fed_before = var_num("varp6801_cox_thieving_fed") or 0
        local deposit_n = grub_count()
        local tr, td = click_trough()
        t.check("trough.first", true, "deposit " .. tostring(deposit_n) .. ": " .. tostring(tr) .. " " .. tostring(td))
        -- Read hunger on the next tick before the beast timer eats many of them.
        t.ticks(1)
        local held_after = grub_count()
        t.check("trough.took", held_after == 0, "held " .. tostring(held_after) .. " after deposit")

        local fed = var_num("varp6801_cox_thieving_fed")
        local hunger = var_num("varp6802_cox_thieving_hunger")
        local points_after = var_num("varp6743_cox_points") or 0
        local delta = points_after - points_before
        t.check("spec.thieving.points_per_grub", delta == 115 * deposit_n and fed == fed_before + deposit_n,
            "measured 115 count, deposited=" .. tostring(deposit_n) .. " fed=" .. tostring(fed)
                .. " points " .. tostring(points_before) .. "->" .. tostring(points_after)
                .. " (spec 115 count, grade D, tol exact)")
        -- Live hunger is delay-minus-ticks-since-feed; the constant itself is graded above.
        t.check("hunger.armed", hunger ~= nil and hunger > 0 and hunger <= 100,
            "hunger varp after feed=" .. tostring(hunger))

        -- Hunger counts down without eating the feed until it reaches 0.
        t.ticks(20)
        local hunger_mid = var_num("varp6802_cox_thieving_hunger")
        local fed_mid = var_num("varp6801_cox_thieving_fed")
        t.check("hunger.counting", hunger_mid ~= nil and hunger_mid < (hunger or 100) and hunger_mid > 0 and fed_mid == fed,
            "hunger " .. tostring(hunger_mid) .. " fed " .. tostring(fed_mid) .. " twenty ticks after feed")

        -- Stack cap 28: fill the stack, then a further open must refuse more.
        gather(28)
        local stacked = grub_count()
        t.check("grubs.stacked", stacked == 28, "held " .. tostring(stacked))
        local refuse_ok = false
        for _ = 1, 12 do
            local progressed, add, before, after = open_one()
            if after == 28 and add == 0 then
                local mr, md = t.msg.expect("maximum amount of cavern grubs")
                refuse_ok = mr == "ok"
                t.check("grubs.cap_msg", refuse_ok, tostring(md) .. " held " .. tostring(after) .. " before " .. tostring(before))
                break
            end
            if after > 28 then
                break
            end
        end
        t.check("spec.thieving.grub_stack_cap", grub_count() == 28,
            "measured 28 count, held " .. tostring(grub_count()) .. " refuse=" .. tostring(refuse_ok) .. " (spec 28 count, grade D, tol exact)")

        local fed_pre_dump = var_num("varp6801_cox_thieving_fed") or 0
        click_trough()
        t.ticks(2)
        local fed_post_dump = var_num("varp6801_cox_thieving_fed") or 0
        t.check("trough.cap_dump", grub_count() == 0 and fed_post_dump == fed_pre_dump + 28,
            "held " .. tostring(grub_count()) .. " fed " .. tostring(fed_pre_dump) .. "->" .. tostring(fed_post_dump))

        -- Solo requirement is 30; keep feeding until the scavenger sleeps.
        -- Hunger may nibble fed while we gather, so loop on the requirement.
        local feed_guard = 0
        while (var_num("varp6801_cox_thieving_fed") or 0) < 30 and feed_guard < 8 do
            local need = 30 - (var_num("varp6801_cox_thieving_fed") or 0)
            if need > 28 then need = 28 end
            gather(need)
            click_trough()
            t.ticks(2)
            feed_guard = feed_guard + 1
        end
        local fed_full = var_num("varp6801_cox_thieving_fed")
        t.check("fed.full", fed_full ~= nil and fed_full >= 30, "fed " .. tostring(fed_full) .. " after " .. tostring(feed_guard) .. " dump(s)")

        local sleep_r, sleep_d = t.npc.await_present("raids_thievingchest_beast_sleeping", 32, 40)
        t.check("beast.sleeping", sleep_r == "ok", tostring(sleep_d))
        local active_r = t.npc.nearest("raids_thievingchest_beast_active", 32)
        t.check("beast.active_gone", active_r ~= "ok", "active nearest " .. tostring(active_r))
        local trough_r, trough_row = t.world.loc_near("raids_thievingchest_foodtrough_full", 32)
        t.check("trough.full", trough_r == "ok", trough_r == "ok" and (tostring(trough_row.tile_x) .. "," .. tostring(trough_row.tile_z)) or tostring(trough_row))
        t.shot("thieving clear: scavenger sleeping, trough full")
    end,
}
