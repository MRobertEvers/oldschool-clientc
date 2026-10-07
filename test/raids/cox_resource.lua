return {
    id = "cox_resource",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::setlevel farming 99",
        "::setlevel herblore 99",
        "::setlevel fishing 99",
        "::setlevel hunter 99",
        "::setlevel cooking 99",
        "::setlevel hitpoints 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::clearinv",
        "::give raids_seed_golpar 2",
        "::give fishing_rod",
        "::give raids_fishingbait 20",
        "::give hunting_butterfly_net",
        "::wield hunting_butterfly_net",
        -- Overload drink path: give a standard 4-dose (brewing is asserted separately).
        "::give raids_vial_overload_4",
        -- Mix path: clean herb + water + secondary for one elder brew.
        "::give raids_vial_water 2",
        "::give raids_golpar",
        "::give raids_stinkhorn_mushroom",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "resource", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "resource",
            sr == "ok" and (tostring(st.raid) .. " " .. tostring(st.room) .. " seed " .. tostring(st.mode)) or tostring(st))

        t.shot("resource idle: patches gourd geyser")

        -- Two farming plots per resource room (COX_MECHANICS.md §15).
        local pr, psum, prows = t.world.loc_copies("raids_patch_empty", 32)
        local plot_n = 0
        if pr == "ok" and type(prows) == "table" then
            plot_n = prows.total or #prows
        end
        t.check("spec.resource.plots", plot_n == 2,
            string.format("measured %d count (spec 2 count, grade D, tol exact)", plot_n))

        -- Reach: room centre can be cut off from content by authored walls;
        -- stand_on_square ::goto's onto an approach tile (gate reach hint).
        local function click_room_loc(sym, op)
            return t.player.click_loc(sym, op or 1, { stand_on_square = true })
        end

        -- Gourd pick + geyser fill (empties from the tree, not setup water).
        local _, water_before = t.inv.count("raids_vial_water")
        water_before = water_before or 0
        local gr, gd = click_room_loc("raids_gourd_tree", 1)
        t.check("gourd.pick", gr == "ok", tostring(gd))
        t.ticks(2)
        local vr, vn = t.inv.count("raids_vial_empty")
        t.check("gourd.empty", vr == "ok" and vn >= 1, "empty vials " .. tostring(vn))
        local fr, fd = click_room_loc("raids_geyser", 1)
        t.check("geyser.fill", fr == "ok", tostring(fd))
        t.ticks(2)
        local wr, wn = t.inv.count("raids_vial_water")
        t.check("geyser.water", wr == "ok" and (wn or 0) > water_before,
            "water vials " .. tostring(wn) .. " was " .. tostring(water_before))

        -- Plant via ::coxresourceplant (use_on often only map_flags when the
        -- client cache lacks Plant/use on empty patch; server oplocu exists).
        -- Mark the tick AFTER the sync plant so grow_ticks is wall time to
        -- fullygrown (delays 12+12+11+11 → 50 with queue N+1 semantics).
        local _, seeds_before = t.inv.count("raids_seed_golpar")
        seeds_before = seeds_before or 0
        t.cheat("::coxresourceplant")
        local tr, plant_tick = t.tick()
        t.check("farm.plant_tick", tr == "ok", tostring(plant_tick))
        -- loc_change is not in the client pool on the same tick as the sync
        -- debugproc; one tick later seed_locs / seed count are honest.
        t.ticks(1)
        local _, seeds_after = t.inv.count("raids_seed_golpar")
        seeds_after = seeds_after or 0
        local sr_seed, ssum, srows = t.world.loc_copies("raids_patch_golpar_seed", 32)
        local seed_n = 0
        if sr_seed == "ok" and type(srows) == "table" then
            seed_n = srows.total or #srows
        end
        local g1r, g1sum, g1rows = t.world.loc_copies("raids_patch_golpar_growth1", 32)
        local g1n = 0
        if g1r == "ok" and type(g1rows) == "table" then
            g1n = g1rows.total or #g1rows
        end
        local planted = (seeds_after < seeds_before) or (seed_n >= 1) or (g1n >= 1)
        t.check("farm.plant", planted,
            string.format("seeds %s->%s seed_locs %d growth1 %d", tostring(seeds_before), tostring(seeds_after), seed_n, g1n))
        t.ticklog.mark("herb planted")

        local grown = false
        local grow_ticks = nil
        for _ = 1, 60 do
            t.ticks(1)
            local cr, csum, crows = t.world.loc_copies("raids_patch_golpar_fullygrown", 32)
            local n = 0
            if cr == "ok" and type(crows) == "table" then
                n = crows.total or #crows
            end
            if n >= 1 then
                grown = true
                local _, now = t.tick()
                grow_ticks = now - plant_tick
                break
            end
        end
        t.check("farm.grown", grown, "golpar fullygrown within 60 ticks")
        t.check("spec.resource.herb_grow", grow_ticks == 50,
            string.format("measured %s ticks (spec 50 ticks, grade D, tol exact)", tostring(grow_ticks)))
        t.shot("resource mid: herb fully grown")

        local hr, hd = t.player.click_loc("raids_patch_golpar_fullygrown", 1, { stand_on_square = true })
        t.check("farm.harvest", hr == "ok", tostring(hd))
        t.ticks(2)
        local grr, gnn = t.inv.count("raids_grimy_golpar")
        t.check("farm.herbs", grr == "ok" and gnn >= 1, "grimy golpar " .. tostring(gnn))

        -- Mixing: clean herb on water vial (cox_herblore opheldu) with stinkhorn secondary.
        local mr, md = t.player.use_item_on_item("raids_golpar", "raids_vial_water")
        t.check("herblore.mix", mr == "ok", tostring(md))
        t.ticks(2)
        local er1, en1 = t.inv.count("raids_vial_elder_strong_4")
        local er2, en2 = t.inv.count("raids_vial_elder_4")
        local er3, en3 = t.inv.count("raids_vial_elder_weak_4")
        local elders = 0
        if er1 == "ok" then elders = elders + (en1 or 0) end
        if er2 == "ok" then elders = elders + (en2 or 0) end
        if er3 == "ok" then elders = elders + (en3 or 0) end
        t.check("herblore.elder", elders >= 1, "elder doses brewed " .. tostring(elders))

        -- Supply mode is per-room (seed+rx+8*rz). seed=1 cell 3,0 → pick 0 (bats).
        -- Prefer local presence (range 64) over session varp (last-writer).
        t.cheat("::coxresourcesupply")
        local bat = t.npc.await_present("raids_bat_6", 64, 15)
        if bat ~= "ok" then
            bat = t.npc.await_present("raids_bat_0", 64, 5)
        end
        local spot = t.npc.await_present("cox_resource_fishing_spot", 64, 5)
        if spot ~= "ok" then
            spot = t.npc.await_present("raids_fishing_snake", 64, 3)
        end
        local supply_r, supply = t.var.server("varp7341_cox_resource_supply")
        t.check("supply.mode", supply_r == "ok" or bat == "ok" or spot == "ok",
            "varp " .. tostring(supply) .. " bat=" .. tostring(bat) .. " spot=" .. tostring(spot))

        if bat == "ok" then
            local bsym = "raids_bat_6"
            local br, brow = t.npc.nearest("raids_bat_6", 64)
            if br ~= "ok" then
                bsym = "raids_bat_0"
                br, brow = t.npc.nearest("raids_bat_0", 64)
            end
            t.check("bat.present", br == "ok", tostring(br))
            if br == "ok" then
                t.player.walk_to(brow.x, brow.z, 12)
                t.ticks(2)
                local cr, cd = t.player.talk_to(bsym, 1)
                t.check("bat.click", cr == "ok" or cr == "refused", tostring(cd))
                local caught = false
                for _ = 1, 40 do
                    t.ticks(1)
                    local rr, rn = t.inv.count("raids_bat6_raw")
                    local r0, n0 = t.inv.count("raids_bat0_raw")
                    if (rr == "ok" and rn >= 1) or (r0 == "ok" and n0 >= 1) then
                        caught = true
                        break
                    end
                end
                t.check("bat.catch", caught, "raw bat after catch window")
            end
        elseif spot == "ok" then
            local br, brow = t.npc.nearest("cox_resource_fishing_spot", 64)
            if br ~= "ok" then
                br, brow = t.npc.nearest("raids_fishing_snake", 64)
            end
            t.check("fish.spot", br == "ok", tostring(br))
            if br == "ok" then
                t.player.walk_to(brow.x, brow.z, 12)
                t.ticks(2)
                local frish, fdish = t.player.talk_to("cox_resource_fishing_spot", 1)
                if frish ~= "ok" then
                    frish, fdish = t.player.talk_to("raids_fishing_snake", 1)
                end
                t.check("fish.click", frish == "ok" or frish == "refused", tostring(fdish))
                local caught = false
                for _ = 1, 40 do
                    t.ticks(1)
                    local rr, rn = t.inv.count("raids_fish6_raw")
                    if rr == "ok" and rn >= 1 then
                        caught = true
                        break
                    end
                end
                t.check("fish.catch", caught, "kyren raw after fishing window")
            end
        else
            t.check("supply.local", false, "no bats or fishing spots within 64 of player")
        end

        -- Overload: −50 hp as five hits of 10, +17@99 (article 5+13%),
        -- reapply 25, duration 500. Read boost BEFORE the HP wait — the
        -- 100-tick stat_restore can drain +17→+16 if measured later.
        local _, atk_base_row = t.skill.read("attack")
        local atk_base = 99
        if type(atk_base_row) == "table" then
            atk_base = atk_base_row.base_level or atk_base_row.level or 99
        end
        t.check("overload.attack_base", atk_base == 99, "attack base " .. tostring(atk_base))

        local _, hp_before = t.skill.read("hitpoints")
        local hp0 = (type(hp_before) == "table" and (hp_before.level or hp_before.current)) or 99
        local dr, dd = t.player.drink("raids_vial_overload_4")
        t.check("overload.drink", dr == "ok", tostring(dd))

        local _, atk_after = t.skill.read("attack")
        local boost = 0
        if type(atk_after) == "table" then
            local cur = atk_after.level or atk_after.current or atk_base
            local base = atk_after.base_level or atk_base
            boost = cur - base
        end
        t.check("spec.resource.overload_boost_99", boost == 17,
            string.format("measured %d count (spec 17 count, grade D, tol exact)", boost))

        -- Hits land across the next few ticks; track peak lost before
        -- health_regen (+1/100t) can mask the fifth hit.
        local max_lost = 0
        for _ = 1, 12 do
            t.ticks(1)
            local _, hp_after = t.skill.read("hitpoints")
            local hp1 = (type(hp_after) == "table" and (hp_after.level or hp_after.current)) or hp0
            local lost = hp0 - hp1
            if lost > max_lost then
                max_lost = lost
            end
            if max_lost >= 50 then
                break
            end
        end
        t.check("spec.resource.overload_hp_cost", max_lost == 50,
            string.format("measured %d hp (spec 50 hp, grade D, tol exact)", max_lost))

        local vr2, ticks_left = t.var.server("varp6365_overload_ticks_left")
        t.check("overload.timer_armed", vr2 == "ok" and tonumber(ticks_left) ~= nil, tostring(ticks_left))
        local left0 = tonumber(ticks_left) or 0
        t.check("spec.resource.overload_duration", left0 == 500,
            string.format("measured %d ticks (spec 500 ticks, grade D, tol exact)", left0))

        t.ticks(25)
        local vr3, ticks_left2 = t.var.server("varp6365_overload_ticks_left")
        local left1 = tonumber(ticks_left2) or 0
        local period = left0 - left1
        t.check("spec.resource.overload_reapply", period == 25,
            string.format("measured %d ticks (spec 25 ticks, grade D, tol exact)", period))

        t.shot("resource clear: farming mixed fishing/hunter overload")
        t.check("resource.complete", true, "resource room farming/mixing/fishing paths exercised")
    end,
}
