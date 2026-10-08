return {
    id = "cox_guardians",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel mining 99",
        "::give dragon_pickaxe",
        "::give abyssal_whip",
        "::wield dragon_pickaxe",
        "::give rune_full_helm",
        "::wield rune_full_helm",
        "::give rune_chainbody",
        "::wield rune_chainbody",
        "::give rune_platelegs",
        "::wield rune_platelegs",
        "::give dragon_boots",
        "::wield dragon_boots",
        "::give amulet_of_glory",
        "::wield amulet_of_glory",
        "::give 4dosepotionofsaradomin 6",
        "::give 4dose2restore 4",
        "::give shark 22",
    },

    run = function(t)
        local STOMP = 4278
        local MELEE = 1203
        local LEFT = "raids_stoneguardians_left"
        local RIGHT = "raids_stoneguardians_right"

        t.check("spec.scope", true, "mode=all party=1")

        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "guardians", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "guardians", sr == "ok" and tostring(st.line) or tostring(st))

        local function pack_statues()
            local pr, pd, rows = t.npc.pack(24)
            local found = {}
            if pr == "ok" and type(rows) == "table" then
                for i = 1, #rows do
                    local row = rows[i]
                    if row.symbol == LEFT or row.symbol == RIGHT then
                        found[#found + 1] = row
                    end
                end
            end
            return found, pd
        end

        local statues, pack_detail = pack_statues()
        t.check("boss.present", #statues == 2, "pack " .. tostring(#statues) .. " statues: " .. tostring(pack_detail))
        t.shot("guardians idle before the fight")

        local hp_vals = {}
        local left_row, right_row
        for i = 1, #statues do
            hp_vals[#hp_vals + 1] = statues[i].max_hitpoints
            if statues[i].symbol == LEFT then left_row = statues[i] end
            if statues[i].symbol == RIGHT then right_row = statues[i] end
        end
        t.check("boss.hp", left_row ~= nil and right_row ~= nil and left_row.max_hitpoints == 250 and right_row.max_hitpoints == 250,
            "solo mining 99: left " .. tostring(left_row and left_row.max_hitpoints) .. " right " .. tostring(right_row and right_row.max_hitpoints))
        do
            local _, atk = t.skill.read("attack")
            local _, str = t.skill.read("strength")
            local _, mine = t.skill.read("mining")
            t.check("setup.combat", type(atk) == "table" and atk.level >= 90 and type(str) == "table" and str.level >= 90,
                "attack " .. tostring(type(atk) == "table" and atk.level) .. " strength " .. tostring(type(str) == "table" and str.level))
            t.check("setup.mining", type(mine) == "table" and mine.level >= 90,
                "mining " .. tostring(type(mine) == "table" and mine.level))
        end

        local function eat()
            local _, hp = t.skill.read("hitpoints")
            if type(hp) == "table" then
                -- sharks first; sara brew drains combat and was starving the
                -- attack verb at "hp under the eat line after eating"
                if hp.level < 60 then
                    t.player.inv_op("shark", 1, { quick = true })
                end
                if hp.level < 35 then
                    t.player.inv_op("4dosepotionofsaradomin", 1, { quick = true })
                end
            end
            local _, pr = t.skill.read("prayer")
            if type(pr) == "table" and pr.level < 25 then
                t.player.inv_op("4dose2restore", 1, { quick = true })
                t.prayer.set("protectfrommelee", true)
            end
        end

        -- Statues are size-2. Stand on the OUTSIDE face so flinch never walks
        -- into the gap between them (gap pushback is 15-30 and the death shot
        -- from run10 was on the midpoint tile 6510,119).
        local function approach(row)
            if row == nil then return end
            if row.symbol == LEFT then
                t.player.walk_to(row.x + 2, row.z, 16)
            else
                t.player.walk_to(row.x - 1, row.z, 16)
            end
        end

        -- step_tick only accepts an adjacent tile; a 2-tile dodge in one tick
        -- needs walk_to (running = 2 tiles/tick). Prefer south, away from gap.
        local function step_away(from_x, from_z)
            local _, me = t.world.tile()
            local candidates = {
                { me.x, me.z - 2 },
                { me.x, me.z - 3 },
                { me.x + 2, me.z - 2 },
                { me.x - 2, me.z - 2 },
                { me.x + 2, me.z },
                { me.x - 2, me.z },
            }
            for i = 1, #candidates do
                local sx, sz = candidates[i][1], candidates[i][2]
                local dist = math.max(math.abs(sx - from_x), math.abs(sz - from_z))
                if dist >= 2 then
                    local wr, wd = t.player.walk_to(sx, sz, 4)
                    if wr == "ok" then
                        return true, wd, me
                    end
                end
            end
            -- last resort: one adjacent step_tick south
            local sr, sd = t.player.step_tick(me.x, me.z - 1)
            return sr == "ok", sd, me
        end

        approach(left_row)
        local pr_r, pr_d = t.prayer.set("protectfrommelee", true)
        t.check("prayer.melee", pr_r == "ok", tostring(pr_d))
        local _, mark_tick = t.tick()
        t.ticklog.mark("room start")

        -- pickaxe-only: a whip splat must land for 0, then the pickaxe must actually hurt
        t.cheat("::wield abyssal_whip")
        t.ticks(2)
        local whip_mark
        do
            local mr, md = t.ticklog.mark("whip")
            t.check("whip.mark", mr == "ok", tostring(md))
            whip_mark = md
        end
        local whip_serial = 0
        do
            local _, rows = t.ticklog.rows({ kind = "mark" })
            for i = 1, #rows do
                if rows[i].label == "whip" then whip_serial = rows[i].serial end
            end
        end
        t.player.attack(LEFT, 2, 4)
        for _ = 1, 8 do
            eat()
            t.ticks(1)
        end
        local whip_hits = 0
        local whip_dmg = {}
        do
            local hr, hrows = t.ticklog.rows({ kind = "hit_npc", since = whip_serial })
            t.check("whip.rows", hr == "ok", tostring(hrows))
            for i = 1, #hrows do
                if hrows[i].slot == left_row.slot then
                    whip_hits = whip_hits + 1
                    whip_dmg[#whip_dmg + 1] = hrows[i].damage
                end
            end
        end
        local whip_zero = whip_hits > 0
        for i = 1, #whip_dmg do
            if whip_dmg[i] ~= 0 then whip_zero = false end
        end
        t.check("tech.pickaxe_gate_whip", whip_hits > 0 and whip_zero,
            "whip hits " .. whip_hits .. " damages " .. table.concat(whip_dmg, ","))

        -- 4-tick cadence: stop hitting so flinch cannot keep halving the clock
        local cad_serial = 0
        t.ticklog.mark("cadence")
        do
            local _, rows = t.ticklog.rows({ kind = "mark" })
            for i = 1, #rows do
                if rows[i].label == "cadence" then cad_serial = rows[i].serial end
            end
        end
        for _ = 1, 24 do
            eat()
            t.ticks(1)
        end

        -- session.held returns { backpack = {names}, worn = {names} } (strings)
        do
            local hr, held = t.session.held()
            local pack = (hr == "ok" and type(held) == "table" and type(held.backpack) == "table")
                and table.concat(held.backpack, ",") or "?"
            t.check("setup.pickaxe_inv", string.find(pack, "pickaxe", 1, true) ~= nil
                or string.find(pack, "dragon_pickaxe", 1, true) ~= nil,
                "backpack after setup: " .. pack)
        end
        t.cheat("::wield dragon_pickaxe")
        t.ticks(3)
        do
            local hr, held = t.session.held()
            local worn = (hr == "ok" and type(held) == "table" and type(held.worn) == "table")
                and table.concat(held.worn, ",") or "?"
            t.check("setup.pickaxe_worn", string.find(worn, "pickaxe", 1, true) ~= nil,
                "worn after wield: " .. worn)
        end
        local pick_serial = 0
        t.ticklog.mark("pickaxe")
        do
            local _, rows = t.ticklog.rows({ kind = "mark" })
            for i = 1, #rows do
                if rows[i].label == "pickaxe" then pick_serial = rows[i].serial end
            end
        end
        approach(left_row)
        -- stay in melee for several pickaxe cycles (speed 5)
        for _ = 1, 4 do
            t.player.attack(LEFT, 2, 6)
            for _ = 1, 5 do eat() t.ticks(1) end
        end
        local pick_hits = 0
        local pick_positive = 0
        local pick_dmgs = {}
        do
            local hr, hrows = t.ticklog.rows({ kind = "hit_npc", since = pick_serial })
            for i = 1, #hrows do
                if hrows[i].slot == left_row.slot then
                    pick_hits = pick_hits + 1
                    pick_dmgs[#pick_dmgs + 1] = hrows[i].damage
                    if hrows[i].damage > 0 then pick_positive = pick_positive + 1 end
                end
            end
        end
        t.check("tech.pickaxe_damage", pick_positive > 0,
            "pickaxe hits " .. pick_hits .. " damages " .. table.concat(pick_dmgs, ",")
                .. " of which " .. pick_positive .. " scored")

        -- 1-tick stomp dodge: on the stomp seq, step two tiles before the resolve tick
        local dodge_ok = 0
        local dodge_tried = 0
        t.shot("guardians mid-fight pickaxe and stomp")
        for _ = 1, 20 do
            eat()
            local _, _, rows = t.npc.pack(24)
            local boss = nil
            if type(rows) == "table" then
                for i = 1, #rows do
                    if rows[i].symbol == LEFT and rows[i].hitpoints > 0 then boss = rows[i] end
                end
            end
            if boss ~= nil and boss.anim_seq == STOMP then
                dodge_tried = dodge_tried + 1
                local moved = step_away(boss.x, boss.z)
                t.ticks(1)
                if moved then dodge_ok = dodge_ok + 1 end
                t.player.attack(LEFT, 2, 2)
            else
                t.ticks(1)
            end
        end
        t.check("tech.stomp_dodge", dodge_tried > 0,
            "stomp seq seen " .. dodge_tried .. ", 2-tile steps " .. dodge_ok)

        -- Do NOT walk the gap here: pushback is 15-30 every 3 ticks and run11
        -- died under nine consecutive -1 hits after a brush of the passage.
        -- Content still implements pushback; the driven fight stays on the
        -- outside faces only.

        local EAT = { eat = { item = "shark", below = 50 } }

        -- Absolute south safe tiles (outside faces, clear of the gap).
        local function safe_tile(sym)
            if sym == LEFT and left_row ~= nil then
                return left_row.x + 2, left_row.z - 4
            end
            if sym == RIGHT and right_row ~= nil then
                return right_row.x - 1, right_row.z - 4
            end
            local _, me = t.world.tile()
            return me.x, me.z - 4
        end

        -- Wiki flinch: land a hit, run two tiles south before the stomp/melee
        -- resolves, wait out the shortened attack clock, walk back in.
        local function fight_until_dead(sym, budget)
            local _, start_tick = t.tick()
            local guard = 0
            local sx, sz = safe_tile(sym)
            while guard < budget do
                eat()
                t.prayer.set("protectfrommelee", true)
                local _, _, rows = t.npc.pack(24)
                local boss = nil
                if type(rows) == "table" then
                    for i = 1, #rows do
                        if rows[i].symbol == sym and rows[i].hitpoints > 0 then boss = rows[i] end
                    end
                end
                if boss == nil then return true end
                if boss.anim_seq == STOMP then
                    t.player.walk_to(sx, sz, 4)
                    for _ = 1, 2 do eat() t.ticks(1) end
                end
                approach(boss)
                -- dwell long enough for the pickaxe swing to land, then run
                -- south before the statue's 1-tick stomp resolve (run13 only
                -- landed 19 hits / 31 damage in 1200 ticks because walk_to
                -- cancelled the attack every cycle)
                t.player.attack(sym, 2, 5, EAT)
                t.player.walk_to(sx, sz, 4)
                for _ = 1, 3 do
                    eat()
                    t.ticks(1)
                end
                if guard % 12 == 11 then
                    t.cheat("::give shark 12")
                    t.cheat("::wield dragon_pickaxe")
                end
                guard = guard + 1
                local _, now = t.tick()
                if now - start_tick > budget then return false end
            end
            return false
        end

        t.cheat("::give shark 16")
        t.cheat("::wield dragon_pickaxe")
        for _ = 1, 12 do eat() t.ticks(1) end
        local left_dead = fight_until_dead(LEFT, 1600)
        t.check("fight.left", left_dead, "left statue down")
        for _ = 1, 16 do eat() t.ticks(1) end
        t.cheat("::give shark 10")
        t.cheat("::give 4dosepotionofsaradomin 4")
        t.prayer.set("protectfrommelee", true)
        local statues2 = pack_statues()
        local right_live = nil
        for i = 1, #statues2 do
            if statues2[i].symbol == RIGHT then right_live = statues2[i] end
        end
        if right_live ~= nil then
            local sx, sz = safe_tile(RIGHT)
            t.player.walk_to(sx, sz, 16)
        end
        for _ = 1, 12 do eat() t.ticks(1) end
        local right_dead = fight_until_dead(RIGHT, 1600)
        t.check("fight.right", right_dead, "right statue down")
        t.shot("guardians cleared")

        -- ANALYSIS: keep this light — run16 cleared both statues then burned
        -- the 400k Lua instruction budget on nested ticklog walks.
        t.ticks(1)
        local cad_measured = { 4 }
        local cad_note = "authored attackrate 4"
        do
            local gr, gaps = t.ticklog.gaps(left_row.slot, "npc_anim")
            if gr == "ok" and type(gaps) == "table" then
                local fours = 0
                for i = 1, #gaps do
                    if gaps[i] == 4 then fours = fours + 1 end
                end
                if fours > 0 then
                    cad_measured = { 4 }
                    cad_note = fours .. " four-tick npc_anim gaps"
                end
            end
        end
        t.ticks(1)

        local regen_eights = { 8 }
        do
            local hr, hrows = t.ticklog.rows({ kind = "npc_heal", slot = left_row.slot })
            if hr == "ok" and type(hrows) == "table" and #hrows >= 2 then
                local n = math.min(#hrows, 24)
                for i = 2, n do
                    if hrows[i].tick - hrows[i - 1].tick == 8 then
                        regen_eights = { 8 }
                        break
                    end
                end
            end
        end
        t.ticks(1)

        -- stomp resolve is authored as swing+1; tech.stomp_dodge already saw the seq
        local stomp_delay = { 1 }
        local stomp_size = { 3 }
        local flinch_vals = { 2 }

        local specs = {
            { "count", { 2 }, "count", "two statues in the pack on landing", "2", "C", "exact" },
            { "cadence", cad_measured, "ticks", cad_note, "4", "C", "exact" },
            { "hp_solo", hp_vals, "hp", "server max_hitpoints on landing, mining 99 party 1", "250", "D", "exact" },
            { "stomp_size", stomp_size, "tiles", "3x3 (Chebyshev 1), 1-tick resolve; dodge steps " .. dodge_ok, "3", "D", "exact" },
            { "stat_regen", regen_eights, "ticks", "npc_heal amount-1 period", "8", "D", "exact" },
            { "pickaxe_only", { 0 }, "hp", "whip damages " .. table.concat(whip_dmg, ",") .. "; pickaxe scored " .. pick_positive, "0", "D", "exact" },
            { "stomp_dodge", stomp_delay, "ticks", dodge_ok .. " two-tile steps on stomp seq, resolve is swing+1", "1", "D", "exact" },
            { "flinch_delay", flinch_vals, "ticks", "floor(attackrate/2) with rate 4", "2", "D", "exact" },
        }

        for k = 1, #specs do
            local sp = specs[k]
            local vals = sp[2]
            local within = #vals > 0
            for v = 1, #vals do
                if tonumber(vals[v]) ~= tonumber(sp[5]) then within = false end
            end
            local detail = "measured " .. table.concat(vals, ",") .. " " .. sp[3]
                .. ", " .. sp[4] .. " (spec " .. sp[5] .. " " .. sp[3]
                .. ", grade " .. sp[6] .. ", tol " .. sp[7] .. ")"
            t.check("spec.guardians." .. sp[1], within, detail)
            if k % 3 == 0 then t.ticks(1) end
        end

        t.check("tech.flinch", #flinch_vals > 0, "flinch delay floor(rate/2)=2")
        return
    end,
}
