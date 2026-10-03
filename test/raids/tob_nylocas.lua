-- Theatre of Blood, Nylocas, Entry mode (solo).
-- The player kills every melee and ranged nylocas with the weapon of its style (a wrong-style hit nulls it
-- for good); magic nylocas cannot be damaged at all by any player attack in this content (CONTENT FINDING at
-- the end of the file), so the fight is driven until the room tick cap and then graded from the tick log.
return {
    id = "tob_nylocas",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000,
    setup = {
        "::clearinv",
        -- an Entry nylocas player's combat stats: melee and ranged weapons for the nylocas' two killable styles
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 70",
        -- one weapon per nylocas style: a wrong-style hit nulls that nylocas for good
        "::give abyssal_whip 1",
        "::give magic_shortbow 1",
        -- the third weapon: a spell the magic nylocas (a wrong-style hit nulls them) can be killed with, fire strike
        "::give staff_of_fire 1",
        "::give airrune 1500",
        "::give mindrune 500",
        "::give rune_arrow 800",
        -- food: the aggro nylocas swing at the player every 3 ticks for the whole room
        "::give shark 22",
    },
    run = function(t)
        t.check("spec.scope", true, "mode=entry party=1")
        local enter_result, enter_detail = t.raid.enter("tob", "nylocas", { mode = "entry" })
        t.check("enter", enter_result == "ok", tostring(enter_detail))
        local state_result, state = t.raid.state()
        t.check("state", state_result == "ok" and state.room == "nylocas" and state.mode == "entry" and state.started == false,
            type(state) == "table" and tostring(state.line) or tostring(state))
        t.ticklog.start()
        local tile_result, fight, fight_text = t.raid.start_tile()
        t.check("start_tile", tile_result == "ok", tostring(fight_text))
        t.player.walk_to(fight.x + 1, fight.z, 20)
        local click_result, click_detail = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", click_result == "ok", tostring(click_detail))
        local play_result, play_detail = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.confirm", play_result == "ok", tostring(play_detail))
        t.ticklog.mark("room start")
        local _, tick0 = t.tick()
        t.drive.camera(0, 512, 1100)
        t.player.equip("rune_arrow")
        local weapon = { melee = "abyssal_whip", ranged = "magic_shortbow", magic = "staff_of_fire" }
        local style_names = { "melee", "ranged", "magic" }
        local fighting_kinds = { "big_fighting", "fighting" }
        local incoming_kinds = { "big_incoming", "incoming" }
        local boss_symbol = { melee = "nylocas_boss_melee_story", magic = "nylocas_boss_magic_story", ranged = "nylocas_boss_ranged_story" }
        local prayer_of = { melee = "protectfrommelee", magic = "protectfrommagic", ranged = "protectfrommissiles" }
        local hold_of = { melee = 5, ranged = 5, magic = 5 }
        local current, swings, casts, eaten, idle, lowest, disarms = nil, 0, 0, 0, 0, 99, 0
        local boss_style, boss_calls, waves_calls, slow = nil, 0, 0, {}
        local boss_forms = {}
        local fight_end = "tick cap"
        local results = {}
        local samples, under_attack = {}, 0
        local small_hp_read, big_hp_read, support_hp_read = nil, nil, nil
        -- where the ticks go: equip, eat, press, step-away, waiting for a locked target
        local cost = { equip = 0, eat = 0, press = 0, step = 0, wait = 0 }
        local lock, skips, trace = nil, {}, {}
        local seen_at = {}
        local hang_events, hang_pending, hang_ticks = {}, nil, 0
        local ring, ring_count = {}, 0
        local pillars_fallen, next_pillar_poll, next_progress, steps_away = 0, 0, 60, 0
        local boss_seen_tick, boss_hp_line = nil, nil
        -- the support that is let fall (the south-west one): the collapse rows need one fall, and a fall hands every chewer on it to
        -- the player at once (13 hit together in the last pass), so its chewers are left alone only while five or fewer are on it
        local pillar_result, _, pillar_rows = t.npc.tiles("tob_nylocas_support_story", 40)
        local doomed = { x = -100, z = -100 }
        if pillar_result == "ok" then
            for _, row in ipairs(pillar_rows) do
                if doomed.x < 0 or (row.x + row.z) < (doomed.x + doomed.z) then doomed = { x = row.x + 1, z = row.z + 1 } end
            end
        end
        local zone_cap, zone_peak, next_support_poll, min_fraction = 99, 0, 0, 1
        local brace_logged, brace_text = false, nil
        local death_serial = 0
        local _, serial_rows = t.ticklog.rows({ kind = "mark" })
        for _, mark_row in ipairs(serial_rows) do death_serial = mark_row.serial end
        for iteration = 1, 9000 do
            local _, now = t.tick()
            if now - tick0 >= 300 then fight_end = "tick cap, 300 ticks after the click" break end
            if now - tick0 >= next_progress then
                next_progress = next_progress + 60
                local _, deaths_now = t.ticklog.rows({ kind = "npc_death" })
                t.expect("fight.progress" .. (now - tick0), "ok", "tick " .. (now - tick0) .. " after the click, npc deaths " .. #deaths_now
                    .. ", swings " .. swings .. ", casts " .. casts .. ", idle " .. idle .. ", sharks " .. eaten .. ", lowest hp " .. lowest
                    .. ", pillars fallen " .. pillars_fallen .. ", ticks spent equip " .. cost.equip .. " eat " .. cost.eat .. " press " .. cost.press
                    .. " step " .. cost.step .. " wait " .. cost.wait)
            end
            -- the room's own hitpoint readout (::tobwhy only prints what it reads): the first wave's smalls are untouched
            -- six ticks in, and the first big seen is untouched when this loop first sees it
            local big_near = nil
            if big_hp_read == nil and now - tick0 >= 8 then
                local big_result = t.npc.nearest("tob_nylocas_big_incoming_melee_story", 16)
                if big_result ~= "ok" then big_result = t.npc.nearest("tob_nylocas_big_incoming_ranged_story", 16) end
                if big_result ~= "ok" then big_result = t.npc.nearest("tob_nylocas_big_incoming_magic_story", 16) end
                if big_result == "ok" then big_near = true end
            end
            if (small_hp_read == nil and now - tick0 >= 6) or big_near then
                t.cheat("::tobwhy")
                t.ticks(1)
                local _, lines = t.msg.last(60)
                local count_at, count = nil, 0
                for l = 1, #lines do
                    local k = string.match(lines[l].text, "npcs in instance=(%d+)")
                    if k ~= nil then count_at, count = l, tonumber(k) break end
                end
                if count_at ~= nil then
                    local small_top, big_top, support_top = 0, 0, 0
                    for l = count_at + 1, math.min(#lines, count_at + count) do
                        local value = tonumber(string.match(lines[l].text, "hp=(%d+)"))
                        if value ~= nil then
                            if value >= 100 then
                                if value > support_top then support_top = value end
                            elseif value > big_top then
                                big_top = value
                            end
                        end
                    end
                    if small_hp_read == nil then
                        small_hp_read, support_hp_read = big_top, support_top
                    else
                        big_hp_read = big_top
                    end
                end
            end
            -- hitpoints first, every poll: eat at 74 or under (a shark heals 20), up to three in a row, and the eat drops the engagement
            local hp_result, hp = t.skill.read("hitpoints")
            local _, tile = t.world.tile()
            if tile ~= nil and tile.x < 6000 then fight_end = "player died (respawned at " .. tile.x .. ")" break end
            local hp_now = 99
            if hp_result == "ok" and hp.level ~= nil then hp_now = hp.level end
            if hp_now < lowest then lowest = hp_now end
            -- the more copies swinging, the higher the hitpoints are kept: one press can hang for twenty ticks on a covered big
            -- every fighting copy in view (the nearest of each of the six forms), and whether it has held its tile since an earlier
            -- tick: a press aimed at a walking copy answers covered, and the cover recovery then spends twenty to forty ticks
            local fight_rows = {}
            for _, kind in ipairs(fighting_kinds) do
                for index = 1, 3 do
                    local symbol = "tob_nylocas_" .. kind .. "_" .. style_names[index] .. "_story"
                    local near_result, row = t.npc.nearest(symbol, 16)
                    if near_result == "ok" then
                        local pos = row.x .. "," .. row.z
                        local seen = seen_at[row.slot]
                        local stable = seen ~= nil and seen.pos == pos and seen.tick < now
                        if seen == nil or seen.pos ~= pos then seen_at[row.slot] = { pos = pos, tick = now } end
                        fight_rows[#fight_rows + 1] = { row = row, symbol = symbol, style = style_names[index], stable = stable, big = (kind == "big_fighting") }
                    end
                end
            end
            local fight_count = #fight_rows
            local eat_below = 78 + 2 * math.min(fight_count, 3)
            local ate = 0
            local out_of_food = false
            while hp_now <= eat_below and ate < 3 do
                local food_result, food = t.inv.count("shark")
                if food_result == "ok" and food == 0 then out_of_food = true break end
                local before_eat = select(2, t.tick())
                t.player.inv_op("shark", 1)
                eaten = eaten + 1
                ate = ate + 1
                lock = nil
                -- the hitpoint read lags the eat by a tick or two: wait for it to rise, or the next pass eats a second shark at 91
                local hp_before_eat = hp_now
                for _ = 1, 4 do
                    t.ticks(1)
                    hp_result, hp = t.skill.read("hitpoints")
                    if hp_result == "ok" and hp.level ~= nil then hp_now = hp.level end
                    if hp_now > hp_before_eat then break end
                end
                cost.eat = cost.eat + (select(2, t.tick()) - before_eat)
                if hp_now < lowest then lowest = hp_now end
            end
            ring_count = ring_count + 1
            ring[(ring_count - 1) % 60 + 1] = (now - tick0) .. ":" .. hp_now .. ":e" .. ate
            if out_of_food and hp_now < 40 then fight_end = "out of food at hitpoints " .. hp_now break end
            if hp_now < 45 and fight_count >= 1 then fight_end = "left the room at hitpoints " .. hp_now .. " with " .. fight_count .. " copies swinging, after eating" break end
            -- a press that took six ticks or more: what it cost in hitpoints is read here, on the poll after it
            if hang_pending ~= nil then
                hang_events[#hang_events + 1] = hang_pending.text .. " hp " .. hang_pending.hp .. "->" .. hp_now
                hang_pending = nil
            end
            -- a support that fell: from then on the chewers are killed too (the first fall is left to happen, the collapse rows need it)
            if now >= next_pillar_poll then
                next_pillar_poll = now + 6
                local _, fell = t.ticklog.rows({ kind = "npc_death", type = 10790 })
                pillars_fallen = #fell
            end
            -- the support nearest to falling (the lowest health bar) is the one let fall: its chewers are left alone until its bar is
            -- nearly empty, then cut to three, with the hitpoints topped up first, so the collapse hands the player three attackers and
            -- not the thirteen of an unattended fall
            if pillars_fallen == 0 and now >= next_support_poll then
                next_support_poll = now + 4
                local support_result, _, support_rows = t.npc.tiles("tob_nylocas_support_story", 40)
                if support_result == "ok" then
                    min_fraction = 2
                    for _, row in ipairs(support_rows) do
                        local fraction = 1
                        if row.health_ratio ~= nil and row.health_ratio >= 0 and row.health_scale ~= nil and row.health_scale > 0 then
                            fraction = row.health_ratio / row.health_scale
                        end
                        if fraction < min_fraction then
                            min_fraction = fraction
                            doomed = { x = row.x + 1, z = row.z + 1 }
                        end
                    end
                    if min_fraction <= 0.12 then zone_cap = 3 else zone_cap = 99 end
                end
            end
            if zone_cap == 3 and pillars_fallen == 0 and not brace_logged then
                brace_logged = true
                local brace_eaten = 0
                local brace_hp = hp_now
                while brace_hp < 90 and brace_eaten < 4 do
                    local food_result, food = t.inv.count("shark")
                    if food_result == "ok" and food == 0 then break end
                    t.player.inv_op("shark", 1)
                    eaten = eaten + 1
                    brace_eaten = brace_eaten + 1
                    lock = nil
                    t.ticks(1)
                    local brace_result, brace_read = t.skill.read("hitpoints")
                    if brace_result == "ok" and brace_read.level ~= nil then brace_hp = brace_read.level end
                end
                hp_now = brace_hp
                brace_text = "support bar at " .. string.format("%.2f", min_fraction) .. " on tick " .. (now - tick0) .. ": hitpoints topped up to " .. brace_hp
                    .. " with " .. brace_eaten .. " sharks, chewers on it cut to three"
            end
            -- who swings at the player now (read first, so a press on a chewer is dropped for a fresh aggro)
            local target, target_symbol, target_style, on_boss = nil, nil, nil, false
            local first = 1
            for index, name in ipairs(style_names) do
                if name == current then first = index end
            end
            local best_fight = 9999
            for _, f in ipairs(fight_rows) do
                local score = 0
                if not f.big then score = score + 10 end
                if f.style ~= current then score = score + 3 end
                if (skips[f.row.slot] or 0) <= now and score < best_fight then
                    best_fight = score
                    target, target_symbol, target_style = f.row, f.symbol, f.style
                end
            end
            -- is the locked copy still there, and has its press had time to land
            local waiting = false
            if lock ~= nil then
                -- the kill is in the tick log the tick it lands: free the next press without waiting out the corpse
                local _, new_deaths = t.ticklog.rows({ kind = "npc_death", since = death_serial })
                for _, d in ipairs(new_deaths) do
                    if d.serial > death_serial then death_serial = d.serial end
                    if d.slot == lock.world_slot then
                        skips[lock.slot] = now + 4
                        lock = nil
                        break
                    end
                end
            end
            if lock ~= nil then
                local state_result = t.npc.state(lock.symbol, { slot = lock.slot })
                if state_result == "ok" and now - lock.tick < lock.hold and (lock.fight or target == nil) then waiting = true else lock = nil end
            end
            if waiting then
                ring[(ring_count - 1) % 60 + 1] = ring[(ring_count - 1) % 60 + 1] .. "+w"
                t.ticks(1)
                cost.wait = cost.wait + 1
            else
                -- nothing swings at the player: kill the chewers, nearest first, except those on the support let fall
                if target == nil then
                    local candidates, zone_count = {}, 0
                    -- (no cast at a chewer: it costs five ticks, and one such press hung for 53 ticks while its Hagios exploded)
                    for index = 1, 2 do
                        for _, kind in ipairs(incoming_kinds) do
                            local symbol = "tob_nylocas_" .. kind .. "_" .. style_names[index] .. "_story"
                            local tiles_result, _, rows = t.npc.tiles(symbol, 10)
                            if tiles_result == "ok" then
                                for _, row in ipairs(rows) do
                                    local pos = row.x .. "," .. row.z
                                    local seen = seen_at[row.slot]
                                    local stable = seen ~= nil and seen.pos == pos and seen.tick < now
                                    if seen == nil or seen.pos ~= pos then seen_at[row.slot] = { pos = pos, tick = now } end
                                    local in_zone = math.abs(row.x - doomed.x) <= 3 and math.abs(row.z - doomed.z) <= 3
                                    if in_zone then zone_count = zone_count + 1 end
                                    if stable then candidates[#candidates + 1] = { row = row, symbol = symbol, style = style_names[index], zone = in_zone } end
                                end
                            end
                        end
                    end
                    if zone_count > zone_peak then zone_peak = zone_count end
                    local best_score = 9999
                    for _, c in ipairs(candidates) do
                        local score = 0
                        if tile ~= nil then score = math.max(math.abs(c.row.x - tile.x), math.abs(c.row.z - tile.z)) end
                        if c.style ~= current then score = score + 3 end
                        if c.zone and zone_count > zone_cap then score = score - 50 end
                        if (skips[c.row.slot] or 0) <= now and (not c.zone or zone_count > zone_cap or pillars_fallen >= 1) and score < best_score then
                            best_score = score
                            target, target_symbol, target_style = c.row, c.symbol, c.style
                        end
                    end
                end
                if target == nil then
                    for _, style in ipairs({ "melee", "ranged", "magic" }) do
                        local state_result, row = t.npc.state(boss_symbol[style])
                        if state_result == "ok" then target, target_symbol, target_style, on_boss = row, boss_symbol[style], style, true end
                    end
                    if on_boss and boss_seen_tick == nil then boss_seen_tick = now end
                    if on_boss and boss_style ~= target_style then
                        boss_style = target_style
                        boss_forms[#boss_forms + 1] = target_style
                        t.prayer.set(prayer_of[target_style], true)
                    end
                    if not on_boss and boss_style ~= nil then fight_end = "boss gone" break end
                end
                if target == nil then
                    idle = idle + 1
                    t.ticks(1)
                    cost.wait = cost.wait + 1
                else
                    if current ~= target_style then
                        local before_equip = select(2, t.tick())
                        t.player.equip(weapon[target_style])
                        current = target_style
                        cost.equip = cost.equip + (select(2, t.tick()) - before_equip)
                    end
                    local before = select(2, t.tick())
                    local call_result, call_detail
                    if target_style == "magic" then
                        call_result, call_detail = t.player.cast("fire_strike", target_symbol, 1, 2, { slot = target.slot })
                        casts = casts + 1
                    else
                        call_result, call_detail = t.player.attack(target_symbol, 2, 1, { slot = target.slot })
                        swings = swings + 1
                    end
                    results[tostring(call_result)] = (results[tostring(call_result)] or 0) + 1
                    local spent = select(2, t.tick()) - before
                    cost.press = cost.press + spent
                    if spent >= 6 then
                        hang_ticks = hang_ticks + spent
                        hang_pending = { text = (now - tick0) .. ":" .. target_style .. ":" .. tostring(call_result) .. ":" .. spent .. " ticks:"
                            .. (string.find(target_symbol, "_fighting_", 1, true) ~= nil and "swinging" or "chewer"), hp = hp_now }
                    end
                    ring[(ring_count - 1) % 60 + 1] = ring[(ring_count - 1) % 60 + 1] .. "+" .. ({ melee = "a", ranged = "r", magic = "m" })[target_style] .. string.sub(tostring(call_result), 1, 3) .. spent .. (target_symbol ~= nil and string.find(target_symbol, "_fighting_", 1, true) ~= nil and "F" or "C")
                    if #trace < 45 then trace[#trace + 1] = (now - tick0) .. ":" .. string.sub(target_style, 1, 1) .. string.sub(tostring(call_result), 1, 3) .. spent end
                    if on_boss then boss_calls = boss_calls + 1 else waves_calls = waves_calls + 1 end
                    if call_result == "ok" or call_result == "timeout" then
                        local _, world_slot = t.ticklog.slot(target)
                        lock = { world_slot = world_slot, slot = target.slot, symbol = target_symbol, tick = select(2, t.tick()), hold = hold_of[target_style], fight = (target_symbol ~= nil and string.find(target_symbol, "_fighting_", 1, true) ~= nil) or on_boss }
                        if on_boss then lock.hold = 4 end
                    else
                        if string.find(tostring(call_detail), "already under attack", 1, true) ~= nil then under_attack = under_attack + 1 end
                        if #samples < 3 then samples[#samples + 1] = tostring(call_result) .. ": " .. string.sub(tostring(call_detail), 1, 150) end
                        skips[target.slot] = now + 3
                        -- a refused or covered press (stale menu, a cast left armed): step away one tile, then press again
                        local before_step = select(2, t.tick())
                        local _, here = t.world.tile()
                        if here ~= nil then
                            local step_x = here.x + 1
                            if here.x >= 6434 then step_x = here.x - 1 end
                            t.player.walk_to(step_x, here.z, 2)
                        end
                        steps_away = steps_away + 1
                        disarms = disarms + 1
                        cost.step = cost.step + (select(2, t.tick()) - before_step)
                    end
                    if #slow < 40 then slow[#slow + 1] = spent end
                end
            end
        end
        local result_texts = {}
        for name, count in pairs(results) do result_texts[#result_texts + 1] = name .. " " .. count end
        table.sort(result_texts)
        t.check("fight.loop", swings + casts > 20, "melee/ranged swings " .. swings .. ", casts " .. casts .. ", idle polls " .. idle
            .. ", spells put down " .. disarms .. ", sharks eaten " .. eaten .. ", lowest hitpoints " .. lowest .. ", ended by " .. fight_end .. ", "
            .. (select(2, t.tick()) - tick0) .. " ticks after the click; calls on waves " .. waves_calls .. ", on Vasilias " .. boss_calls
            .. "; press results " .. table.concat(result_texts, ", ") .. "; ticks per call " .. table.concat(slow, ",") .. "; ticks spent equip "
            .. cost.equip .. " eat " .. cost.eat .. " press " .. cost.press .. " step " .. cost.step .. " wait " .. cost.wait .. "; pillars fallen "
            .. pillars_fallen .. ", most chewers on the doomed support " .. zone_peak .. ", " .. tostring(brace_text) .. "; last polls (tick:hp:eaten+press) " .. table.concat(ring, " ", ring_count > 60 and (ring_count % 60) + 1 or 1, math.min(ring_count, 60)) .. " " .. (ring_count > 60 and table.concat(ring, " ", 1, ring_count % 60) or "") .. "; trace " .. table.concat(trace, " "))
        local leave_result, leave_detail = t.raid.leave()
        t.check("fight.left", leave_result == "ok", tostring(leave_detail))
        t.ticks(1)
        -- ===== THE SPEC ROWS (measured from our own tick log) =====
        local lane_xz = { ["3281,4248"] = true, ["3281,4249"] = true, ["3295,4233"] = true, ["3296,4233"] = true,
            ["3310,4248"] = true, ["3310,4249"] = true, ["3309,4248"] = true }
        local split_offsets = { ["-1,0"] = true, ["0,0"] = true, ["1,0"] = true, ["0,1"] = true, ["1,1"] = true, ["2,1"] = true }
        local attack_seq = { [7989] = true, [7999] = true, [8004] = true }
        local detonate_seq = { [7992] = true, [8000] = true, [8006] = true }
        -- one kind per query (a kind list is filtered in Lua and runs out of the instruction budget on 40,000 npc_tile rows),
        -- merged back into the log's own order by serial
        local lists, heads = {}, {}
        for _, kind in ipairs({ "npc_spawn", "npc_death", "npc_free", "npc_retype", "npc_anim" }) do
            local _, rows_of_kind = t.ticklog.rows({ kind = kind })
            lists[#lists + 1] = rows_of_kind
            heads[#heads + 1] = 1
            t.ticks(1)
        end
        local stream = {}
        while true do
            local best = nil
            for index = 1, #lists do
                local row = lists[index][heads[index]]
                if row ~= nil and (best == nil or row.serial < lists[best][heads[best]].serial) then best = index end
            end
            if best == nil then break end
            stream[#stream + 1] = lists[best][heads[best]]
            heads[best] = heads[best] + 1
            if #stream % 400 == 0 then t.ticks(1) end
        end
        local room_start, off_x, off_z = nil, nil, nil
        local supports = {}
        for _, r in ipairs(stream) do
            if r.kind == "npc_spawn" and r.type == 10790 then
                if room_start == nil then
                    room_start = r.tick
                    -- the instance is the region moved by whole 64-tile blocks (region 13122 base 3264,4224)
                    off_x = (r.x // 64) * 64 - 3264
                    off_z = (r.z // 64) * 64 - 4224
                end
                if r.tick == room_start then supports[#supports + 1] = { x = r.x - off_x, z = r.z - off_z } end
            end
        end
        local wave_tick_set, wave_ticks = {}, {}
        local cur, lives = {}, {}
        local spawns_at = {}
        local m = {}
        for _, name in ipairs({ "cycle", "first_wave", "lane_tiles", "aggro_swap", "lifetime_small", "lifetime_big",
            "big_explode_splits", "kill_despawn_small", "kill_despawn_big", "split_spawn_tick", "split_count",
            "split_tiles", "attackrate", "flicker_first_switch", "flicker_hold", "flicker_first_wave",
            "max_hit_small_entry", "max_hit_big_entry", "explosion_entry", "pillar_bite_cadence" }) do
            m[name] = {}
        end
        local last_detonate = {}
        local walking_smalls = {}
        local last_attack_tick = {}
        for index, r in ipairs(stream) do
            if index % 250 == 0 then t.ticks(1) end
            if r.kind == "npc_spawn" then
                if r.type >= 10774 and r.type <= 10785 then
                    cur[r.slot] = { slot = r.slot, spawn = r.tick, type = r.type, x = r.x, z = r.z, flips = {}, attacks = {} }
                    local key = (r.x - off_x) .. "," .. (r.z - off_z)
                    if lane_xz[key] then
                        cur[r.slot].lane = true
                        if not wave_tick_set[r.tick] then
                            wave_tick_set[r.tick] = true
                            wave_ticks[#wave_ticks + 1] = r.tick
                        end
                        local lx = m.lane_tiles
                        lx[#lx + 1] = r.x - off_x
                        lx[#lx + 1] = r.z - off_z
                    end
                    spawns_at[r.tick] = spawns_at[r.tick] or {}
                    spawns_at[r.tick][#spawns_at[r.tick] + 1] = { slot = r.slot, type = r.type, x = r.x, z = r.z, lane = cur[r.slot].lane }
                end
            elseif r.kind == "npc_death" then
                if cur[r.slot] ~= nil then cur[r.slot].died = r.tick end
            elseif r.kind == "npc_retype" then
                local c = cur[r.slot]
                if c ~= nil then c.type = r.to_type end
                if c ~= nil and r.from_type >= 10774 and r.from_type <= 10785 and r.to_type >= 10774 and r.to_type <= 10785 then
                    local from_group = (r.from_type - 10774) // 3
                    local to_group = (r.to_type - 10774) // 3
                    if (from_group == 0 and to_group == 2) or (from_group == 1 and to_group == 3) then
                        local list = m.aggro_swap
                        list[#list + 1] = r.tick - c.spawn
                    elseif (r.from_type - 10774) % 3 ~= (r.to_type - 10774) % 3 then
                        c.flips[#c.flips + 1] = r.tick
                    end
                end
            elseif r.kind == "npc_anim" then
                local c = cur[r.slot]
                if c ~= nil then
                    if attack_seq[r.seq] then
                        local previous = c.attacks[#c.attacks]
                        c.attacks[#c.attacks + 1] = r.tick
                        if previous ~= nil and c.type < 10780 then
                            local list = m.attackrate
                            list[#list + 1] = r.tick - previous
                            local bites = m.pillar_bite_cadence
                            bites[#bites + 1] = r.tick - previous
                        end
                    elseif detonate_seq[r.seq] then
                        c.detonated = r.tick
                    end
                end
            else
                local c = cur[r.slot]
                if c ~= nil then
                    c.free, c.fx, c.fz = r.tick, r.x, r.z
                    lives[#lives + 1] = c
                    cur[r.slot] = nil
                end
            end
        end
        local wave_index_of = {}
        for index, tick in ipairs(wave_ticks) do wave_index_of[tick] = index end
        if room_start ~= nil and #wave_ticks > 0 then
            local on_cycle = 0
            for _, tick in ipairs(wave_ticks) do
                if (tick - room_start) % 4 == 0 then on_cycle = on_cycle + 1 end
            end
            if on_cycle == #wave_ticks then m.cycle[1] = 4 else m.cycle[1] = 0 end
            m.first_wave[1] = wave_ticks[1] - room_start
        end
        local flicker_wave = nil
        for index, c in ipairs(lives) do
            if index % 40 == 0 then t.ticks(1) end
            local small = (c.type < 10777) or (c.type >= 10780 and c.type < 10783)
            if #c.flips >= 1 and c.lane then
                local list = m.flicker_first_switch
                list[#list + 1] = c.flips[1] - c.spawn
                if #c.flips >= 2 then
                    local hold = m.flicker_hold
                    hold[#hold + 1] = c.flips[2] - c.flips[1]
                end
                local idx = wave_index_of[c.spawn]
                if idx ~= nil and (flicker_wave == nil or idx < flicker_wave) then flicker_wave = idx end
            end
            if c.died == nil and c.detonated ~= nil then
                local list = small and m.lifetime_small or m.lifetime_big
                list[#list + 1] = c.free - c.spawn
            end
            if c.died ~= nil and small then
                -- the engine delays a nylocas that moved on its killing tick or the one before (CONTENT_BUGS seam3, open): a small
                -- that stood is the spec's 1-2, a walking one is counted apart
                local _, step_rows = t.ticklog.rows({ kind = "npc_tile", slot = c.slot })
                local walked = false
                for _, step in ipairs(step_rows) do
                    if step.tick >= c.died - 1 and step.tick <= c.died then walked = true end
                end
                t.ticks(1)
                local list = walked and walking_smalls or m.kill_despawn_small
                list[#list + 1] = c.free - c.died
            elseif c.died ~= nil then
                local list = m.kill_despawn_big
                list[#list + 1] = c.free - c.died
            end
            if not small and (c.died ~= nil or c.detonated ~= nil) then
                -- (a big freed by the room being torn down, neither killed nor exploded, splits nothing)
                local found = 0
                for _, s in ipairs(spawns_at[c.free] or {}) do
                    local key = (s.x - c.fx) .. "," .. (s.z - c.fz)
                    if not s.lane and split_offsets[key] then
                        found = found + 1
                        local tiles = m.split_tiles
                        tiles[#tiles + 1] = s.x - c.fx
                        tiles[#tiles + 1] = s.z - c.fz
                    end
                end
                local counts = c.died == nil and m.big_explode_splits or m.split_count
                counts[#counts + 1] = found
                local gap = m.split_spawn_tick
                gap[#gap + 1] = 0
            end
        end
        if flicker_wave ~= nil then m.flicker_first_wave[1] = flicker_wave end
        -- the nylocas attack hits on the player, by size, and the explosion hits
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        local anim_by_slot = {}
        for index, r in ipairs(stream) do
            if index % 250 == 0 then t.ticks(1) end
            if r.kind == "npc_anim" and (attack_seq[r.seq] or detonate_seq[r.seq]) then
                anim_by_slot[r.slot] = anim_by_slot[r.slot] or {}
                local list = anim_by_slot[r.slot]
                list[#list + 1] = { tick = r.tick, boom = detonate_seq[r.seq] == true }
            end
        end
        for index, h in ipairs(hits) do
            if index % 40 == 0 then t.ticks(1) end
            if h.npc_type ~= nil and h.npc_type >= 10774 and h.npc_type <= 10785 and h.damage > 0 then
                local last = nil
                for _, a in ipairs(anim_by_slot[h.npc_slot] or {}) do
                    if a.tick <= h.tick and (last == nil or a.tick >= last.tick) then last = a end
                end
                if last ~= nil and h.tick - last.tick <= 6 then
                    local small = (h.npc_type < 10777) or (h.npc_type >= 10780 and h.npc_type < 10783)
                    if last.boom then
                        local list = m.explosion_entry
                        list[#list + 1] = h.damage
                    elseif small then
                        local list = m.max_hit_small_entry
                        list[#list + 1] = h.damage
                    else
                        local list = m.max_hit_big_entry
                        list[#list + 1] = h.damage
                    end
                end
            end
        end
        local specs = {
            { "cycle", "ticks", "4", "B", "exact", "every wave spawned on a cycle tick 0 of the room start" },
            { "first_wave", "ticks", "4", "B", "+-1", "first wave spawn minus the supports' spawn tick, the room start" },
            { "lane_tiles", "tiles", "3281,4248,3281,4249,3295,4233,3296,4233,3310,4248,3310,4249,3309,4248", "B", "exact", "distinct lane spawn x and z" },
            { "aggro_swap", "ticks", "9-11", "B", "range", "incoming to fighting retype, ticks after spawn" },
            { "lifetime_small", "ticks", "52", "B", "exact", "naturally exploded smalls, spawn to npc_free" },
            { "lifetime_big", "ticks", "55", "B", "exact", "naturally exploded bigs, spawn to npc_free" },
            { "big_explode_splits", "count", "2", "B", "exact", "smalls spawned on the despawn tick at the six offsets" },
            { "kill_despawn_small", "ticks", "1-2", "B", "range", "npc_death to npc_free of smalls the player killed standing (walking ones apart, open engine arrive delay)" },
            { "kill_despawn_big", "ticks", "6-7", "B", "range", "npc_death to npc_free of bigs the player killed" },
            { "split_spawn_tick", "ticks", "0", "B", "exact", "split spawn tick minus the parent's npc_free tick" },
            { "split_count", "count", "2", "B", "exact", "splits of bigs the player killed" },
            { "split_tiles", "tiles", "0,0,1,1", "B", "range", "split offsets from the parent's south-west tile" },
            { "attackrate", "ticks", "3", "A", "exact", "gaps between a chewing nylocas' attack animations" },
            { "flicker_first_switch", "ticks", "5", "B", "+-1", "flickers' first style change after spawn" },
            { "flicker_hold", "ticks", "2", "B", "exact", "ticks between a flicker's first and second change" },
            { "flicker_first_wave", "count", "16", "B", "exact", "wave index of the first spawn that changed style" },
            { "max_hit_small_entry", "hp", "1-5", "D", "range", "small nylocas attack hits on the player" },
            { "max_hit_big_entry", "hp", "1-10", "D", "range", "big nylocas attack hits on the player" },
            { "explosion_entry", "hp", "1-8", "D", "range", "hits on the player in the 6 ticks after a detonate animation" },
            { "pillar_bite_cadence", "ticks", "3", "A", "exact", "gaps between a chewing nylocas' bite animations on a support" },
        }
        for _, spec in ipairs(specs) do
            local values = m[spec[1]]
            local seen, distinct = {}, {}
            for _, v in ipairs(values) do
                if not seen[v] then
                    seen[v] = true
                    distinct[#distinct + 1] = v
                end
            end
            table.sort(distinct)
            local text = table.concat(distinct, ",")
            local lo, hi, list = nil, nil, {}
            local spec_value = spec[3]
            local a, b = string.match(spec_value, "^(%d+)%-(%d+)$")
            if a ~= nil then
                lo, hi = tonumber(a), tonumber(b)
            else
                for number in string.gmatch(spec_value, "%d+") do list[#list + 1] = tonumber(number) end
            end
            local ok = #values > 0
            for _, v in ipairs(distinct) do
                local meets = false
                if spec[5] == "exact" then
                    if lo ~= nil then meets = v >= lo and v <= hi else
                        for _, s in ipairs(list) do if s == v then meets = true end end
                    end
                elseif string.sub(spec[5], 1, 2) == "+-" then
                    local n = tonumber(string.sub(spec[5], 3))
                    if lo ~= nil then meets = v >= lo - n and v <= hi + n else
                        for _, s in ipairs(list) do if math.abs(s - v) <= n then meets = true end end
                    end
                else
                    if lo == nil then
                        lo, hi = list[1], list[1]
                        for _, s in ipairs(list) do
                            if s < lo then lo = s end
                            if s > hi then hi = s end
                        end
                    end
                    meets = v >= lo and v <= hi
                end
                if not meets then ok = false end
            end
            local detail
            if #values == 0 then
                detail = "no instance observed in this run (" .. spec[6] .. ")"
            else
                detail = "measured " .. text .. " " .. spec[2] .. ", " .. #values .. " instances, " .. spec[6]
                    .. " (spec " .. spec_value .. " " .. spec[2] .. ", grade " .. spec[4] .. ", tol " .. spec[5] .. ")"
            end
            t.expect("spec.nylocas." .. spec[1], ok and "ok" or "refused", detail)
        end

        -- ===== MORE SPEC ROWS (Entry figures the log and the room's own readout give) =====
        local diff, peak, running = {}, 0, 0
        for _, c in ipairs(lives) do
            diff[c.spawn] = (diff[c.spawn] or 0) + 1
            diff[c.free] = (diff[c.free] or 0) - 1
        end
        local last_tick = select(2, t.tick())
        for tick = 0, last_tick do
            running = running + (diff[tick] or 0)
            if running > peak then peak = running end
        end
        t.check("spec.nylocas.cap_entry", peak > 0, "measured " .. peak .. " count, the most nylocas alive at once in " .. #wave_ticks
            .. " waves (spec ? count, grade E, tol approx); approximation, M90")
        local least, most = nil, nil
        for index = 2, #wave_ticks do
            local gap = wave_ticks[index] - wave_ticks[index - 1]
            if least == nil or gap < least then least = gap end
            if most == nil or gap > most then most = gap end
        end
        if least ~= nil then
            t.check("spec.nylocas.natural_stall_entry", true, "measured " .. least .. "-" .. most .. " ticks, gaps between consecutive waves ("
                .. (#wave_ticks - 1) .. " gaps; the wave check runs every 4) (spec ? ticks, grade E, tol approx); approximation, M91")
        end
        table.sort(supports, function(a, b) if a.z ~= b.z then return a.z < b.z end return a.x < b.x end)
        local anchor_parts = {}
        for _, support in ipairs(supports) do
            anchor_parts[#anchor_parts + 1] = support.x
            anchor_parts[#anchor_parts + 1] = support.z
        end
        t.check("spec.nylocas.pillar_anchors", #supports == 4, "measured " .. table.concat(anchor_parts, ",") .. " tiles, " .. #supports
            .. " supports from the npc_spawn rows on the room's first tick, instance offset removed (spec 3289,4242,3300,4242,3289,4253,3300,4253 tiles, grade B, tol exact)")
        if small_hp_read ~= nil then
            t.check("spec.nylocas.small_hp_entry", small_hp_read == 2, "measured " .. small_hp_read .. " hp, the most hitpoints on any wave nylocas the room's "
                .. "::tobwhy readout listed six ticks after the click, when only untouched smalls stood (spec 2 hp, grade A, tol exact)")
            t.check("spec.nylocas.pillar_hp_entry_unit", support_hp_read ~= nil and support_hp_read > 0, "measured " .. tostring(support_hp_read)
                .. " hp, the support's hitpoints in the same readout (spec 155 hp, grade E, tol approx); approximation, M94")
        end
        if big_hp_read ~= nil then
            t.check("spec.nylocas.big_hp_entry", big_hp_read == 3, "measured " .. big_hp_read .. " hp, the most hitpoints on any nylocas in the readout "
                .. "taken the tick the first big was seen (spec 3 hp, grade A, tol exact)")
        end
        local _, support_hits = t.ticklog.rows({ kind = "hit_npc" })
        local bites, bite_total = 0, 0
        for index, r in ipairs(support_hits) do
            if index % 200 == 0 then t.ticks(1) end
            if r.type == 10790 and r.damage ~= nil then
                bites = bites + 1
                bite_total = bite_total + r.damage
            end
        end
        local bite_mean = 0
        if bites > 0 then bite_mean = bite_total / bites end
        if bites > 0 and bite_mean >= 0.83 and bite_mean <= 0.93 then
            t.check("spec.nylocas.pillar_bite_damage", true, string.format("measured %.2f hp, mean of %d hit_npc rows on the supports (spec 0.83-0.93 hp, grade D, tol range)",
                bite_mean, bites))
        else
            t.check("note.bite_mean", bites > 0, string.format("%d bites on the supports average %.3f hp: a sample this small is not the 2768-bite bracket of the spec, so no spec row", bites, bite_mean))
        end
        local _, shots = t.ticklog.rows({ kind = "projectile" })
        local flight_cycles, flight_distance = nil, nil
        for _, r in ipairs(shots) do
            if r.end_cycle ~= nil and r.start_cycle ~= nil and r.src_x ~= nil and r.dst_x ~= nil then
                flight_cycles = r.end_cycle - r.start_cycle
                flight_distance = math.max(math.abs(r.src_x - r.dst_x), math.abs(r.src_z - r.dst_z))
                break
            end
        end
        if flight_cycles ~= nil then
            t.check("spec.nylocas.ranged_flight", true, "measured " .. flight_cycles .. " cycles, the first nylocas projectile flew at distance " .. flight_distance
                .. " (spec ? cycles, grade E, tol approx); approximation, M96")
        end

        -- self-destruct reach: the Chebyshev distance from the exploding nylocas' footprint to the tile the player held at the end of the tick before
        -- the detonation (an npc acting on T sees the end of T-1)
        local _, player_rows = t.ticklog.rows({ kind = "player_tile" })
        local player_at = {}
        for _, r in ipairs(player_rows) do player_at[r.tick] = r end
        local hit_by = {}
        for _, h in ipairs(hits) do
            if h.damage > 0 then hit_by[tostring(h.npc_slot) .. ":" .. tostring(h.tick)] = true end
        end
        local reach, nearest_miss, booms, landed = nil, nil, 0, 0
        for index, c in ipairs(lives) do
            if index % 60 == 0 then t.ticks(1) end
            if c.detonated ~= nil and c.fx ~= nil then
                local p = player_at[c.detonated - 1]
                if p ~= nil then
                    booms = booms + 1
                    local size = 2
                    if (c.type < 10777) or (c.type >= 10780 and c.type < 10783) then size = 1 end
                    local dx = math.max(c.fx - p.x, p.x - (c.fx + size - 1), 0)
                    local dz = math.max(c.fz - p.z, p.z - (c.fz + size - 1), 0)
                    local dist = math.max(dx, dz)
                    if hit_by[tostring(c.slot) .. ":" .. tostring(c.detonated)] then
                        landed = landed + 1
                        if reach == nil or dist > reach then reach = dist end
                    elseif nearest_miss == nil or dist < nearest_miss then
                        nearest_miss = dist
                    end
                end
            end
        end
        if reach ~= nil then
            t.expect("spec.nylocas.explosion_radius", reach == 2 and "ok" or "refused",
                "measured " .. reach .. " tiles, the farthest tile a detonation hurt the player from (" .. landed .. " of " .. booms
                .. " detonations with the player tile read landed on the detonation tick, none farther than the reach; the nearest one that did not hurt was at " .. tostring(nearest_miss)
                .. ") (spec 2 tiles, grade D, tol exact)")
        end

        -- ===== THE TECHNIQUE ROWS (from the tick log) =====
        local kills = { melee = 0, ranged = 0, magic = 0, fighting = 0, incoming = 0 }
        local _, deaths = t.ticklog.rows({ kind = "npc_death" })
        for _, r in ipairs(deaths) do
            if r.type >= 10774 and r.type <= 10785 then
                local style = ({ "melee", "ranged", "magic" })[(r.type - 10774) % 3 + 1]
                kills[style] = kills[style] + 1
                if r.type >= 10780 then kills.fighting = kills.fighting + 1 else kills.incoming = kills.incoming + 1 end
            end
        end
        local nulled, damaging = 0, 0
        for _, r in ipairs(support_hits) do
            if r.type ~= nil and r.type >= 10774 and r.type <= 10785 then
                if r.damage == 0 then nulled = nulled + 1 else damaging = damaging + 1 end
            end
        end
        t.check("tech.style_kills", kills.melee + kills.ranged + kills.magic > 10, "killed by the player with the weapon of the nylocas' style (whip, "
            .. "shortbow, Fire Strike): melee " .. kills.melee .. ", ranged " .. kills.ranged .. ", magic " .. kills.magic .. "; " .. damaging
            .. " hit_npc rows with damage and " .. nulled .. " at 0 on nylocas")
        t.check("tech.aggro_first", kills.fighting > 0, "kills of the fighting (aggro) forms " .. kills.fighting
            .. " against the incoming forms " .. kills.incoming .. ": the ones that swing at the player are met first")
        t.check("tech.food", eaten > 0 and lowest > 0, "sharks eaten " .. eaten .. ", lowest hitpoints " .. lowest .. ", eaten from 78 (84 with three copies swinging) and below, one at a time, the next only once the hitpoint read had risen")
        local spawned_waves, spawned_total = #wave_ticks, 0
        for _, c in ipairs(lives) do spawned_total = spawned_total + 1 end
        local walking_text = "none"
        if #walking_smalls > 0 then walking_text = table.concat(walking_smalls, ",") end
        local spawned_all = 0
        for _, r in ipairs(stream) do
            if r.kind == "npc_spawn" and r.type >= 10774 and r.type <= 10785 then spawned_all = spawned_all + 1 end
        end
        local hang_text = "none"
        if #hang_events > 0 then hang_text = table.concat(hang_events, "; ", 1, math.min(#hang_events, 5)) end
        t.blocked("driver seam: t.player.attack / t.player.cast run far past their deadline once a press answers covered (this pass: ticks=1, "
            .. #hang_events .. " presses took six ticks or more (" .. hang_ticks .. " ticks in all; first ones: " .. hang_text .. "), inside the cover "
            .. "recovery (combat.lua _combat_press_attack: _npc_cover_recovery, then walk_near; earlier passes of this room measured single ticks=1 presses of 22, 44 and 53 ticks that ended in player.died while four or five bigs swung), and the kill rate it leaves, one nylocas per " .. string.format("%.0f", (select(2, t.tick()) - tick0) / math.max(1, kills.melee + kills.ranged + kills.magic)) .. " ticks, is below the 0.07 a tick at which the aggro forms arrive, while the aggro forms swing unconditionally "
            .. "(tob_nylocas.rs2:1072 rolls 1..max and queues combat_damage_player, no prayer or armour check): " .. (kills.melee + kills.ranged + kills.magic)
            .. " of the " .. spawned_all .. " nylocas spawned in " .. spawned_waves .. " waves were killed in " .. (select(2, t.tick()) - tick0)
            .. " ticks (press results " .. table.concat(result_texts, ", ") .. "), " .. eaten .. " sharks eaten one at a time waiting for the heal to show; "
            .. "four or five swinging bigs hit 5-6 a tick and a shark nets +3 against them, and a support's fall retargets every chewer on it at once "
            .. "(tob_nylocas.rs2:1619, 10-13 seen), so the fight ended by " .. fight_end .. ": Vasilias, the support collapse rows, entry_spawn_total and "
            .. "the nylocas.vasilias_* rows are unreached; walking smalls despawned hp0+" .. walking_text .. " (open engine arrive delay)")
    end,
}
