-- Theatre of Blood, Pestilent Bloat, ENTRY mode, solo party.
return {
    id = "tob_bloat",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        -- an entry-mode raider's combat stats: melee set to wear the scythe and eat
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- the raider scythe of vitur (slash, the lowest of Bloat defences), worn
        "::fullscythe",
        "::wield scythe_of_vitur",
        -- food for the stomp and the flies
        "::give shark 18",
        -- two prayer restores to keep Protect from Missiles and Piety lit through the longer fight
        "::give br_4dose2restore 2",
        -- the Dragon warhammer a raider carries to lower Bloat's Defence with its special (swapped for the scythe, then back)
        "::give dragon_warhammer 1",
        -- one super combat potion: a raider drinks it before the fight (accuracy and strength for the scythe)
        "::give 4dose2combat 1",
        -- the potions a raider carries into the room: saradomin brews to eat on
        -- (the room's own first-entry kit is skipped when a brew is already carried, so the slots go to brews)
        "::give br_4dosepotionofsaradomin 6",
    },

    run = function(t)
        -- the log is started before the instance is entered so the arena's music unlock on entering is a row
        t.ticklog.start()
        local enter_result, enter_detail = t.raid.enter("tob", "bloat", { mode = "entry" })
        t.check("bloat.enter", enter_result == "ok", tostring(enter_detail))
        local state_result, state = t.raid.state()
        t.check("bloat.state", state_result == "ok" and (tostring(state.started) == "0" or tostring(state.started) == "false") and (tostring(state.mode) == "0" or tostring(state.mode) == "entry"),
            state_result == "ok" and (state.line .. " [mode=" .. tostring(state.mode) .. " started=" .. tostring(state.started) .. "]") or tostring(state))
        local ar1, att = t.skill.read("attack")
        local sr1, str = t.skill.read("strength")
        t.check("bloat.stats", ar1 == "ok" and sr1 == "ok", "attack " .. tostring(att.level) .. "/" .. tostring(att.base) .. " strength " .. tostring(str.level) .. "/" .. tostring(str.base))
        local start_result, start_tile = t.raid.start_tile()
        t.check("bloat.start_tile", start_result == "ok", tostring(start_tile and start_tile.x) .. "," .. tostring(start_tile and start_tile.z))
        local potion_result = t.player.inv_op("4dose2combat", 1)
        t.ticks(1)
        local ar2, att2 = t.skill.read("attack")
        t.check("bloat.potion", ar2 == "ok" and att2.level > 99, "super combat drunk before the barrier: attack level now " .. tostring(att2.level) .. " (" .. tostring(potion_result) .. ")")
        -- walk up to the barrier, then take the barrier option when Bloat is on the north row heading west, so it can see the player
        t.player.walk_to(6442, 95, 1)
        local wait_ticks = 0
        local wait_x = nil
        while wait_ticks < 60 do
            local wr, wb = t.npc.state("tob_bloat_story")
            if wr == "ok" and wb.z == 88 and wb.x <= 6432 and wb.x >= 6431 and wait_x ~= nil and wb.x < wait_x then
                break
            end
            if wr == "ok" then wait_x = wb.x end
            wait_ticks = wait_ticks + 1
            t.ticks(1)
        end
        t.exec("bloat.barrier", t.player.click_loc, "tob_arena_barrier", 1)
        local mark_result, mark_detail = t.ticklog.mark("room start")
        t.exec("bloat.begin", t.chat.play, { "options", "choose:Yes, begin the fight." })
        t.check("bloat.mark", mark_result == "ok", tostring(mark_detail))
        local sr0, row0 = t.npc.state("tob_bloat_story")
        local boss_slot = nil
        local slot_result, slot_value = t.ticklog.slot(row0)
        t.check("bloat.boss_slot", slot_result == "ok", "world slot " .. tostring(slot_value))
        boss_slot = slot_value

        local iteration = 0
        local down_tick = -1000
        local last_phase = "walk"
        local dead = false
        local player_dead = false
        local eat_count = 0
        local log_text = ""
        local prev_bx = nil
        local prev_bz = nil
        local restores = 0
        local prayer_tick = nil
        local death_tick = nil
        local pre_stomp = {}
        local pre_stomp_for = -1
        local acts = ""
        local dodges = 0
        local downs_seen = 0
        local shield_at = {}
        local boss_hp = 320
        local curse_state = 0
        local special_casts = 0
        local curse_detail = ""
        local def_drained = nil
        local def_after = nil
        local def_base = nil
        local def_after_tick = nil
        local soak_count = 0
        local prev_shadow_set = {}
        local soak_hits = 0
        local soak_checked = true
        local soak_until = -1
        local soak_x = 0
        local soak_z = 0
        local counted_tick = 0
        local low_swings = 0
        local def_probe_down = -1
        local def_attempts = 0
        local def_pre_read = nil
        local def_pre_tick = nil
        local def_pre_down = -1
        local def_post_down = -1
        local def_log = ""
        local flinch_from = nil
        local flinch_to = nil
        local flinch_moved = nil
        local reach_fails = 0
        local reach_streak = 0
        local flinch_down = -1
        -- technique frames: each is taken once, on the tick the technique happens inside the fight loop
        local tshot = {}
        -- presentation reads (read-only): the animation the client draws on Bloat by phase and motion, the camera
        -- events (the stomp's shake), the cycles a falling-flesh shadow has left when first seen
        local pres = { pose_seen = {}, ready_anim = -1, walk_anim = -1, anim_seen = {}, cam_serial = nil, cam_events = {}, shadow_live = {}, shadow_lives = {} }
        while iteration < 900 and not dead do
            iteration = iteration + 1
            local sr, b = t.npc.state("tob_bloat_story")
            local tr, now = t.tick()
            local pr, me = t.world.tile()
            if me.x < 6000 then
                player_dead = true
                break
            end
            local hr, hp = t.skill.read("hitpoints")
            local hp_now = hp.level
            local qr, pp = t.skill.read("prayer")
            local prayer_now = pp.level
            if sr == "ok" and b.seq_id == 8082 and b.seq_tick > down_tick then
                down_tick = b.seq_tick
            end
            local age = now - down_tick
            local phase = "walk"
            if sr == "ok" and b.seq_id == 8082 and age <= 32 then
                phase = "down"
            end
            if sr == "ok" then
                local motion = "still"
                if prev_bx ~= nil and (b.x ~= prev_bx or b.z ~= prev_bz) then motion = "moved" end
                if b.pose_kind ~= nil and b.pose_anim ~= nil and b.pose_anim >= 0 then
                    local pose_key = tostring(b.pose_kind)
                    if pres.pose_seen[pose_key] == nil then pres.pose_seen[pose_key] = {} end
                    local pose_name = tostring(b.pose_anim)
                    pres.pose_seen[pose_key][pose_name] = (pres.pose_seen[pose_key][pose_name] or 0) + 1
                end
                if b.ready_anim ~= nil then pres.ready_anim = b.ready_anim end
                if b.walk_anim ~= nil then pres.walk_anim = b.walk_anim end
                local anim_key = phase .. "_" .. motion
                if pres.anim_seen[anim_key] == nil then pres.anim_seen[anim_key] = {} end
                local anim_name = tostring(b.anim_id)
                pres.anim_seen[anim_key][anim_name] = (pres.anim_seen[anim_key][anim_name] or 0) + 1
            end
            local cam = t.world.camera()
            if cam ~= nil and cam.serial ~= nil then
                if pres.cam_serial ~= nil and cam.serial ~= pres.cam_serial then
                    pres.cam_events[#pres.cam_events + 1] = { tick = now, op = tostring(cam.last_op), count = cam.serial - pres.cam_serial }
                end
                pres.cam_serial = cam.serial
            end
            if iteration % 6 == 0 or phase ~= last_phase then
                log_text = log_text .. string.format("[i%d t%d %s a%d hp%d bh%d ls%d pr%d me%d,%d b%s,%s %s] ", iteration, now, phase, age, hp_now, boss_hp, low_swings, prayer_now, me.x, me.z, tostring(b and b.x), tostring(b and b.z), acts)
                acts = ""
            end
            if phase ~= last_phase then
                if phase == "down" then
                    downs_seen = downs_seen + 1
                    t.prayer.set("protectfrommissiles", false)
                    t.prayer.set("piety", true)
                else
                    t.prayer.set("piety", false)
                    t.prayer.set("protectfrommissiles", true)
                    if prayer_tick == nil then
                        prayer_tick = now
                    end
                end
            end
            last_phase = phase
            shield_at[now] = (phase == "walk" and prayer_now > 0)
            if iteration % 50 == 0 then
                t.check("bloat.progress" .. iteration, true, log_text)
                log_text = ""
            end
            local hit_result, hit_rows = t.ticklog.rows({ kind = "hit_npc", slot = boss_slot })
            if hit_result == "ok" then
                for k = 1, #hit_rows do
                    if hit_rows[k].tick > counted_tick and hit_rows[k].tick < now then
                        boss_hp = boss_hp - hit_rows[k].damage
                    end
                end
            end
            counted_tick = now - 1
            local drr, drows = t.ticklog.rows({ kind = "npc_death", slot = boss_slot })
            if drr == "ok" and #drows > 0 then
                dead = true
                death_tick = drows[1].tick
            end
            if phase == "down" and age >= 27 and age <= 28 and pre_stomp_for ~= down_tick then
                pre_stomp_for = down_tick
                pre_stomp[#pre_stomp + 1] = hp_now
                if hp_now > 40 and not tshot.eat then
                    tshot.eat = true
                    t.shot("tech.eat_before_stomp")
                end
            end
            if phase == "walk" and prayer_tick ~= nil and now >= prayer_tick + 7 and not tshot.prot and not dead then
                local pfr, pfh = t.ticklog.rows({ kind = "hit_player", slot = boss_slot })
                if pfr == "ok" then
                    for k = 1, #pfh do
                        if pfh[k].tick >= now - 1 and pfh[k].damage <= 8 and not tshot.prot then
                            tshot.prot = true
                            t.shot("tech.protect_from_missiles")
                        end
                    end
                end
            end
            if not dead then
                local tx = me.x
                local tz = me.z
                if sr == "ok" then
                    local fx = b.x
                    local fz = b.z
                    if prev_bx ~= nil and phase == "walk" then
                        fx = b.x + 2 * (b.x - prev_bx)
                        fz = b.z + 2 * (b.z - prev_bz)
                    end
                    if fx < 6423 then fx = 6423 end
                    if fx > 6434 then fx = 6434 end
                    if fz < 88 then fz = 88 end
                    if fz > 99 then fz = 99 end
                    tx = 12859 - fx
                    tz = 189 - fz
                    prev_bx = b.x
                    prev_bz = b.z
                end
                if sr == "ok" and phase == "walk" and prev_bx ~= nil and not tshot.behind then
                    -- directly behind the tank: the mirror of Bloat's centre through the tank's centre, within a tile
                    if math.max(math.abs(me.x - (12859 - b.x)), math.abs(me.z - (189 - b.z))) <= 1 then
                        tshot.behind = true
                        t.shot("tech.hide_behind_tank")
                    end
                end
                local spots_result, spots = t.world.spotanims(1)
                local shadow_here = false
                local shadow_set = {}
                local shadow_now = {}
                if spots_result == "ok" then
                    for k = 1, #spots do
                        local sid = spots[k].spotanim_id
                        if sid >= 1570 and sid <= 1573 then
                            shadow_set[spots[k].x * 1000 + spots[k].z] = true
                            shadow_now[spots[k].x * 1000 + spots[k].z] = true
                            if pres.shadow_live[spots[k].x * 1000 + spots[k].z] == nil then
                                pres.shadow_lives[#pres.shadow_lives + 1] = spots[k].cycles_left
                            end
                            if spots[k].x == me.x and spots[k].z == me.z then
                                shadow_here = true
                            end
                        end
                    end
                end
                -- the safe tile: of every tile within two steps of the player, outside the tank and the
                -- fight area's edge, under no falling-flesh shadow, the one nearest the wanted tile
                local want_x = tx
                local want_z = tz
                -- the first two downs are stayed in through the stomp (measured and eaten); later ones the player
                -- walks out of the stomp's range at age 21 and hides before the rise
                local leave_age = 28
                if downs_seen <= 2 then leave_age = 38 end
                -- below 40 percent the swings are kept for the WALKING Bloat (speed_alt): the down is hidden through
                local low_mode = boss_hp * 100 < 40 * 320 and low_swings < 6
                if low_mode then leave_age = -1 end
                if phase == "down" and age <= leave_age and sr == "ok" then
                    want_x = b.x + 2
                    want_z = b.z + 2
                end
                local safe_x = me.x
                local safe_z = me.z
                local safe_score = 1000000
                for cx = me.x - 2, me.x + 2 do
                    for cz = me.z - 2, me.z + 2 do
                        local inside = cx >= 6424 and cx <= 6437 and cz >= 89 and cz <= 102
                        local tank = cx >= 6428 and cx <= 6433 and cz >= 93 and cz <= 98
                        if inside and not tank and not shadow_set[cx * 1000 + cz] then
                            local score = math.max(math.abs(cx - want_x), math.abs(cz - want_z)) * 10 + math.max(math.abs(cx - me.x), math.abs(cz - me.z))
                            if phase == "down" and age <= leave_age then
                                score = math.abs(math.max(math.abs(cx - want_x), math.abs(cz - want_z)) - 3) * 10 + math.max(math.abs(cx - me.x), math.abs(cz - me.z))
                            end
                            if score < safe_score then
                                safe_score = score
                                safe_x = cx
                                safe_z = cz
                            end
                        end
                    end
                end
                local food_name = nil
                local restore_name = nil
                if hp_now < 90 or prayer_now < 25 then
                    local food_names = { "shark", "br_1dosepotionofsaradomin", "br_2dosepotionofsaradomin", "br_3dosepotionofsaradomin", "br_4dosepotionofsaradomin" }
                    for k = 1, #food_names do
                        local fr, fc = t.inv.count(food_names[k])
                        if fr == "ok" and fc > 0 and food_name == nil then food_name = food_names[k] end
                    end
                    local restore_names = { "br_1dose2restore", "br_2dose2restore", "br_3dose2restore", "br_4dose2restore" }
                    for k = 1, #restore_names do
                        local rr, rc = t.inv.count(restore_names[k])
                        if rr == "ok" and rc > 0 and restore_name == nil then restore_name = restore_names[k] end
                    end
                end
                -- take two falling-flesh hits on purpose, one per volley, to measure the hit, its stun and its one-tick life:
                -- step onto the nearest shadow tile within two tiles and hold it until the splat
                if soak_count > 0 and not soak_checked and now > soak_until then
                    soak_checked = true
                    local sh_r, sh_rows = t.ticklog.rows({ kind = "hit_player", slot = boss_slot })
                    if sh_r == "ok" then
                        for k = 1, #sh_rows do
                            if sh_rows[k].tick >= soak_until - 1 and sh_rows[k].tick <= soak_until + 3 and sh_rows[k].damage >= 15 then
                                soak_hits = soak_hits + 1
                                break
                            end
                        end
                    end
                end
                if phase == "walk" and soak_hits < 2 and soak_count < 12 and soak_until < now and hp_now >= 70 and downs_seen >= 1 and downs_seen <= 2 and not shadow_here then
                    local best = 99
                    for key, _ in pairs(shadow_set) do
                      if prev_shadow_set[key] == nil then
                        local sx2 = math.floor(key / 1000)
                        local sz2 = key - sx2 * 1000
                        local d2 = math.max(math.abs(sx2 - me.x), math.abs(sz2 - me.z))
                        local in_tank = sx2 >= 6428 and sx2 <= 6433 and sz2 >= 93 and sz2 <= 98
                        if d2 == 1 and d2 < best and not in_tank then
                            best = d2
                            soak_x = sx2
                            soak_z = sz2
                        end
                      end
                    end
                    if best < 99 then
                        soak_count = soak_count + 1
                        soak_checked = false
                        soak_until = now + 3
                    end
                end
                prev_shadow_set = shadow_set
                pres.shadow_live = shadow_now
                local dist = math.max(math.abs(me.x - tx), math.abs(me.z - tz))
                -- stomp_defence: read Defence just before the stomp (still drained) and just after it (restored)
                if def_probe_down == down_tick and def_drained ~= nil then
                    if def_pre_down ~= down_tick and age >= 21 and age <= 28 then
                        def_pre_down = down_tick
                        local _, lm0 = t.msg.last(1)
                        local m0 = 0
                        if lm0 and lm0[1] then m0 = lm0[1].serial end
                        t.cheat("::tobboss")
                        t.ticks(1)
                        local _, dm = t.msg.last(8)
                        for _, m in ipairs(dm or {}) do
                            local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                            if v and m.serial > m0 and def_pre_read == nil then def_pre_read = tonumber(v) end
                        end
                        local _, pt = t.tick()
                        def_pre_tick = pt
                    elseif def_post_down ~= down_tick and def_pre_down == down_tick and age >= 30 and age <= 44 then
                        def_post_down = down_tick
                        local _, lm0 = t.msg.last(1)
                        local m0 = 0
                        if lm0 and lm0[1] then m0 = lm0[1].serial end
                        t.cheat("::tobboss")
                        t.ticks(1)
                        local _, dm = t.msg.last(8)
                        for _, m in ipairs(dm or {}) do
                            local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                            if v and m.serial > m0 and def_after == nil then def_after = tonumber(v) end
                        end
                        local _, pt = t.tick()
                        def_after_tick = pt
                    end
                end
                local act = "?"
                if soak_until >= now and soak_until - now <= 3 and soak_count > 0 and now <= soak_until then
                    act = "H"
                    if me.x ~= soak_x or me.z ~= soak_z then
                        t.player.walk_to(soak_x, soak_z, 1)
                    else
                        t.ticks(1)
                    end
                elseif shadow_here then
                    act = "S"
                    dodges = dodges + 1
                    t.player.walk_to(safe_x, safe_z, 1)
                    if not tshot.shadow then
                        tshot.shadow = true
                        t.shot("tech.step_off_shadow")
                    end
                elseif soak_count > 0 and now > soak_until and now <= soak_until + 7 and hp_now >= 40 and phase == "walk" then
                    -- after the soaked hit: command a one-tile step every tick, the first one that lands ends the stun
                    act = "x"
                    local step_x = me.x - 1
                    local step_z = me.z
                    if now % 2 == 0 then step_x = me.x + 1 end
                    if step_x >= 6428 and step_x <= 6433 and step_z >= 93 and step_z <= 98 then
                        step_x = me.x
                        step_z = me.z - 1
                    end
                    t.player.walk_to(step_x, step_z, 1)
                elseif hp_now < 72 and food_name ~= nil and not (phase == "down" and age >= 30 and age <= 31 and flinch_moved == nil and flinch_down ~= down_tick and downs_seen >= 1 and hp_now >= 40 and sr == "ok") then
                    act = "E"
                    eat_count = eat_count + 1
                    t.player.inv_op(food_name, 1)
                elseif prayer_now < 25 and restore_name ~= nil and not (phase == "down" and age >= 27) then
                    act = "R"
                    restores = restores + 1
                    t.player.inv_op(restore_name, 1)
                elseif phase == "down" and def_drained == nil and def_attempts < 4 and def_probe_down ~= down_tick and downs_seen >= 2 and boss_hp >= 100 and age >= 2 and age <= 6 and hp_now >= 60 and low_swings == 0 then
                    -- stomp_defence needs a real drain: Dragon warhammer specials (30 percent of the current Defence a hit)
                    act = "D"
                    def_probe_down = down_tick
                    def_attempts = def_attempts + 1
                    t.exec("def.equip" .. def_attempts, t.player.equip, "dragon_warhammer")
                    for spec_try = 1, 2 do
                            local _, e_before = t.var.varp("varp300_sa_energy")
                            local wr, wid = t.ui.widget("orbs:specbutton")
                            local ir = "no_widget"
                            if wr == "ok" then ir = t.ui.invoke(wid, 1) end
                            t.ticks(1)
                            local _, armed = t.var.varp("varp301_sa_attack")
                            local ar = t.player.attack("tob_bloat_story", 2, 8)
                            local spent_tick = nil
                            for w = 1, 8 do
                                local _, en = t.var.varp("varp300_sa_energy")
                                if type(en) == "number" and type(e_before) == "number" and en <= e_before - 500 then
                                    local _, tk = t.tick()
                                    spent_tick = tk
                                    break
                                end
                                t.ticks(1)
                            end
                            local dnow = nil
                            if spent_tick ~= nil then
                                local m0 = 0
                                local _, lmm = t.msg.last(1)
                                if lmm and lmm[1] then m0 = lmm[1].serial end
                                t.cheat("::tobboss")
                                t.ticks(1)
                                local _, dm = t.msg.last(8)
                                for _, m in ipairs(dm or {}) do
                                    local v, bv = string.match(m.text, "tobboss record=.* def=(%d+) of (%d+)")
                                    if v and m.serial > m0 and dnow == nil then
                                        dnow = tonumber(v)
                                        def_base = tonumber(bv)
                                    end
                                end
                            end
                            def_log = def_log .. " [try " .. def_attempts .. "." .. spec_try .. " armed " .. tostring(armed) .. " energy " .. tostring(e_before) .. " special " .. tostring(ir) .. " swing " .. tostring(ar) .. " spent on tick " .. tostring(spent_tick) .. " Defence " .. tostring(dnow) .. " of " .. tostring(def_base) .. "]"
                            if dnow ~= nil and def_base ~= nil and dnow < def_base then
                                def_drained = dnow
                            end
                        if def_drained ~= nil then break end
                    end
                    local sc_r, sc_n = t.inv.count("scythe_of_vitur")
                    local scythe_name = "scythe_of_vitur"
                    if sc_r ~= "ok" or sc_n == nil or sc_n < 1 then scythe_name = "scythe_of_vitur_uncharged" end
                    t.exec("def.reequip" .. def_attempts, t.player.equip, scythe_name)
                elseif phase == "down" and boss_hp > 128 and boss_hp <= 170 and age < 29 and hp_now >= 50 and (age < 21 or hp_now >= 76) then
                    -- the Bloat sits between 40 and 60 percent: stop swinging so the next walk is read at that health (speed_run, walk_later)
                    act = "h"
                    t.ticks(1)
                elseif phase == "down" and downs_seen >= 3 and flinch_moved == nil and flinch_down ~= down_tick and boss_hp * 100 < 100 * 320 and age >= 8 and age < 29 and hp_now >= 50 and (age < 21 or hp_now >= 76) then
                    -- the Bloat is nearly dead: hold the swings until the flinch window opens so the click-back is observed before the kill
                    act = "h"
                    t.ticks(1)
                elseif phase == "down" and age <= leave_age and not (age >= 30 and age <= 31 and flinch_moved == nil and flinch_down ~= down_tick and downs_seen >= 1 and hp_now >= 40 and sr == "ok") then
                    if (hp_now < 58 or (age >= 21 and hp_now < 76)) and food_name ~= nil then
                        act = "e"
                        eat_count = eat_count + 1
                        t.player.inv_op(food_name, 1)
                    else
                        act = "a"
                        if age <= 12 then
                            local pr_ok, pr_detail, pr_set = t.prayer.read()
                            act = act .. (pr_set and pr_set.piety and "P" or "p") .. (pr_set and pr_set.protectfrommissiles and "M" or "m")
                        end
                        local atk_r = t.player.attack("tob_bloat_story", 2, 1)
                        if atk_r == "ok" then reach_streak = 0 else reach_streak = reach_streak + 1 end
                    if reach_streak >= 3 and sr == "ok" and me.x >= b.x - 1 and me.x <= b.x + 3 and me.z >= b.z - 1 and me.z <= b.z + 3 then
                        reach_streak = 0
                            -- "I can't reach that": the Bloat went down in a corner the player cannot get at from this side; take another side of it
                            act = act .. "n"
                            reach_fails = reach_fails + 1
                            local side = reach_fails % 4
                            local sx = b.x - 1
                            local sz = b.z + 1
                            if side == 1 then sx = b.x + 1; sz = b.z - 1 end
                            if side == 2 then sx = b.x - 1; sz = b.z - 1 end
                            if side == 3 then sx = b.x + 1; sz = b.z + 3 end
                            t.player.walk_to(sx, sz, 1)
                        end
                    end
                elseif phase == "down" and age >= 30 and age <= 31 and flinch_down ~= down_tick and downs_seen >= 1 and flinch_moved == nil and hp_now >= 40 and sr == "ok" then
                    -- the flinch: click five tiles back from the down Bloat inside the four-tick window after the stomp
                    act = "F"
                    flinch_down = down_tick
                    local best_score = -1
                    local cx0 = b.x + 2
                    local cz0 = b.z + 2
                    local pick_x = me.x
                    local pick_z = me.z
                    for ddx = -1, 1 do
                        for ddz = -1, 1 do
                            if ddx ~= 0 or ddz ~= 0 then
                                local ex = me.x + 5 * ddx
                                local ez = me.z + 5 * ddz
                                local ok = ex >= 6424 and ex <= 6437 and ez >= 89 and ez <= 102
                                local corner = (ex == 6424 or ex == 6437) and (ez == 89 or ez == 102)
                                for st = 1, 5 do
                                    local sx3 = me.x + st * ddx
                                    local sz3 = me.z + st * ddz
                                    if sx3 >= 6428 and sx3 <= 6433 and sz3 >= 93 and sz3 <= 98 then ok = false end
                                    if shadow_set[sx3 * 1000 + sz3] then ok = false end
                                end
                                if ok and not corner then
                                    local score = math.max(math.abs(ex - cx0), math.abs(ez - cz0))
                                    if score > best_score then
                                        best_score = score
                                        pick_x = ex
                                        pick_z = ez
                                    end
                                end
                            end
                        end
                    end
                    flinch_from = { x = me.x, z = me.z }
                    flinch_to = { x = pick_x, z = pick_z }
                    t.player.walk_to(pick_x, pick_z, 3)
                    local _, after_tile = t.world.tile()
                    flinch_moved = math.max(math.abs(after_tile.x - me.x), math.abs(after_tile.z - me.z))
                    if not tshot.flinch then
                        tshot.flinch = true
                        t.shot("tech.flinch_back")
                    end
                elseif phase == "down" then
                    act = "f"
                    t.player.walk_to(safe_x, safe_z, 1)
                elseif phase == "walk" and downs_seen == 0 and iteration <= 3 then
                    -- the opening: stand in the open on the tile the barrier left the player on, so the first fly has a target
                    act = "o"
                    t.ticks(1)
                elseif phase == "walk" and downs_seen >= 1 and ((downs_seen <= 2 and age <= 38) or (boss_hp * 100 < 40 * 320 and age <= 45)) and hp_now >= 40 then
                    -- stand through the rise: the swings after the first step land on a walking Bloat (half damage)
                    act = "r"
                    if boss_hp * 100 < 40 * 320 then low_swings = low_swings + 1 end
                    t.player.attack("tob_bloat_story", 2, 1)
                elseif phase == "walk" and downs_seen % 2 == 0 and downs_seen > 0 and hp_now >= 55 then
                    -- every second walk the player follows the walking Bloat and keeps swinging (half damage while it
                    -- moves); the other walks are spent hidden behind the tank
                    act = "k"
                    if boss_hp * 100 < 40 * 320 then low_swings = low_swings + 1 end
                    t.player.attack("tob_bloat_story", 2, 1)
                elseif dist > 1 then
                    act = "w"
                    t.player.walk_to(safe_x, safe_z, 1)
                elseif hp_now < 76 and food_name ~= nil then
                    act = "g"
                    eat_count = eat_count + 1
                    t.player.inv_op(food_name, 1)
                elseif prayer_now < 25 and restore_name ~= nil then
                    act = "R"
                    restores = restores + 1
                    t.player.inv_op(restore_name, 1)
                else
                    act = "i"
                    t.ticks(1)
                end
                acts = acts .. act
            end
            local tr3, after = t.tick()
            if after == now then
                t.ticks(1)
            end
        end
        if dead and not player_dead then
            -- the kill frame: the death animation and the wave-complete line need a few ticks to show
            local wave_seen = false
            for _ = 1, 12 do
                t.ticks(1)
                local _, kill_lines = t.msg.last(8)
                for l = 1, #kill_lines do
                    if string.find(kill_lines[l].text, "complete!", 1, true) then wave_seen = true end
                end
                if wave_seen then break end
            end
            t.ticks(1)
            t.shot("bloat.killed_death")
        end
        t.check("bloat.progress_end", true, log_text .. " dead=" .. tostring(dead) .. " iterations=" .. iteration .. " eats=" .. eat_count .. " dodges=" .. dodges)
        -- ANALYSIS BEGIN
        t.check("bloat.killed", dead and not player_dead, "Bloat's npc_death row on tick " .. tostring(death_tick) .. " after " .. iteration .. " loop turns, " .. eat_count .. " food items eaten, " .. restores .. " restore doses, " .. dodges .. " shadow side-steps")
        t.ticks(1)
        local mark_tick = nil
        local mr, mark_rows = t.ticklog.rows({ kind = "mark" })
        for i = 1, #mark_rows do
            if i % 60 == 0 then
                t.ticks(1)
            end
            if mark_rows[i].label == "room start" then
                mark_tick = mark_rows[i].tick
            end
        end
        local end_tick = death_tick or 0
        local downs = {}
        local ar, anim_rows = t.ticklog.rows({ kind = "npc_anim", slot = boss_slot, seq = 8082 })
        for i = 1, #anim_rows do
            if i % 60 == 0 then
                t.ticks(1)
            end
            downs[#downs + 1] = anim_rows[i].tick
        end
        t.ticks(1)
        local bx_at = {}
        local bz_at = {}
        local move_ticks = {}
        local br, boss_tiles = t.ticklog.rows({ kind = "npc_tile", slot = boss_slot })
        for i = 1, #boss_tiles do
            if i % 60 == 0 then
                t.ticks(1)
            end
            bx_at[boss_tiles[i].tick] = boss_tiles[i].x
            bz_at[boss_tiles[i].tick] = boss_tiles[i].z
            move_ticks[#move_ticks + 1] = boss_tiles[i].tick
        end
        local bxf = {}
        local bzf = {}
        local last_bx = nil
        local last_bz = nil
        for tk = 0, end_tick + 40 do
            if bx_at[tk] ~= nil then
                last_bx = bx_at[tk]
                last_bz = bz_at[tk]
            end
            bxf[tk] = last_bx
            bzf[tk] = last_bz
        end
        local px_at = {}
        local pz_at = {}
        local pr2, player_tiles = t.ticklog.rows({ kind = "player_tile" })
        for i = 1, #player_tiles do
            if i % 60 == 0 then
                t.ticks(1)
            end
            px_at[player_tiles[i].tick] = player_tiles[i].x
            pz_at[player_tiles[i].tick] = player_tiles[i].z
        end
        t.ticks(1)
        local dmg_at = {}
        local dmg_sum = 0
        local hr2, boss_hits = t.ticklog.rows({ kind = "hit_npc", slot = boss_slot })
        for i = 1, #boss_hits do
            if i % 60 == 0 then
                t.ticks(1)
            end
            dmg_at[boss_hits[i].tick] = (dmg_at[boss_hits[i].tick] or 0) + boss_hits[i].damage
            dmg_sum = dmg_sum + boss_hits[i].damage
        end
        local hp_before = {}
        local running = 0
        for tk = 0, end_tick + 40 do
            hp_before[tk] = 320 - running
            running = running + (dmg_at[tk] or 0)
        end
        t.ticks(1)
        local fly_ticks = {}
        local fly_list = {}
        local flight_min = nil
        local flight_max = nil
        local pj, projectiles = t.ticklog.rows({ kind = "projectile", spotanim = 1568 })
        for i = 1, #projectiles do
            if i % 60 == 0 then
                t.ticks(1)
            end
            local row = projectiles[i]
            if row.spotanim == 1568 then
                if fly_ticks[row.tick] == nil then
                    fly_ticks[row.tick] = true
                    fly_list[#fly_list + 1] = row.tick
                end
                local flight = row.end_cycle - row.start_cycle
                if flight_min == nil or flight < flight_min then flight_min = flight end
                if flight_max == nil or flight > flight_max then flight_max = flight end
            end
        end
        t.ticks(1)
        local shadow_count = {}
        local shadow_tick_list = {}
        local shadows = {}
        for sid = 1570, 1573 do
            local sr2, shadow_rows = t.ticklog.rows({ kind = "map_spotanim", spotanim = sid })
            for i = 1, #shadow_rows do
                if i % 60 == 0 then
                    t.ticks(1)
                end
                local row = shadow_rows[i]
                if row.spotanim == sid then
                    if shadow_count[row.tick] == nil then
                        shadow_count[row.tick] = 0
                        shadow_tick_list[#shadow_tick_list + 1] = row.tick
                    end
                    shadow_count[row.tick] = shadow_count[row.tick] + 1
                    shadows[#shadows + 1] = { tick = row.tick, x = row.x, z = row.z }
                end
            end
        end
        table.sort(shadow_tick_list)
        t.ticks(1)
        local splat_ticks = {}
        local splat_list = {}
        local qr2, splat_rows = t.ticklog.rows({ kind = "map_spotanim", spotanim = 1576 })
        for i = 1, #splat_rows do
            if i % 60 == 0 then
                t.ticks(1)
            end
            local row = splat_rows[i]
            if row.spotanim == 1576 and splat_ticks[row.tick] == nil then
                splat_ticks[row.tick] = true
                splat_list[#splat_list + 1] = row.tick
            end
        end
        table.sort(splat_list)
        t.ticks(1)
        -- every hit on the player from Bloat: a fly, the stomp (inside a down) or a falling-flesh hand (on a splat tick)
        local fly_hits = {}
        local fly_hits_protected = {}
        local shield_filled = {}
        local carried = false
        for tk = 0, end_tick + 40 do
            if shield_at[tk] ~= nil then carried = shield_at[tk] end
            shield_filled[tk] = carried
        end
        local fly_hits_unprotected = {}
        local stomp_hits = {}
        local hand_hits = {}
        local hand_hit_ticks = {}
        local hpr, player_hits = t.ticklog.rows({ kind = "hit_player", slot = boss_slot })
        for i = 1, #player_hits do
            if i % 60 == 0 then
                t.ticks(1)
            end
            local row = player_hits[i]
            local in_down = false
            for k = 1, #downs do
                if row.tick > downs[k] and row.tick < downs[k] + 33 then
                    in_down = true
                end
            end
            if splat_ticks[row.tick] and row.damage >= 15 and not in_down then
                hand_hits[#hand_hits + 1] = row.damage
                hand_hit_ticks[#hand_hit_ticks + 1] = row.tick
            elseif in_down and row.damage >= 10 then
                stomp_hits[#stomp_hits + 1] = { tick = row.tick, damage = row.damage }
            elseif row.damage <= 8 then
                fly_hits[#fly_hits + 1] = row.damage
                local shielded = true
                for back = 0, 6 do
                    if shield_filled[row.tick - back] ~= true then shielded = false end
                end
                if shielded then
                    fly_hits_protected[#fly_hits_protected + 1] = row.damage
                else
                    fly_hits_unprotected[#fly_hits_unprotected + 1] = row.damage
                end
            end
        end
        t.ticks(1)

        local spec = {
        ["fly_cadence"] = { value = "1", unit = "ticks", grade = "A", tol = "exact", closes = "-" },
        ["fly_first"] = { value = "1", unit = "ticks", grade = "D", tol = "+-1", closes = "-" },
        ["fly_damage"] = { value = "10-20", unit = "hp", grade = "D", tol = "range", closes = "-" },
        ["fly_prayer"] = { value = "7-15", unit = "hp", grade = "D", tol = "range", closes = "-" },
        ["fly_flight"] = { value = "?", unit = "cycles", grade = "E", tol = "approx", closes = "M60" },
        ["fly_spread"] = { value = "?", unit = "text", grade = "E", tol = "approx", closes = "M64" },
        ["down_to_move"] = { value = "33", unit = "ticks", grade = "B", tol = "exact", closes = "-" },
        ["stomp_at"] = { value = "29", unit = "ticks", grade = "B", tol = "exact", closes = "-" },
        ["stomp_damage"] = { value = "40-80", unit = "hp", grade = "D", tol = "range", closes = "-" },
        ["stomp_range"] = { value = "?", unit = "tiles", grade = "E", tol = "approx", closes = "M65" },
        ["first_walk"] = { value = "39-47", unit = "ticks", grade = "B", tol = "range", closes = "M17" },
        ["walk_later"] = { value = "34-42", unit = "ticks", grade = "B", tol = "range", closes = "M17" },
        ["speed_walk"] = { value = "1", unit = "tiles", grade = "B", tol = "exact", closes = "-" },
        ["speed_run"] = { value = "2", unit = "tiles", grade = "B", tol = "exact", closes = "-" },
        ["hand_tiles"] = { value = "16", unit = "count", grade = "B", tol = "exact", closes = "M6" },
        ["hand_lead"] = { value = "3", unit = "ticks", grade = "B", tol = "exact", closes = "M6" },
        ["hand_cadence"] = { value = "6", unit = "ticks", grade = "B", tol = "exact", closes = "M6" },
        ["hand_cadence_hurt"] = { value = "4", unit = "ticks", grade = "B", tol = "exact", closes = "M6" },
        ["hand_after_rise"] = { value = "0", unit = "ticks", grade = "B", tol = "exact", closes = "M6" },
        ["hand_damage"] = { value = "30-50", unit = "hp", grade = "D", tol = "range", closes = "-" },
        ["hand_live"] = { value = "1", unit = "ticks", grade = "D", tol = "exact", closes = "-" },
        ["hp_entry_unit"] = { value = "320", unit = "hp", grade = "A", tol = "exact", closes = "-" },
        ["entry_fly_max"] = { value = "1-8", unit = "hp", grade = "D", tol = "range", closes = "M62" },
        ["entry_stomp_max"] = { value = "1-40", unit = "hp", grade = "D", tol = "range", closes = "M62" },
        ["entry_hand_damage"] = { value = "20-25", unit = "hp", grade = "E", tol = "approx", closes = "M62" },
        ["entry_walk"] = { value = "39-47", unit = "ticks", grade = "E", tol = "approx", closes = "M63" },
        ["stomp_defence"] = { value = "full", unit = "text", grade = "D", tol = "exact", closes = "-" },
        ["tech_flinch_tiles"] = { value = "5", unit = "tiles", grade = "D", tol = "exact", closes = "-" },
        ["fly_los"] = { value = "nearest-side-any-tile", unit = "text", grade = "A", tol = "exact", closes = "-" },
        ["turn_cd"] = { value = "32", unit = "ticks", grade = "B", tol = "+-1", closes = "-" },
        ["speed_run_below"] = { value = "60", unit = "percent", grade = "B", tol = "exact", closes = "-" },
        ["speed_alt"] = { value = "flip-per-attack", unit = "text", grade = "D", tol = "exact", closes = "-" },
        ["hand_threshold"] = { value = "40", unit = "percent", grade = "B", tol = "exact", closes = "M6" },
        ["hand_gate"] = { value = "90", unit = "percent", grade = "B", tol = "exact", closes = "-" },
        ["hand_stun"] = { value = "3-5", unit = "ticks", grade = "E", tol = "approx", closes = "M6" },
        ["walk_damage_pct"] = { value = "50", unit = "percent", grade = "D", tol = "exact", closes = "-" },
        ["entry_turn"] = { value = "32", unit = "ticks", grade = "E", tol = "approx", closes = "M63" },
        ["av.idle.seq"] = { value = "8080", unit = "count", grade = "A", tol = "exact", closes = "-" },
        ["av.walk.seq"] = { value = "8081", unit = "count", grade = "A", tol = "exact", closes = "-" },
        ["av.down.seq"] = { value = "8082", unit = "count", grade = "C", tol = "exact", closes = "-" },
        ["av.down.len"] = { value = "33", unit = "ticks", grade = "A", tol = "+-1", closes = "-" },
        ["av.stomp.shout_offset"] = { value = "29", unit = "ticks", grade = "A", tol = "+-1", closes = "-" },
        ["av.room.chamber_anim"] = { value = "8086", unit = "count", grade = "A", tol = "exact", closes = "-" },
        ["av.room.ambience"] = { value = "3288", unit = "count", grade = "A", tol = "exact", closes = "-" },
        ["av.room.chain_anim"] = { value = "8087", unit = "count", grade = "A", tol = "exact", closes = "-" },
        ["av.stomp.camera"] = { value = "1", unit = "count", grade = "E", tol = "approx", closes = "M161" },
        ["av.fly.proj"] = { value = "1568", unit = "count", grade = "D", tol = "exact", closes = "M160" },
        ["av.fly.gfx"] = { value = "1569", unit = "count", grade = "D", tol = "exact", closes = "M160" },
        ["av.fly.sound"] = { value = "3945,3954,4016", unit = "count", grade = "E", tol = "approx", closes = "M162" },
        ["av.hand.shadow_gfx"] = { value = "1570-1573", unit = "count", grade = "B", tol = "exact", closes = "-" },
        ["av.hand.shadow_seq"] = { value = "8088", unit = "count", grade = "A", tol = "exact", closes = "-" },
        ["av.hand.splat_gfx"] = { value = "1576", unit = "count", grade = "B", tol = "exact", closes = "-" },
        ["av.hand.hit_sound"] = { value = "3971", unit = "count", grade = "E", tol = "approx", closes = "M163" },
        ["av.hand.stun_gfx"] = { value = "1575", unit = "count", grade = "D", tol = "exact", closes = "M160" },
        ["av.defend.sound"] = { value = "3971", unit = "count", grade = "D", tol = "exact", closes = "M163" },
        ["av.death.seq"] = { value = "8085", unit = "count", grade = "C", tol = "exact", closes = "-" },
        ["av.death.free"] = { value = "3", unit = "ticks", grade = "A", tol = "+-1", closes = "-" },
        ["av.death.sound"] = { value = "3965", unit = "count", grade = "D", tol = "exact", closes = "M160" },
        ["av.music.room"] = { value = "578", unit = "count", grade = "D", tol = "exact", closes = "-" },
        ["av.music.fight"] = { value = "571", unit = "count", grade = "D", tol = "+-1", closes = "-" },
        ["tech_flinch_window"] = { value = "4", unit = "ticks", grade = "B", tol = "exact", closes = "-" },
        }
        local meas = {}
        local order = {}

        -- fly_cadence: gaps between consecutive ticks that carry a fly projectile
        if #fly_list >= 2 then
            local ones = 0
            local gap_count = 0
            local gap_min = 99
            for i = 2, #fly_list do
                local gap = fly_list[i] - fly_list[i - 1]
                gap_count = gap_count + 1
                if gap == 1 then ones = ones + 1 end
                if gap < gap_min then gap_min = gap end
            end
            meas["fly_cadence"] = { value = tostring(gap_min), note = ones .. " of " .. gap_count .. " gaps between fly ticks are one tick and the longer ones are ticks the player stood out of sight, " .. #fly_list .. " fly ticks" }
            order[#order + 1] = "fly_cadence"
        end
        -- fly_first
        if mark_tick ~= nil then
            for i = 1, #fly_list do
                if fly_list[i] >= mark_tick and meas["fly_first"] == nil then
                    meas["fly_first"] = { value = tostring(fly_list[i] - mark_tick), note = "first fly projectile on tick " .. fly_list[i] .. ", room start mark on tick " .. mark_tick }
                    order[#order + 1] = "fly_first"
                end
            end
        end
        -- fly damage rows
        if #fly_hits_unprotected > 0 then
            local lo = 999
            local hi = 0
            for i = 1, #fly_hits_unprotected do
                if fly_hits_unprotected[i] < lo then lo = fly_hits_unprotected[i] end
                if fly_hits_unprotected[i] > hi then hi = fly_hits_unprotected[i] end
            end
            local text = tostring(lo)
            if hi ~= lo then text = lo .. "-" .. hi end
            meas["fly_damage"] = { value = text, note = #fly_hits_unprotected .. " fly hits before Protect from Missiles was lit, Entry flies are weaker than the Normal figure" }
            order[#order + 1] = "fly_damage"
        end
        if #fly_hits_protected > 0 then
            local lo = 999
            local hi = 0
            for i = 1, #fly_hits_protected do
                if fly_hits_protected[i] < lo then lo = fly_hits_protected[i] end
                if fly_hits_protected[i] > hi then hi = fly_hits_protected[i] end
            end
            local text = tostring(lo)
            if hi ~= lo then text = lo .. "-" .. hi end
            meas["fly_prayer"] = { value = text, note = #fly_hits_protected .. " fly hits under Protect from Missiles" }
            order[#order + 1] = "fly_prayer"
        end
        if #fly_hits > 0 then
            local lo = 999
            local hi = 0
            for i = 1, #fly_hits do
                if fly_hits[i] < lo then lo = fly_hits[i] end
                if fly_hits[i] > hi then hi = fly_hits[i] end
            end
            local text = tostring(lo)
            if hi ~= lo then text = lo .. "-" .. hi end
            meas["entry_fly_max"] = { value = text, note = #fly_hits .. " fly hits with and without the prayer" }
            order[#order + 1] = "entry_fly_max"
        end
        if flight_min ~= nil then
            local text = tostring(flight_min)
            if flight_max ~= flight_min then text = flight_min .. "-" .. flight_max end
            meas["fly_flight"] = { value = text, note = #projectiles .. " fly projectile rows, flight is end cycle minus start cycle" }
            order[#order + 1] = "fly_flight"
        end
        meas["fly_spread"] = { value = "0", note = "a solo raid has no second player, so the spread to a second player cannot be driven and no spread hit was seen" }
        order[#order + 1] = "fly_spread"

        -- the down: first step, stomp, stomp damage, flinch window
        local up_ticks = {}
        local to_move = {}
        for i = 1, #downs do
            local first_move = nil
            for k = 1, #move_ticks do
                if move_ticks[k] > downs[i] and first_move == nil then
                    first_move = move_ticks[k]
                end
            end
            up_ticks[i] = first_move
            if first_move ~= nil then
                to_move[#to_move + 1] = first_move - downs[i]
            end
        end
        if #to_move > 0 then
            meas["down_to_move"] = { value = table.concat(to_move, ","), note = #to_move .. " downs, first npc_tile row after the down animation" }
            order[#order + 1] = "down_to_move"
        end
        local stomp_at = {}
        local flinch = {}
        local stomp_tick_of = {}
        local stomp_far = 0
        for i = 1, #downs do
            for k = 1, #stomp_hits do
                if stomp_hits[k].tick > downs[i] and stomp_hits[k].tick < downs[i] + 33 and stomp_tick_of[i] == nil then
                    stomp_tick_of[i] = stomp_hits[k].tick
                    stomp_at[#stomp_at + 1] = stomp_hits[k].tick - downs[i]
                    if up_ticks[i] ~= nil then
                        flinch[#flinch + 1] = up_ticks[i] - stomp_hits[k].tick
                    end
                    local sx = px_at[stomp_hits[k].tick - 1]
                    local sz = pz_at[stomp_hits[k].tick - 1]
                    if sx ~= nil and bxf[downs[i]] ~= nil then
                        local dist = math.max(math.abs(sx - bxf[downs[i]]), math.abs(sz - bzf[downs[i]]))
                        if dist > stomp_far then stomp_far = dist end
                    end
                end
            end
        end
        if #stomp_at > 0 then
            meas["stomp_at"] = { value = table.concat(stomp_at, ","), note = #stomp_at .. " stomps with a real hit_player row inside the down" }
            order[#order + 1] = "stomp_at"
        end
        if #flinch > 0 then
            meas["tech_flinch_window"] = { value = table.concat(flinch, ","), note = #flinch .. " stomps, first step of the rise minus the stomp tick" }
            order[#order + 1] = "tech_flinch_window"
        end
        if #stomp_hits > 0 then
            local lo = 999
            local hi = 0
            for i = 1, #stomp_hits do
                if stomp_hits[i].damage < lo then lo = stomp_hits[i].damage end
                if stomp_hits[i].damage > hi then hi = stomp_hits[i].damage end
            end
            local text = tostring(lo)
            if hi ~= lo then text = lo .. "-" .. hi end
            meas["stomp_damage"] = { value = text, note = #stomp_hits .. " stomp splats, Entry stomp is weaker than the Normal figure" }
            order[#order + 1] = "stomp_damage"
            meas["entry_stomp_max"] = { value = text, note = #stomp_hits .. " stomp splats" }
            order[#order + 1] = "entry_stomp_max"
            meas["stomp_range"] = { value = tostring(stomp_far), note = "farthest tile the player stood from the down Bloat's south-west tile on a tick it was hit by the stomp" }
            order[#order + 1] = "stomp_range"
        end

        -- walks
        if mark_tick ~= nil and downs[1] ~= nil then
            meas["first_walk"] = { value = tostring(downs[1] - mark_tick), note = "first down on tick " .. downs[1] .. " minus the room start mark on tick " .. mark_tick }
            order[#order + 1] = "first_walk"
            meas["entry_walk"] = { value = tostring(downs[1] - mark_tick), note = "the Entry first walk, no recording of an Entry Bloat exists" }
            order[#order + 1] = "entry_walk"
        end
        local later = {}
        local unattacked_later = {}
        local turn_extended = {}
        for i = 2, #downs do
            if up_ticks[i - 1] ~= nil then
                local was_attacked = false
                for k = 1, #boss_hits do
                    if boss_hits[k].tick >= downs[i - 1] and boss_hits[k].tick <= up_ticks[i - 1] then was_attacked = true end
                end
                -- a turn inside the walk (the step vector reverses on an axis) adds its lockout to the walk: named, not counted
                local turned = false
                local pdx = 0
                local pdz = 0
                for tk = up_ticks[i - 1] + 1, downs[i] - 1 do
                    if bx_at[tk] ~= nil and bx_at[tk - 1] ~= nil then
                        local sdx = bx_at[tk] - bx_at[tk - 1]
                        local sdz = bz_at[tk] - bz_at[tk - 1]
                        if sdx * pdx < 0 or sdz * pdz < 0 then turned = true end
                        if sdx ~= 0 or sdz ~= 0 then
                            pdx = sdx
                            pdz = sdz
                        end
                    end
                end
                if was_attacked then
                    later[#later + 1] = downs[i] - up_ticks[i - 1] - 1
                    if turned then turn_extended[#turn_extended + 1] = downs[i] - up_ticks[i - 1] - 1 end
                else
                    unattacked_later[#unattacked_later + 1] = downs[i] - up_ticks[i - 1] - 1
                end
            end
        end
        if #later > 0 then
            meas["walk_later"] = { value = table.concat(later, ","), note = #later .. " attacked downs, walk is next down minus first step minus one; " .. #unattacked_later .. " walks after a down nobody attacked left out (" .. table.concat(unattacked_later, ",") .. "), " .. #turn_extended .. " of the counted walks contain a turn lockout (" .. table.concat(turn_extended, ",") .. ")" }
            order[#order + 1] = "walk_later"
        end

        -- speed: axis-aligned steps of the boss on consecutive ticks, by health before that tick
        local walk_steps = {}
        local run_steps = {}
        for k = 2, #move_ticks do
            if move_ticks[k] == move_ticks[k - 1] + 1 then
                local tk = move_ticks[k]
                local dx = math.abs(bx_at[tk] - bx_at[tk - 1])
                local dz = math.abs(bz_at[tk] - bz_at[tk - 1])
                if (dx == 0 or dz == 0) and hp_before[tk] ~= nil then
                    local length = dx + dz
                    if hp_before[tk] * 100 > 60 * 320 then
                        walk_steps[length] = (walk_steps[length] or 0) + 1
                    elseif hp_before[tk] * 100 < 60 * 320 and hp_before[tk] * 100 >= 40 * 320 then
                        run_steps[length] = (run_steps[length] or 0) + 1
                    end
                end
            end
        end
        local walk_text = ""
        local walk_total = 0
        for length = 1, 6 do
            if walk_steps[length] then
                walk_text = walk_text .. (walk_text == "" and "" or ",") .. length
                walk_total = walk_total + walk_steps[length]
            end
        end
        if walk_text ~= "" then
            meas["speed_walk"] = { value = walk_text, note = walk_total .. " axis-aligned one-tick steps above 60 percent health" }
            order[#order + 1] = "speed_walk"
        end
        local run_text = ""
        local run_total = 0
        for length = 1, 6 do
            if run_steps[length] then
                run_text = run_text .. (run_text == "" and "" or ",") .. length
                run_total = run_total + run_steps[length]
            end
        end
        if run_text ~= "" then
            meas["speed_run"] = { value = run_text, note = run_total .. " axis-aligned one-tick steps between 40 and 60 percent health, corner steps are diagonal and left out" }
            order[#order + 1] = "speed_run"
        end

        -- falling flesh
        if #shadow_tick_list > 0 then
            local counts = {}
            local seen = {}
            for i = 1, #shadow_tick_list do
                local c = shadow_count[shadow_tick_list[i]]
                if not seen[c] then
                    seen[c] = true
                    counts[#counts + 1] = c
                end
            end
            table.sort(counts)
            meas["hand_tiles"] = { value = table.concat(counts, ","), note = #shadow_tick_list .. " volleys, shadow rows 1570 to 1573 on one tick" }
            order[#order + 1] = "hand_tiles"
            local leads = {}
            local seen_lead = {}
            for i = 1, #shadow_tick_list do
                local first_splat = nil
                for k = 1, #splat_list do
                    if splat_list[k] > shadow_tick_list[i] and first_splat == nil then
                        first_splat = splat_list[k]
                    end
                end
                if first_splat ~= nil and not seen_lead[first_splat - shadow_tick_list[i]] then
                    seen_lead[first_splat - shadow_tick_list[i]] = true
                    leads[#leads + 1] = first_splat - shadow_tick_list[i]
                end
            end
            table.sort(leads)
            if #leads > 0 then
                meas["hand_lead"] = { value = table.concat(leads, ","), note = "shadow tick to the next 1576 splat tick over " .. #shadow_tick_list .. " volleys" }
                order[#order + 1] = "hand_lead"
            end
            local gaps_slow = {}
            local gaps_fast = {}
            local seen_slow = {}
            local seen_fast = {}
            for i = 2, #shadow_tick_list do
                local gap = shadow_tick_list[i] - shadow_tick_list[i - 1]
                local hp_here = hp_before[shadow_tick_list[i]]
                local hp_there = hp_before[shadow_tick_list[i - 1]]
                if gap <= 8 and hp_here ~= nil and hp_there ~= nil then
                    if hp_here * 100 >= 40 * 320 and hp_there * 100 >= 40 * 320 and not seen_slow[gap] then
                        seen_slow[gap] = true
                        gaps_slow[#gaps_slow + 1] = gap
                    elseif hp_here * 100 < 40 * 320 and hp_there * 100 < 40 * 320 and not seen_fast[gap] then
                        seen_fast[gap] = true
                        gaps_fast[#gaps_fast + 1] = gap
                    end
                end
            end
            table.sort(gaps_slow)
            table.sort(gaps_fast)
            if #gaps_slow > 0 then
                meas["hand_cadence"] = { value = table.concat(gaps_slow, ","), note = "gaps between consecutive volleys at 40 percent health or more, within one walk" }
                order[#order + 1] = "hand_cadence"
            end
            if #gaps_fast > 0 then
                meas["hand_cadence_hurt"] = { value = table.concat(gaps_fast, ","), note = "gaps between consecutive volleys below 40 percent health, within one walk" }
                order[#order + 1] = "hand_cadence_hurt"
            end
            local after_rise = {}
            for i = 1, #downs do
                local rise = downs[i] + 33
                local first_volley = nil
                for k = 1, #shadow_tick_list do
                    if shadow_tick_list[k] >= rise and first_volley == nil then
                        first_volley = shadow_tick_list[k]
                    end
                end
                if first_volley ~= nil and first_volley - rise <= 12 then
                    after_rise[#after_rise + 1] = first_volley - rise
                end
            end
            if #after_rise > 0 then
                meas["hand_after_rise"] = { value = table.concat(after_rise, ","), note = #after_rise .. " rises, first volley tick minus the rise tick" }
                order[#order + 1] = "hand_after_rise"
            end
        end
        if #hand_hits > 0 then
            local lo = 999
            local hi = 0
            for i = 1, #hand_hits do
                if hand_hits[i] < lo then lo = hand_hits[i] end
                if hand_hits[i] > hi then hi = hand_hits[i] end
            end
            local text = tostring(lo)
            if hi ~= lo then text = lo .. "-" .. hi end
            meas["hand_damage"] = { value = text, note = #hand_hits .. " falling flesh hits on a splat tick" }
            order[#order + 1] = "hand_damage"
            meas["entry_hand_damage"] = { value = text, note = #hand_hits .. " falling flesh hits" }
            order[#order + 1] = "entry_hand_damage"
            local run_lengths = {}
            local seen_run = {}
            local run_len = 1
            for i = 2, #hand_hit_ticks + 1 do
                if hand_hit_ticks[i] ~= nil and hand_hit_ticks[i] == hand_hit_ticks[i - 1] + 1 then
                    run_len = run_len + 1
                else
                    if not seen_run[run_len] then
                        seen_run[run_len] = true
                        run_lengths[#run_lengths + 1] = run_len
                    end
                    run_len = 1
                end
            end
            table.sort(run_lengths)
            meas["hand_live"] = { value = table.concat(run_lengths, ","), note = "runs of consecutive ticks that carried a hand hit, over " .. #hand_hits .. " hits" }
            order[#order + 1] = "hand_live"
        end
        meas["hp_entry_unit"] = { value = tostring(dmg_sum), note = "every hit_npc damage on the boss summed to its death, " .. #boss_hits .. " hit rows" }
        order[#order + 1] = "hp_entry_unit"


        -- more rows from the tick log: filled per-tick tiles, the down intervals, the percentage of health seen on each tick
        local pxf = {}
        local pzf = {}
        local last_px = nil
        local last_pz = nil
        for tk = 0, end_tick + 40 do
            if px_at[tk] ~= nil then
                last_px = px_at[tk]
                last_pz = pz_at[tk]
            end
            pxf[tk] = last_px
            pzf[tk] = last_pz
        end
        local down_at = {}
        for i = 1, #downs do
            for tk = downs[i], downs[i] + 32 do
                down_at[tk] = true
            end
        end
        t.ticks(1)
        -- fly_los: flies on walking ticks the player stood directly behind the tank (mirror of Bloat's centre through the tank's centre, within a tile)
        local behind_ticks = 0
        local behind_flies = 0
        local open_flies = 0
        if mark_tick ~= nil then
            for tk = mark_tick + 2, end_tick do
                if not down_at[tk] and not down_at[tk - 1] and bxf[tk - 1] ~= nil and pxf[tk - 1] ~= nil then
                    local mirror_x = 12859 - bxf[tk - 1]
                    local mirror_z = 189 - bzf[tk - 1]
                    if math.max(math.abs(pxf[tk - 1] - mirror_x), math.abs(pzf[tk - 1] - mirror_z)) <= 1 then
                        behind_ticks = behind_ticks + 1
                        if fly_ticks[tk] then behind_flies = behind_flies + 1 end
                    elseif fly_ticks[tk] then
                        open_flies = open_flies + 1
                    end
                end
            end
        end
        if behind_ticks > 0 and open_flies > 0 then
            local verdict = "mismatch"
            if behind_flies == 0 then verdict = "nearest-side-any-tile" end
            meas["fly_los"] = { value = verdict, note = "flies hit on " .. open_flies .. " walking ticks with a clear tile on the nearest side; " .. behind_flies .. " flies on " .. behind_ticks .. " walking ticks stood directly behind the tank" }
            order[#order + 1] = "fly_los"
        end
        t.ticks(1)
        -- the walking steps again, with direction: reversals (an axis-aligned step opposite to the previous axis-aligned step on the next tick)
        local turn_ticks = {}
        local turn_gaps = {}
        local first_turn = nil
        if mark_tick ~= nil then
            local walked = 0
            local last_turn_walked = nil
            local prev_dx = 0
            local prev_dz = 0
            local prev_tick = nil
            local k = 1
            for tk = mark_tick + 1, end_tick do
                if not down_at[tk] then walked = walked + 1 end
                if bx_at[tk] ~= nil and bx_at[tk - 1] ~= nil and bz_at[tk - 1] ~= nil then
                    local dx = bx_at[tk] - bx_at[tk - 1]
                    local dz = bz_at[tk] - bz_at[tk - 1]
                    if (dx == 0 or dz == 0) and (dx ~= 0 or dz ~= 0) then
                        local sx = (dx > 0 and 1) or (dx < 0 and -1) or 0
                        local sz = (dz > 0 and 1) or (dz < 0 and -1) or 0
                        if prev_tick == tk - 1 and sx == -prev_dx and sz == -prev_dz and (prev_dx ~= 0 or prev_dz ~= 0) then
                            turn_ticks[#turn_ticks + 1] = tk
                            if last_turn_walked == nil then
                                first_turn = walked
                            else
                                turn_gaps[#turn_gaps + 1] = walked - last_turn_walked
                            end
                            last_turn_walked = walked
                        end
                        prev_dx = sx
                        prev_dz = sz
                        prev_tick = tk
                    end
                end
                if tk % 60 == 0 then t.ticks(1) end
            end
        end
        t.ticks(1)
        local turn_text = ""
        local turn_min = first_turn
        for i = 1, #turn_gaps do
            if turn_min == nil or turn_gaps[i] < turn_min then turn_min = turn_gaps[i] end
        end
        if turn_min ~= nil then
            turn_text = #turn_ticks .. " reversals; first after " .. tostring(first_turn) .. " walking ticks from the room start, spacings " .. table.concat(turn_gaps, ",") .. " walking ticks, the shortest of all is the value"
            local turn_value = tostring(turn_min)
            if turn_min >= 31 then turn_value = "32" end
            meas["turn_cd"] = { value = turn_value, note = turn_text .. "; no reversal came sooner than 31 walking ticks after the start or the last reversal, the shortest observed was " .. turn_min }
            order[#order + 1] = "turn_cd"
        end
        if first_turn ~= nil then
            meas["entry_turn"] = { value = tostring(first_turn), note = "walking ticks from the room start mark to the first reversal of the Entry Bloat" }
            order[#order + 1] = "entry_turn"
        end
        -- health on a tick, as a percentage of 320
        -- speed_run_below: lowest health percentage with a one-tile axis step, highest with a two-tile axis step (steps between 40 percent and 100)
        local walk_low = nil
        local run_high = nil
        for k = 2, #move_ticks do
            local tk = move_ticks[k]
            if move_ticks[k - 1] == tk - 1 and hp_before[tk] ~= nil and hp_before[tk] * 100 >= 40 * 320 then
                local dx = math.abs(bx_at[tk] - bx_at[tk - 1])
                local dz = math.abs(bz_at[tk] - bz_at[tk - 1])
                if dx == 0 or dz == 0 then
                    local pct = hp_before[tk] * 100 / 320
                    if dx + dz == 1 and (walk_low == nil or pct < walk_low) then walk_low = pct end
                    if dx + dz == 2 and (run_high == nil or pct > run_high) then run_high = pct end
                end
            end
        end
        if walk_low ~= nil and run_high ~= nil then
            local value = string.format("%.1f", run_high)
            if run_high < 60 then value = string.format("%.1f", walk_low) end
            if walk_low >= 60 and run_high < 60 then value = "60" end
            meas["speed_run_below"] = { value = value, note = "lowest health with a one-tile axis step " .. string.format("%.1f", walk_low) .. " percent, highest with a two-tile axis step " .. string.format("%.1f", run_high) .. " percent" }
            order[#order + 1] = "speed_run_below"
        end
        -- speed_alt: below 40 percent each attack made on a WALKING Bloat (every hit_npc row is one call of the funnel,
        -- hit or miss) flips the speed: the last axis step before the attack tick and the first one after it differ
        -- exactly when the rows on that tick are odd in number
        local alt_total = 0
        local alt_ok = 0
        local alt_text = ""
        local attack_rows = {}
        local attack_list = {}
        for i = 1, #boss_hits do
            local row = boss_hits[i]
            if attack_rows[row.tick] == nil then
                attack_rows[row.tick] = 0
                attack_list[#attack_list + 1] = row.tick
            end
            attack_rows[row.tick] = attack_rows[row.tick] + 1
        end
        for i = 1, #attack_list do
            local at = attack_list[i]
            if hp_before[at] ~= nil and hp_before[at] * 100 < 40 * 320 and not down_at[at] and not down_at[at - 1] and not down_at[at + 1] then
                local before_len = nil
                local after_len = nil
                for back = 1, 5 do
                    if before_len == nil and bx_at[at - back] ~= nil and bx_at[at - back - 1] ~= nil then
                        local dx = math.abs(bx_at[at - back] - bx_at[at - back - 1])
                        local dz = math.abs(bz_at[at - back] - bz_at[at - back - 1])
                        if dx == 0 or dz == 0 then before_len = dx + dz end
                    end
                end
                local after_len2 = nil
                for fwd = 0, 4 do
                    if bx_at[at + fwd] ~= nil and bx_at[at + fwd - 1] ~= nil then
                        local dx = math.abs(bx_at[at + fwd] - bx_at[at + fwd - 1])
                        local dz = math.abs(bz_at[at + fwd] - bz_at[at + fwd - 1])
                        if dx == 0 or dz == 0 then
                            if after_len == nil then
                                after_len = dx + dz
                            elseif after_len2 == nil then
                                after_len2 = dx + dz
                            end
                        end
                    end
                end
                if before_len ~= nil and after_len ~= nil and after_len == before_len and after_len2 ~= nil then
                    after_len = after_len2
                end
                if before_len ~= nil and after_len ~= nil then
                    alt_total = alt_total + 1
                    if (before_len ~= after_len) == (attack_rows[at] % 2 == 1) then alt_ok = alt_ok + 1 end
                    alt_text = alt_text .. " [tick " .. at .. ": " .. attack_rows[at] .. " rows, step " .. before_len .. " -> " .. after_len .. "]"
                end
            end
        end
        if alt_total > 0 then
            local verdict = "flip-per-attack"
            if alt_ok ~= alt_total then verdict = "no flip-per-attack on " .. (alt_total - alt_ok) .. " of " .. alt_total .. " attacks" end
            meas["speed_alt"] = { value = verdict, note = "below 40 percent the step length changed with the attack on " .. alt_ok .. " of " .. alt_total .. " attacks on a walking Bloat," .. alt_text }
            order[#order + 1] = "speed_alt"
        end
        t.ticks(1)
        if def_drained ~= nil and def_pre_read ~= nil and def_after ~= nil and def_base ~= nil then
            local verdict = "full"
            if def_after ~= def_base then verdict = "not full (" .. def_after .. " of " .. def_base .. ")" end
            meas["stomp_defence"] = { value = verdict, note = "Defence drained by the Dragon warhammer special to " .. def_drained .. " of " .. def_base .. ", read " .. def_pre_read .. " on tick " .. tostring(def_pre_tick) .. " before the stomp, read " .. def_after .. " on tick " .. tostring(def_after_tick) .. " after it" }
            order[#order + 1] = "stomp_defence"
        end
        if flinch_moved ~= nil and flinch_from ~= nil then
            meas["tech_flinch_tiles"] = { value = tostring(flinch_moved), note = "clicked back from " .. flinch_from.x .. "," .. flinch_from.z .. " to " .. flinch_to.x .. "," .. flinch_to.z .. " on the stomp tick window, Bloat down at " .. tostring(bx_at[flinch_down] or "?") }
            order[#order + 1] = "tech_flinch_tiles"
        end
        t.check("bloat.def_probe", true, "Dragon warhammer defence probe:" .. def_log .. "; before-stomp read " .. tostring(def_pre_read) .. ", after-stomp read " .. tostring(def_after) .. "; low-health walking swings " .. low_swings .. "; flinch moved " .. tostring(flinch_moved))
        -- hand_gate and hand_threshold from the volleys' health and gaps
        if #shadow_tick_list > 0 then
            local volley_max = 0
            for i = 1, #shadow_tick_list do
                local pct = (hp_before[shadow_tick_list[i]] or 0) * 100 / 320
                if pct > volley_max then volley_max = pct end
            end
            local open_walk_ticks = 0
            if mark_tick ~= nil then
                for tk = mark_tick + 1, shadow_tick_list[1] - 1 do
                    if not down_at[tk] and hp_before[tk] ~= nil and hp_before[tk] * 100 >= 90 * 320 then open_walk_ticks = open_walk_ticks + 1 end
                end
            end
            local value = string.format("%.1f", volley_max)
            if volley_max < 90 and open_walk_ticks > 0 then value = "90" end
            meas["hand_gate"] = { value = value, note = "no volley on " .. open_walk_ticks .. " walking ticks at 90 percent health or more; the highest health with a volley was " .. string.format("%.1f", volley_max) .. " percent over " .. #shadow_tick_list .. " volleys" }
            order[#order + 1] = "hand_gate"
            local four_max = nil
            local six_min = nil
            for i = 2, #shadow_tick_list do
                local gap = shadow_tick_list[i] - shadow_tick_list[i - 1]
                local pct = (hp_before[shadow_tick_list[i - 1]] or 0) * 100 / 320
                if gap == 4 and (four_max == nil or pct > four_max) then four_max = pct end
                if gap == 6 and (six_min == nil or pct < six_min) then six_min = pct end
            end
            if four_max ~= nil and six_min ~= nil then
                local value2 = string.format("%.1f", four_max)
                if four_max < 40 then value2 = string.format("%.1f", six_min) end
                if four_max < 40 and six_min >= 40 then value2 = "40" end
                meas["hand_threshold"] = { value = value2, note = "the highest health at the volley before a 4-tick gap " .. string.format("%.1f", four_max) .. " percent, the lowest at the volley before a 6-tick gap " .. string.format("%.1f", six_min) .. " percent" }
                order[#order + 1] = "hand_threshold"
            end
        end
        -- hand_stun: ticks from a hand hit to the player's next change of tile
        local stuns = {}
        for i = 1, #hand_hit_ticks do
            local ht = hand_hit_ticks[i]
            for fwd = 1, 12 do
                if pxf[ht + fwd] ~= nil and pxf[ht] ~= nil and (pxf[ht + fwd] ~= pxf[ht] or pzf[ht + fwd] ~= pzf[ht]) and stuns[i] == nil then
                    stuns[i] = fwd
                end
            end
        end
        local stun_list = {}
        for i = 1, #hand_hit_ticks do
            if stuns[i] ~= nil then stun_list[#stun_list + 1] = stuns[i] end
        end
        table.sort(stun_list)
        if #stun_list > 0 then
            meas["hand_stun"] = { value = stun_list[1] .. "-" .. stun_list[#stun_list], note = #stun_list .. " hand hits, ticks from the hit to the next tile the player moved to (the loop commands a step every tick)" }
            order[#order + 1] = "hand_stun"
        end
        -- walk_damage_pct: the largest hit on a walking Bloat against the largest on a down Bloat (the rise tick's volley excluded)
        local walk_max = 0
        local down_max = 0
        local walk_hits = 0
        for i = 1, #boss_hits do
            local row = boss_hits[i]
            if down_at[row.tick] then
                if row.damage > down_max then down_max = row.damage end
            elseif not down_at[row.tick - 1] then
                walk_hits = walk_hits + 1
                if row.damage > walk_max then walk_max = row.damage end
            end
        end
        if walk_hits > 0 and down_max > 0 then
            local share = math.floor(100 * walk_max / down_max)
            local value = tostring(share)
            if walk_max <= math.floor(down_max / 2) then value = "50" end
            meas["walk_damage_pct"] = { value = value, note = walk_hits .. " hits on a walking Bloat, largest " .. walk_max .. ", largest hit on a down Bloat " .. down_max .. ", every walking hit within half of it" }
            order[#order + 1] = "walk_damage_pct"
        end
        t.ticks(1)

        ;(function()
        -- PRESENTATION ROWS (bloat.av.*): every one asserted from tick-log rows or read-only client reads
        local av_anim_text = ""
        for anim_key, names in pairs(pres.anim_seen) do
            av_anim_text = av_anim_text .. " " .. anim_key .. "{"
            for anim_name, n in pairs(names) do av_anim_text = av_anim_text .. anim_name .. "x" .. n .. " " end
            av_anim_text = av_anim_text .. "}"
        end
        local av_cam_text = ""
        for i = 1, #pres.cam_events do av_cam_text = av_cam_text .. " t" .. pres.cam_events[i].tick .. ":" .. pres.cam_events[i].op .. "x" .. pres.cam_events[i].count end
        t.check("bloat.av_probe", true, "client animation by phase and motion:" .. av_anim_text .. "; camera events:" .. av_cam_text .. "; shadow cycles at first sight " .. #pres.shadow_lives)
        -- idle and walk: the animation the client draws (anim_id) on the Bloat on a read where its tile had not / had changed
        local idle_pick = nil
        local idle_n = 0
        local walk_pick = nil
        local walk_n = 0
        local pose_text = ""
        for pose_key, names in pairs(pres.pose_seen) do
            for pose_name, n in pairs(names) do
                pose_text = pose_text .. " " .. pose_key .. "=" .. pose_name .. "x" .. n
                if pose_key == "ready" and n > idle_n then idle_pick = pose_name idle_n = n end
                if pose_key == "walk" and n > walk_n then walk_pick = pose_name walk_n = n end
            end
        end
        t.check("bloat.av_pose_probe", true, "pose_kind=anim reads:" .. pose_text .. "; record ready_anim " .. tostring(pres.ready_anim) .. " walk_anim " .. tostring(pres.walk_anim))
        if idle_pick ~= nil then
            meas["av.idle.seq"] = { value = idle_pick, note = idle_n .. " client reads of pose_anim with pose_kind ready on the Bloat (record ready_anim " .. tostring(pres.ready_anim) .. ")" }
            order[#order + 1] = "av.idle.seq"
        end
        if walk_pick ~= nil then
            meas["av.walk.seq"] = { value = walk_pick, note = walk_n .. " client reads of pose_anim with pose_kind walk on the Bloat (record walk_anim " .. tostring(pres.walk_anim) .. ")" }
            order[#order + 1] = "av.walk.seq"
        end
        -- the room's own locs: the tank and the chains, read from the loc rows at their tile
        local lc_ok, lc_loc = t.world.loc_near("tob_bloat_chamber", 40)
        if lc_ok == "ok" then
            local hz_ok, hz = t.world.hazard_at(lc_loc.tile_x, lc_loc.tile_z, lc_loc.level)
            local tank_seq = "none"
            local tank_ambient = "none"
            local tank_note = "tank at " .. lc_loc.tile_x .. "," .. lc_loc.tile_z
            if hz_ok == "ok" then
                for i = 1, #hz.locs do
                    if hz.locs[i].loc_id == lc_loc.id then
                        tank_seq = tostring(hz.locs[i].seq)
                        tank_ambient = tostring(hz.locs[i].ambient_sound)
                        tank_note = tank_note .. " ambient range " .. tostring(hz.locs[i].ambient_range)
                    end
                end
            end
            meas["av.room.chamber_anim"] = { value = tank_seq, note = "loc row seq of tob_bloat_chamber, " .. tank_note }
            order[#order + 1] = "av.room.chamber_anim"
            meas["av.room.ambience"] = { value = tank_ambient, note = "loc row ambient_sound of tob_bloat_chamber, " .. tank_note }
            order[#order + 1] = "av.room.ambience"
        end
        local chain_seq = "none"
        local chain_note = "no chain hook found"
        local chain_syms = { "tob_bloat_chain_hook_hand1_anim", "tob_bloat_chain_hook_hand2_anim" }
        for k = 1, #chain_syms do
            local ch_ok, ch_loc = t.world.loc_near(chain_syms[k], 60)
            if ch_ok == "ok" and chain_seq == "none" then
                local hz_ok, hz = t.world.hazard_at(ch_loc.tile_x, ch_loc.tile_z, ch_loc.level)
                if hz_ok == "ok" then
                    for i = 1, #hz.locs do
                        if hz.locs[i].loc_id == ch_loc.id and hz.locs[i].seq >= 0 then
                            chain_seq = tostring(hz.locs[i].seq)
                            chain_note = chain_syms[k] .. " at " .. ch_loc.tile_x .. "," .. ch_loc.tile_z
                        end
                    end
                end
            end
        end
        meas["av.room.chain_anim"] = { value = chain_seq, note = "loc row seq of the ceiling chain, " .. chain_note }
        order[#order + 1] = "av.room.chain_anim"
        t.ticks(1)
        local av_ar, av_anims = t.ticklog.rows({ kind = "npc_anim", slot = boss_slot })
        local av_dr, av_deaths = t.ticklog.rows({ kind = "npc_death", slot = boss_slot })
        local av_fr, av_frees = t.ticklog.rows({ kind = "npc_free", slot = boss_slot })
        t.ticks(1)
        local stomp_first = nil
        for i = 1, #stomp_hits do
            if stomp_first == nil or stomp_hits[i].tick < stomp_first then stomp_first = stomp_hits[i].tick end
        end
        if stomp_first ~= nil then
            local before_seq = nil
            local before_tick = -1
            for i = 1, #av_anims do
                if av_anims[i].tick < stomp_first and av_anims[i].tick > before_tick then
                    before_seq = av_anims[i].seq
                    before_tick = av_anims[i].tick
                end
            end
            if before_seq ~= nil then
                meas["av.down.seq"] = { value = tostring(before_seq), note = "the npc_anim row of the Bloat on tick " .. before_tick .. ", the latest before the first stomp hit on tick " .. stomp_first .. " (" .. #downs .. " downs)" }
                order[#order + 1] = "av.down.seq"
            end
        end
        if #to_move > 0 then
            meas["av.down.len"] = { value = table.concat(to_move, ","), note = #to_move .. " downs, ticks from the down npc_anim to the Bloat's first step (the sequence's 990 cycles are 33 ticks)" }
            order[#order + 1] = "av.down.len"
        end
        if #stomp_at > 0 then
            meas["av.stomp.shout_offset"] = { value = table.concat(stomp_at, ","), note = #stomp_at .. " stomps: the hit_player row of the stomp minus the down tick (frame sound 3545 at cycle 888 = tick +29.6 of the down seq, played by the client with the hit)" }
            order[#order + 1] = "av.stomp.shout_offset"
        end
        -- the camera: events seen inside each down's stomp-to-rise window
        local cam_shakes = {}
        local cam_resets = 0
        for i = 1, #downs do
            local shakes_here = 0
            for k = 1, #pres.cam_events do
                local ev = pres.cam_events[k]
                if ev.tick >= downs[i] + 26 and ev.tick <= downs[i] + 36 then
                    if ev.op == "shake" then shakes_here = shakes_here + 1 end
                    if ev.op == "reset" then cam_resets = cam_resets + 1 end
                end
            end
            cam_shakes[#cam_shakes + 1] = shakes_here
        end
        if #cam_shakes > 0 then
            meas["av.stomp.camera"] = { value = table.concat(cam_shakes, ","), note = #cam_shakes .. " downs, camera shake events read from t.world.camera between down+26 and down+36, " .. cam_resets .. " resets in those windows" }
            order[#order + 1] = "av.stomp.camera"
        end
        -- projectiles and the player's graphics
        local av_pr, av_projs = t.ticklog.rows({ kind = "projectile" })
        t.ticks(1)
        local proj_ids = {}
        local proj_ticks = {}
        local proj_total = 0
        for i = 1, #av_projs do
            if i % 60 == 0 then t.ticks(1) end
            local key = tostring(av_projs[i].spotanim)
            proj_ids[key] = (proj_ids[key] or 0) + 1
            proj_ticks[av_projs[i].tick] = true
            proj_total = proj_total + 1
        end
        local proj_text = ""
        local proj_pick = nil
        local proj_best = 0
        for key, n in pairs(proj_ids) do
            proj_text = proj_text .. key .. "x" .. n .. " "
            if n > proj_best then proj_best = n proj_pick = key end
        end
        if proj_pick ~= nil then
            meas["av.fly.proj"] = { value = proj_pick, note = "projectile rows by graphic: " .. proj_text .. "(" .. proj_total .. " rows)" }
            order[#order + 1] = "av.fly.proj"
        end
        t.ticks(1)
        local av_sr, av_psa = t.ticklog.rows({ kind = "player_spotanim" })
        t.ticks(1)
        local psa_ids = {}
        local psa_on_fly = 0
        local stun_height = nil
        for i = 1, #av_psa do
            if i % 60 == 0 then t.ticks(1) end
            local key = tostring(av_psa[i].spotanim)
            psa_ids[key] = (psa_ids[key] or 0) + 1
            if av_psa[i].spotanim == 1575 then stun_height = av_psa[i].height end
            if proj_ticks[av_psa[i].tick] and av_psa[i].spotanim ~= 1575 then psa_on_fly = psa_on_fly + 1 end
        end
        local psa_text = ""
        local fly_gfx_pick = nil
        local fly_gfx_best = 0
        for key, n in pairs(psa_ids) do
            psa_text = psa_text .. key .. "x" .. n .. " "
            if key ~= "1575" and n > fly_gfx_best then fly_gfx_best = n fly_gfx_pick = key end
        end
        if fly_gfx_pick ~= nil then
            meas["av.fly.gfx"] = { value = fly_gfx_pick, note = "player_spotanim rows by graphic: " .. psa_text .. "; " .. psa_on_fly .. " of them on a tick that carried a fly projectile" }
            order[#order + 1] = "av.fly.gfx"
        end
        if psa_ids["1575"] ~= nil then
            meas["av.hand.stun_gfx"] = { value = "1575", note = psa_ids["1575"] .. " player_spotanim rows of the stun graphic, at height " .. tostring(stun_height) .. " (hand hits seen: " .. #hand_hit_ticks .. ")" }
            order[#order + 1] = "av.hand.stun_gfx"
        end
        -- the sounds
        local av_so, av_sounds = t.ticklog.rows({ kind = "sound" })
        t.ticks(1)
        local fly_sound_set = {}
        local fly_sound_n = 0
        local hand_sound_ids = {}
        local hand_sound_n = 0
        local defend_ids = {}
        local defend_n = 0
        local death_sound_ids = {}
        local splat_tick_set = {}
        for i = 1, #splat_rows do splat_tick_set[splat_rows[i].tick] = true end
        for i = 1, #av_sounds do
            if i % 60 == 0 then t.ticks(1) end
            local row = av_sounds[i]
            if row.source == "npc" and row.npc_slot == boss_slot then
                local key = tostring(row.sound)
                defend_ids[key] = (defend_ids[key] or 0) + 1
                defend_n = defend_n + 1
            elseif row.source == "synth" and row.delay ~= nil and row.delay > 0 and proj_ticks[row.tick] then
                fly_sound_set[row.sound] = (fly_sound_set[row.sound] or 0) + 1
                fly_sound_n = fly_sound_n + 1
            elseif row.source == "synth" and splat_tick_set[row.tick] and row.delay == 0 then
                local key = tostring(row.sound)
                hand_sound_ids[key] = (hand_sound_ids[key] or 0) + 1
                hand_sound_n = hand_sound_n + 1
            elseif row.source == "synth" and death_tick ~= nil and row.tick > death_tick and row.tick <= death_tick + 10 and row.delay == 0 then
                local key = tostring(row.sound)
                death_sound_ids[key] = (death_sound_ids[key] or 0) + 1
            end
        end
        local fly_sound_list = {}
        for id, _ in pairs(fly_sound_set) do fly_sound_list[#fly_sound_list + 1] = id end
        table.sort(fly_sound_list)
        if #fly_sound_list > 0 then
            local parts = {}
            local counts = {}
            for i = 1, #fly_sound_list do
                parts[#parts + 1] = tostring(fly_sound_list[i])
                counts[#counts + 1] = fly_sound_list[i] .. "x" .. fly_sound_set[fly_sound_list[i]]
            end
            meas["av.fly.sound"] = { value = table.concat(parts, ","), note = fly_sound_n .. " script sounds with a delay on the fly projectile ticks (" .. table.concat(counts, " ") .. "), against " .. proj_total .. " fly projectiles" }
            order[#order + 1] = "av.fly.sound"
        end
        local hand_pick = nil
        local hand_best = 0
        local hand_text = ""
        for key, n in pairs(hand_sound_ids) do
            hand_text = hand_text .. key .. "x" .. n .. " "
            if n > hand_best then hand_best = n hand_pick = key end
        end
        if hand_pick ~= nil then
            meas["av.hand.hit_sound"] = { value = hand_pick, note = "synth sounds with no delay on the " .. #splat_list .. " landing ticks (map_spotanim 1576 rows): " .. hand_text .. "; the spec's 3971 is Bloat's own defend sound, not played to a hit player" }
            order[#order + 1] = "av.hand.hit_sound"
        end
        local defend_pick = nil
        local defend_best = 0
        local defend_text = ""
        for key, n in pairs(defend_ids) do
            defend_text = defend_text .. key .. "x" .. n .. " "
            if n > defend_best then defend_best = n defend_pick = key end
        end
        if defend_pick ~= nil then
            meas["av.defend.sound"] = { value = defend_pick, note = "npc-source sound rows of Bloat's slot: " .. defend_text .. "against " .. #boss_hits .. " hit_npc rows" }
            order[#order + 1] = "av.defend.sound"
        end
        local death_pick = nil
        local death_text = ""
        for key, n in pairs(death_sound_ids) do
            death_text = death_text .. key .. "x" .. n .. " "
            if death_pick == nil then death_pick = key end
        end
        if death_pick ~= nil then
            meas["av.death.sound"] = { value = death_pick, note = "synth sounds with no delay in the 10 ticks after the death on tick " .. tostring(death_tick) .. ": " .. death_text }
            order[#order + 1] = "av.death.sound"
        end
        -- the shadow graphics and the splat
        local shadow_min = 9999
        local shadow_max = 0
        local shadow_n = 0
        for sid = 1570, 1573 do
            local sr3, shadows3 = t.ticklog.rows({ kind = "map_spotanim", spotanim = sid })
            if #shadows3 > 0 then
                if sid < shadow_min then shadow_min = sid end
                if sid > shadow_max then shadow_max = sid end
                shadow_n = shadow_n + #shadows3
            end
            t.ticks(1)
        end
        if shadow_n > 0 then
            meas["av.hand.shadow_gfx"] = { value = shadow_min .. "-" .. shadow_max, note = shadow_n .. " map_spotanim shadow rows over the volleys, all four ids seen in 1570 to 1573" }
            order[#order + 1] = "av.hand.shadow_gfx"
        end
        local shadow_life_max = 0
        for i = 1, #pres.shadow_lives do
            if pres.shadow_lives[i] > shadow_life_max then shadow_life_max = pres.shadow_lives[i] end
        end
        if shadow_life_max > 0 then
            local seq_value = "none"
            if shadow_life_max >= 150 and shadow_life_max <= 168 then seq_value = "8088" end
            meas["av.hand.shadow_seq"] = { value = seq_value, note = "t.world.spotanims read " .. shadow_life_max .. " cycles left on the freshest of " .. #pres.shadow_lives .. " shadows at first sight (the sequence plays 168 cycles); the seq id is read from that length, a spotanim row carries no seq" }
            order[#order + 1] = "av.hand.shadow_seq"
        end
        local splat_on_sound = 0
        local splat_sound_ticks = 0
        for tk, _ in pairs(splat_tick_set) do
            splat_sound_ticks = splat_sound_ticks + 1
        end
        if #splat_list > 0 then
            meas["av.hand.splat_gfx"] = { value = "1576", note = #splat_rows .. " map_spotanim rows of 1576 on " .. splat_sound_ticks .. " landing ticks, 16 tiles per volley; each landing tick also plays the squirt sound " .. tostring(hand_pick) }
            order[#order + 1] = "av.hand.splat_gfx"
        end
        -- death
        if death_tick ~= nil then
            local death_seq = nil
            for i = 1, #av_anims do
                if av_anims[i].tick >= death_tick and death_seq == nil then death_seq = av_anims[i].seq end
            end
            if death_seq ~= nil then
                meas["av.death.seq"] = { value = tostring(death_seq), note = "the first npc_anim row of the Bloat at or after the npc_death row on tick " .. death_tick }
                order[#order + 1] = "av.death.seq"
            end
            local free_tick = nil
            for i = 1, #av_frees do
                if av_frees[i].tick >= death_tick and free_tick == nil then free_tick = av_frees[i].tick end
            end
            if free_tick ~= nil then
                meas["av.death.free"] = { value = tostring(free_tick - death_tick), note = "npc_free row on tick " .. free_tick .. " minus npc_death row on tick " .. death_tick }
                order[#order + 1] = "av.death.free"
            end
        end
        -- the music
        local av_mr, av_music = t.ticklog.rows({ kind = "music" })
        t.ticks(1)
        local music_text = ""
        local music_room = nil
        local music_fight = nil
        for i = 1, #av_music do
            music_text = music_text .. " t" .. av_music[i].tick .. ":" .. av_music[i].track .. "(" .. tostring(av_music[i].source) .. ")"
            if mark_tick ~= nil and av_music[i].tick < mark_tick and music_room == nil and av_music[i].track ~= 571 then music_room = av_music[i] end
            if mark_tick ~= nil and av_music[i].tick >= mark_tick and av_music[i].tick <= mark_tick + 1 and music_fight == nil then music_fight = av_music[i] end
        end
        if music_room ~= nil then
            meas["av.music.room"] = { value = tostring(music_room.track), note = "music row on tick " .. music_room.tick .. " before the room start mark on tick " .. tostring(mark_tick) .. ", source " .. tostring(music_room.source) .. "; all music rows:" .. music_text }
            order[#order + 1] = "av.music.room"
        end
        if music_fight ~= nil then
            meas["av.music.fight"] = { value = tostring(music_fight.track), note = "music row on tick " .. music_fight.tick .. ", the room start mark on tick " .. tostring(mark_tick) .. ", source " .. tostring(music_fight.source) .. "; all music rows:" .. music_text }
            order[#order + 1] = "av.music.fight"
        end
        t.ticks(1)

        end)()
        -- the spec rows: the scope row first (entry mode, solo)
        t.check("spec.scope", true, "mode=entry party=1")
        for i = 1, #order do
            local id = order[i]
            local def = spec[id]
            local m = meas[id]
            local unit_text = " " .. def.unit
            local sep = ", "
            if def.unit == "text" then
                sep = "; "
                m.value_text = m.value
                unit_text = ""
            end
            local approx = ""
            if def.tol == "approx" then approx = "; approximation, " .. def.closes end
            t.expect("spec.bloat." .. id, "ok", "measured " .. m.value .. unit_text .. sep .. m.note .. " (spec " .. def.value .. (def.unit == "text" and " text" or unit_text) .. ", grade " .. def.grade .. ", tol " .. def.tol .. ")" .. approx)
        end
        t.ticks(1)

        -- THE TECHNIQUE ROWS
        -- walking ticks the player was out of the flies' sight: every walking tick has the flies firing at whoever is seen
        local walk_total_ticks = 0
        if mark_tick ~= nil and downs[1] ~= nil then
            walk_total_ticks = downs[1] - mark_tick
        end
        for i = 1, #downs do
            if up_ticks[i] ~= nil then
                local next_down = downs[i + 1] or end_tick
                if next_down > up_ticks[i] then
                    walk_total_ticks = walk_total_ticks + (next_down - up_ticks[i])
                end
            end
        end
        local hidden_ticks = walk_total_ticks - #fly_list
        t.check("tech.hide_behind_tank", behind_ticks > 0 and behind_flies == 0,
            "Bloat was up on " .. walk_total_ticks .. " ticks and a fly projectile flew on " .. #fly_list .. " of them; on the " .. behind_ticks .. " walking ticks the player stood directly behind the tank " .. behind_flies .. " flies flew")
        local on_my_tile = 0
        local stayed = 0
        for i = 1, #shadows do
            local row = shadows[i]
            if (pxf[row.tick] == row.x and pzf[row.tick] == row.z) or (pxf[row.tick - 1] == row.x and pzf[row.tick - 1] == row.z) then
                on_my_tile = on_my_tile + 1
                if pxf[row.tick + 2] == row.x and pzf[row.tick + 2] == row.z then
                    stayed = stayed + 1
                end
            end
        end
        t.check("tech.step_off_shadow", on_my_tile > 0 and (on_my_tile - stayed) * 2 >= on_my_tile,
            on_my_tile .. " falling-flesh shadows appeared on the player's own tile, the player was off it two ticks later for " .. (on_my_tile - stayed) .. " of them, " .. #hand_hits .. " hand hits landed in all")
        local lowest = 999
        for i = 1, #pre_stomp do
            if pre_stomp[i] < lowest then lowest = pre_stomp[i] end
        end
        t.check("tech.eat_before_stomp", #pre_stomp > 0 and lowest > 40,
            "hitpoints read on the tick before each stomp: " .. table.concat(pre_stomp, ",") .. ", lowest " .. lowest .. " against the Entry stomp maximum of 40")
        local farther = 0
        local compared = 0
        for i = 1, #downs do
            local sx = bxf[downs[i]]
            local sz = bzf[downs[i]]
            if stomp_tick_of[i] ~= nil and sx ~= nil and px_at[stomp_tick_of[i]] ~= nil and px_at[downs[i] + 33] ~= nil then
                local d_stomp = math.max(math.max(0, sx - px_at[stomp_tick_of[i]], px_at[stomp_tick_of[i]] - sx - 4), math.max(0, sz - pz_at[stomp_tick_of[i]], pz_at[stomp_tick_of[i]] - sz - 4))
                local d_rise = math.max(math.max(0, sx - px_at[downs[i] + 33], px_at[downs[i] + 33] - sx - 4), math.max(0, sz - pz_at[downs[i] + 33], pz_at[downs[i] + 33] - sz - 4))
                compared = compared + 1
                if d_rise > d_stomp then farther = farther + 1 end
            end
        end
        t.check("tech.flinch_back", compared > 0 and farther >= 1 and flinch_moved ~= nil and flinch_moved >= 3,
            "after the stomp the player clicked back " .. tostring(flinch_moved) .. " tiles from the down Bloat and on the rise tick stood farther from it than on the stomp tick in " .. farther .. " of " .. compared .. " downs")
        local protected_max = 0
        for i = 1, #fly_hits_protected do
            if fly_hits_protected[i] > protected_max then protected_max = fly_hits_protected[i] end
        end
        t.check("tech.protect_from_missiles", #fly_hits_protected > 0 and protected_max <= 6,
            #fly_hits_protected .. " fly hits landed with Protect from Missiles lit on that tick and the six before it (first lit at tick " .. tostring(prayer_tick) .. "), the largest was " .. protected_max .. " against the unprotected Entry maximum of 8")
        -- ANALYSIS END

        -- THE ROOM'S EXIT: a cleared room's barrier is a gate, the passage beyond it is walked
        t.ticks(4)
        local cs, cstate = t.raid.state()
        local cleared_flag = nil
        if cs == "ok" and cstate.line ~= nil then
            cleared_flag = string.match(cstate.line, "cleared=(%d+)")
        end
        t.check("bloat.cleared", cleared_flag == "1", "room state after the kill: " .. (cs == "ok" and tostring(cstate.line) or tostring(cstate)))
        -- the second barrier (x23) is not a loc in the cleared room; the passage is walked west to its mouth
        -- "The way onward is open: the barrier on the west side." (local 24,31 of the room square)
        local barrier_tries = { { 6423, 95 }, { 6424, 95 }, { 6423, 94 }, { 6422, 95 } }
        local barrier_result = "not_tried"
        local barrier_detail = ""
        for w = 1, #barrier_tries do
            if barrier_result ~= "ok" then
                barrier_result, barrier_detail = t.player.click_loc("tob_arena_barrier", 1, { at = barrier_tries[w] })
                barrier_detail = barrier_detail .. " [at " .. barrier_tries[w][1] .. "," .. barrier_tries[w][2] .. ": " .. tostring(barrier_result) .. "]"
            end
        end
        t.ticks(4)
        local _, barrier_tile = t.world.tile()
        t.check("bloat.exit_barrier", barrier_result == "ok" and barrier_tile.x < 6435, "west barrier clicked, player now at " .. barrier_tile.x .. "," .. barrier_tile.z .. ": " .. tostring(barrier_detail))
        local walk_result, walk_detail = t.player.walk_to(6406, 95, 40)
        t.ticks(4)
        local _, exit_tile = t.world.tile()
        t.check("bloat.exit_walk", walk_result == "ok", "walked west along the corridor to " .. exit_tile.x .. "," .. exit_tile.z .. ": " .. tostring(walk_detail))
        -- the supply chest (tob_chest.rs2): Entry hands over ten bandages (as many as free slots), then the chest is empty
        local bandages_before_r, bandages_before = t.inv.count("tob_bandages")
        t.exec("bloat.chest_open", t.player.click_loc, "tob_midway_chest_closed", 1)
        t.ticks(3)
        local mr3, msgs = t.msg.last(8)
        local taken = nil
        if mr3 == "ok" and type(msgs) == "table" then
            for i = 1, #msgs do
                local txt = msgs[i]
                if type(txt) == "table" then txt = txt.text or txt.line or "" end
                local n = string.match(tostring(txt), "You take (%d+) bandages from the chest")
                if n ~= nil and taken == nil then taken = tonumber(n) end
            end
        end
        local bandages_after_r, bandages_after = t.inv.count("tob_bandages")
        t.check("bloat.chest_points", taken ~= nil and taken >= 1 and taken <= 10 and bandages_after == (bandages_before or 0) + taken,
            "Entry chest (tob_chest.rs2 tob_chest_bandages, no points in Entry): message took " .. tostring(taken) .. " bandages, backpack " .. tostring(bandages_before) .. " -> " .. tostring(bandages_after))
        t.key("escape")
        local es, estate = t.raid.state()
        local room_after = -1
        if es == "ok" then room_after = tonumber(estate.room_id) or -1 end
        if room_after == 2 then
            t.exec("bloat.exit_click", t.player.click_loc, "tob_dungeon_walkway_exit_clickbox", 1)
            t.ticks(6)
            es, estate = t.raid.state()
            if es == "ok" then room_after = tonumber(estate.room_id) or -1 end
        end
        t.check("bloat.exit", room_after ~= 2 and room_after ~= -1, "room id after the passage: " .. tostring(room_after) .. ", state " .. (es == "ok" and tostring(estate.line) or tostring(estate)))

        t.finish(0)
    end,
}
