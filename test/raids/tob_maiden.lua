return {
    id = "tob_maiden",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        -- an empty backpack so the food and gear below all fit
        "::clearinv",
        -- combat stats an Entry Mode maiden player brings along
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- ranged gear worn for the fight (Maiden is slain from range)
        "::give twisted_bow",
        "::give dragon_arrow 1000",
        "::give masori_mask",
        "::give masori_body",
        "::give masori_chaps",
        "::give avas_assembler",
        -- food for the blackstorm and pool damage (14 sharks)
        "::give shark 14",
        -- three Saradomin brews: more healing, and the room's debug kit adds none of its own when the pack holds one
        "::give br_4dosepotionofsaradomin 3",
        -- four prayer restores: Protect from Magic drains through the fight and the pools drain more
        "::give br_4dose2restore 4",
        -- a melee weapon for the bow flick (the drain is judged on the weapon worn when she aims)
        "::give abyssal_whip",
    },

    run = function(t)
        -- (1) land at the room's entrance the way a player arrives
        t.check("spec.scope", true, "mode=entry party=1")
        local er, ed = t.raid.enter("tob", "maiden", { mode = "entry" })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "maiden" and st.mode == "entry" and not st.started, sr == "ok" and (st.raid .. " " .. st.room .. " " .. st.mode .. " started " .. tostring(st.started) .. " " .. tostring(st.line)) or tostring(st))
        -- (2) the log runs before the room starts, and her world slot is read while she stands
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))
        local br, brow = t.npc.nearest("tob_maiden_100", 30)
        t.check("boss.present", br == "ok", tostring(brow and brow.slot))
        local wr, ws = t.ticklog.slot(brow)
        t.check("boss.slot", wr == "ok", tostring(ws))
        local gear = { "masori_mask", "masori_body", "masori_chaps", "avas_assembler", "twisted_bow", "dragon_arrow" }
        for g = 1, #gear do
            t.exec("gear." .. gear[g], t.player.equip, gear[g])
        end
        local sc_r, sc_n = t.inv.count("shark")
        t.check("food.count", sc_r == "ok" and sc_n >= 14, "sharks carried into the fight: " .. tostring(sc_n))
        -- (3) the player's own click on the barrier starts the room
        local cr, cd = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", cr == "ok", tostring(cd))
        local pr, pd = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.confirm", pr == "ok", tostring(pd))
        local mr, md = t.msg.expect("The fight begins")
        t.check("barrier.msg", mr == "ok", tostring(md))
        local mk1, mk2 = t.ticklog.mark("room start")
        t.check("room.mark", mk1 == "ok", tostring(mk2))
        local tr, mark_tick = t.tick()
        t.check("room.tick", tr == "ok", tostring(mark_tick))
        -- her hitpoints before anything touches her: the content's read-only readout, the room is unchanged by it
        t.cheat("::tobwhy")
        t.ticks(1)
        local _, base_lines = t.msg.last(40)
        local boss_hp = nil
        for l = 1, #base_lines do
            if boss_hp == nil and string.find(base_lines[l].text, "Maiden", 1, true) then boss_hp = tonumber(string.match(base_lines[l].text, "hp=(%d+)")) end
        end
        t.check("boss.hp", boss_hp ~= nil, "readout before the first attack: her hitpoints " .. tostring(boss_hp))

        -- (4) the fight. Phase A: step on the tick before each of her attacks, prayer off, until a
        -- throw has been aimed past a step and one blackstorm has landed unprotected. Her animation
        -- is read from the client on every tick of the window (the animation lengths of the table).
        local boss_symbols = { "tob_maiden_100", "tob_maiden_70", "tob_maiden_50", "tob_maiden_30" }
        local prayer_on_tick = nil
        local sidesteps = 0
        local sidesteps_plus1 = 0
        local side_texts = ""
        local disc_blood = false
        local eats = 0
        local lowest_hp = 999
        local serial_mark = 0
        local anim_reads = {}
        local ring = {
            { -1, 0 }, { 0, 1 }, { 0, -1 }, { -1, 1 }, { -1, -1 }, { 1, 0 }, { 1, 1 }, { 1, -1 },
            { -2, 0 }, { 0, 2 }, { 0, -2 }, { 2, 0 }, { -2, 2 }, { -2, -2 }, { 2, 2 }, { 2, -2 },
        }
        t.player.attack("tob_maiden_100", 2, 2)
        for k = 1, 8 do
            if disc_blood and prayer_on_tick ~= nil then break end
            if prayer_on_tick == nil then
                local hr, hrows = t.ticklog.rows({ kind = "hit_player" })
                local big = false
                for h = 1, #hrows do
                    if hrows[h].damage >= 15 then big = true end
                end
                if big then
                    local pr2, pd2 = t.prayer.set("protectfrommagic", true)
                    t.check("prayer.magic", pr2 == "ok", tostring(pd2))
                    local _, set_tick = t.tick()
                    prayer_on_tick = set_tick
                end
            end
            local target = mark_tick + 7 + 10 * (k - 1)
            local guard = 0
            local nr, now = t.tick()
            while now < target and guard < 40 do
                local _, hpw = t.skill.read("hitpoints")
                if hpw.level < lowest_hp then lowest_hp = hpw.level end
                if hpw.level < 62 then
                    t.player.inv_op("shark", 1)
                    eats = eats + 1
                end
                local _, mew = t.world.tile()
                local hzw_r, hzw = t.world.hazard_at(mew.x, mew.z)
                if hzw_r == "ok" and string.find(hzw.text, "loc", 1, true) then
                    for r = 1, #ring do
                        local wx = mew.x + ring[r][1]
                        local wz = mew.z + ring[r][2]
                        local wc_r, wc = t.world.hazard_at(wx, wz)
                        if wc_r == "ok" and wc.count == 0 then
                            t.player.walk_to(wx, wz, 3)
                            break
                        end
                    end
                end
                local as_r, as_row = t.npc.state("tob_maiden_100")
                if as_r == "ok" then anim_reads[#anim_reads + 1] = { tick = now, anim = as_row.anim_id } end
                t.ticks(1)
                nr, now = t.tick()
                guard = guard + 1
            end
            if now == target then
                local _, me0 = t.world.tile()
                local step_r = "refused"
                local step_d = "no quiet adjacent tile"
                for r = 1, 8 do
                    local sx = me0.x + ring[r][1]
                    local sz = me0.z + ring[r][2]
                    local sc_r3, sc3 = t.world.hazard_at(sx, sz)
                    if sc_r3 == "ok" and sc3.count == 0 then
                        step_r, step_d = t.player.step_tick(sx, sz)
                        break
                    end
                end
                sidesteps = sidesteps + 1
                if string.find(tostring(step_d), "(+1)", 1, true) then sidesteps_plus1 = sidesteps_plus1 + 1 end
                if sidesteps <= 3 then side_texts = side_texts .. "[" .. tostring(step_r) .. " " .. tostring(step_d) .. "] " end
                for q = 1, 7 do
                    local _, qnow = t.tick()
                    local qs_r, qs_row = t.npc.state("tob_maiden_100")
                    if qs_r == "ok" then anim_reads[#anim_reads + 1] = { tick = qnow, anim = qs_row.anim_id } end
                    t.ticks(1)
                end
                t.player.attack("tob_maiden_100", 2, 2)
                local ar2, arows = t.ticklog.rows({ kind = "npc_anim", slot = ws, since = serial_mark })
                for a = 1, #arows do
                    serial_mark = arows[a].serial
                    if arows[a].seq == 8091 and arows[a].tick == target + 1 and string.find(tostring(step_d), "(+1)", 1, true) then disc_blood = true end -- maiden_attack_blood
                end
            end
        end
        t.check("fight.sidesteps", sidesteps > 0, "sidesteps on the tick before her attack: " .. sidesteps .. ", resolved +1: " .. sidesteps_plus1 .. ", aimed-past-a-step throw seen: " .. tostring(disc_blood) .. ", prayer lit at tick " .. tostring(prayer_on_tick) .. " " .. side_texts)
        local phase_b_tick = nil
        do
            local _, pbt = t.tick()
            phase_b_tick = pbt
        end

        -- Phase B: the kill. Ranged attacks (crabs after the first pair has walked in), re-pressed whenever a
        -- dodge, a flick or eight quiet ticks dropped the engagement.
        if prayer_on_tick == nil then
            local pr3, pd3 = t.prayer.set("protectfrommagic", true)
            t.check("prayer.magic_late", pr3 == "ok", tostring(pd3))
            local _, set_tick2 = t.tick()
            prayer_on_tick = set_tick2
        end
        local food_names = { "shark", "br_4dosepotionofsaradomin", "br_3dosepotionofsaradomin", "br_2dosepotionofsaradomin", "br_1dosepotionofsaradomin" }
        local restore_names = { "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" }
        local far_ring = { { 0, 3 }, { 0, -3 }, { -3, 0 }, { -3, 3 }, { -3, -3 }, { 3, 0 }, { 3, 3 }, { 3, -3 } }
        local focus = nil
        local focus_tick = 0
        local dodges = 0
        local far_dodges = 0
        local far_moves = {}
        local restores = 0
        local danger = {}
        local last_serial = 0
        local crab_swings = 0
        local slug_swings = 0
        local iterations = 0
        local crab_hp = nil
        local slug_hp = nil
        local stand_state = 0
        local stand_start = 0
        local stand_end_tick = nil
        local leak_state = 0
        local crab_seen_tick = 0
        local hp_reads = {}
        local last_read_clock = -1
        local anim_serial = 0
        local flick_hold = false
        local flick_a = 0
        local flick_before = nil
        local flick_equip_tick = 0
        local flicks = {}
        local flick_tries = 0
        local trace = ""
        local done_detail = "not finished"
        while iterations < 1500 do
            iterations = iterations + 1
            local frr, frows = t.ticklog.rows({ kind = "npc_free", slot = ws })
            if type(frows) == "table" and #frows > 0 then
                done_detail = "boss npc_free at tick " .. tostring(frows[1].tick) .. " after " .. iterations .. " iterations"
                break
            end
            local _, now_tick = t.tick()
            local seg = tostring(now_tick)
            local _, hp = t.skill.read("hitpoints")
            if hp.level < lowest_hp then lowest_hp = hp.level end
            local _, me = t.world.tile()
            local prr, prows = t.ticklog.rows({ kind = "projectile", since = last_serial })
            local new_blood = false
            if type(prows) == "table" then
                for k = 1, #prows do
                    last_serial = prows[k].serial
                    if prows[k].spotanim == 1578 then -- maiden_blood_proj
                        danger[#danger + 1] = { x = prows[k].dst_x, z = prows[k].dst_z, land = prows[k].tick + math.ceil((prows[k].end_cycle - prows[k].start_cycle) / 30) }
                        new_blood = true
                    end
                end
            end
            local threatened = false
            local projectile_threat = false
            local hzr, hz = t.world.hazard_at(me.x, me.z)
            local under_pool = hzr == "ok" and string.find(hz.text, "loc", 1, true) ~= nil
            if stand_state == 0 then
                if under_pool then
                    if hp.level >= 70 then
                        stand_state = 1
                        stand_start = now_tick
                    else
                        stand_state = 2
                        stand_end_tick = now_tick
                    end
                end
            elseif stand_state == 1 then
                if now_tick >= stand_start + 3 then
                    stand_state = 2
                    stand_end_tick = now_tick
                end
            end
            if stand_state == 2 then
                for k = 1, #danger do
                    if danger[k].x == me.x and danger[k].z == me.z and now_tick + 2 >= danger[k].land and now_tick <= danger[k].land + 12 then
                        threatened = true
                        projectile_threat = true
                    end
                end
                if under_pool then threatened = true end
            end
            if threatened then
                local moved = false
                if projectile_threat then
                    local cands = {}
                    if far_dodges % 2 == 0 then cands = { { 0, 3 }, { 0, -3 } } else cands = { { 0, -3 }, { 0, 3 } } end
                    if me.x <= 6438 then
                        cands[#cands + 1] = { 3, 0 }
                        cands[#cands + 1] = { 3, 3 }
                        cands[#cands + 1] = { 3, -3 }
                    else
                        cands[#cands + 1] = { -3, 0 }
                        cands[#cands + 1] = { -3, 3 }
                        cands[#cands + 1] = { -3, -3 }
                    end
                    for k = 1, #cands do
                        local cx = me.x + cands[k][1]
                        local cz = me.z + cands[k][2]
                        local bad = cx < 6432 or cx > 6446 or cz < 84 or cz > 104
                        for d = 1, #danger do
                            if math.max(math.abs(danger[d].x - cx), math.abs(danger[d].z - cz)) <= 2 and now_tick <= danger[d].land + 12 then bad = true end
                        end
                        local cr2, ch = t.world.hazard_at(cx, cz)
                        if not bad and cr2 == "ok" and ch.count == 0 then
                            local wr = t.player.walk_to(cx, cz, 4)
                            if wr == "ok" then
                                moved = true
                                far_moves[#far_moves + 1] = { tick = now_tick, fx = me.x, fz = me.z }
                                far_dodges = far_dodges + 1
                                dodges = dodges + 1
                                trace = trace .. "[far-dodge t" .. now_tick .. " " .. me.x .. "," .. me.z .. ">" .. cx .. "," .. cz .. "]"
                                break
                            end
                        end
                    end
                end
                if not moved then
                    for k = 1, #ring do
                        local cx = me.x + ring[k][1]
                        local cz = me.z + ring[k][2]
                        local bad = false
                        for d = 1, #danger do
                            if danger[d].x == cx and danger[d].z == cz and now_tick <= danger[d].land + 12 then bad = true end
                        end
                        local cr2, ch = t.world.hazard_at(cx, cz)
                        if not bad and cr2 == "ok" and not string.find(ch.text, "loc", 1, true) then
                            if k <= 8 then
                                t.player.step_tick(cx, cz)
                            else
                                t.player.walk_to(cx, cz, 3)
                            end
                            dodges = dodges + 1
                            trace = trace .. "[dodge t" .. now_tick .. " " .. me.x .. "," .. me.z .. ">" .. cx .. "," .. cz .. "]"
                            break
                        end
                    end
                end
                focus = nil
            end
            if hp.level < 70 then
                for f = 1, #food_names do
                    local fc_r, fc = t.inv.count(food_names[f])
                    if fc_r == "ok" and fc > 0 then
                        t.player.inv_op(food_names[f], 1)
                        eats = eats + 1
                        trace = trace .. "[eat " .. food_names[f] .. " hp" .. hp.level .. " t" .. now_tick .. "]"
                        break
                    end
                end
            end
            do local _, st1 = t.tick() seg = seg .. " eat>" .. st1 end
            local _, pw = t.skill.read("prayer")
            if pw.level < 28 then
                for f = 1, #restore_names do
                    local rc_r, rc = t.inv.count(restore_names[f])
                    if rc_r == "ok" and rc > 0 then
                        t.player.inv_op(restore_names[f], 1)
                        restores = restores + 1
                        trace = trace .. "[restore pray" .. pw.level .. " t" .. now_tick .. "]"
                        break
                    end
                end
            end
            do local _, st2 = t.tick() seg = seg .. " dodge>" .. st2 end
            -- the bow flick: the drain is chosen when her shot is aimed (the attack tick), so a melee weapon
            -- put on between the aim and the impact must still see the RANGED stat drained
            if flick_hold then
                if now_tick >= flick_a + 6 then
                    local _, rl = t.skill.read("ranged")
                    local _, al = t.skill.read("attack")
                    local _, sl = t.skill.read("strength")
                    flicks[#flicks + 1] = { a = flick_a, equip_tick = flick_equip_tick, dr = flick_before.r - rl.level, da = flick_before.a - al.level, ds = flick_before.s - sl.level }
                    t.player.equip("twisted_bow")
                    flick_hold = false
                    focus = nil
                    trace = trace .. "[flick done A" .. flick_a .. " dr" .. (flick_before.r - rl.level) .. "]"
                end
            elseif flick_tries < 7 and prayer_on_tick ~= nil and not threatened and #flicks < 3 then
                local fr_r, frows2 = t.ticklog.rows({ kind = "npc_anim", slot = ws, seq = 8092, since = anim_serial })
                if type(frows2) == "table" then
                    for k = 1, #frows2 do
                        anim_serial = frows2[k].serial
                        if now_tick <= frows2[k].tick + 2 and not flick_hold and frows2[k].tick > phase_b_tick then
                            local _, rl = t.skill.read("ranged")
                            local _, al = t.skill.read("attack")
                            local _, sl = t.skill.read("strength")
                            flick_before = { r = rl.level, a = al.level, s = sl.level }
                            t.player.equip("abyssal_whip")
                            local _, eq_tick = t.tick()
                            flick_equip_tick = eq_tick
                            flick_a = frows2[k].tick
                            flick_hold = true
                            flick_tries = flick_tries + 1
                            focus = nil
                            trace = trace .. "[flick start A" .. flick_a .. " equip t" .. eq_tick .. "]"
                        end
                    end
                end
            end
            do local _, st3 = t.tick() seg = seg .. " flick>" .. st3 end
            if focus ~= nil and now_tick - focus_tick >= 8 then focus = nil end
            local want = nil
            local crab_r, crab = t.npc.nearest("maiden_elemental", 60)
            if crab_r == "ok" then
                if leak_state == 0 then
                    leak_state = 1
                    crab_seen_tick = now_tick
                end
                if leak_state ~= 1 or now_tick - crab_seen_tick >= 45 then
                    want = "maiden_elemental"
                end
            else
                if leak_state == 1 then leak_state = 2 end
                local slug_r, slug = t.npc.nearest("maiden_blood_slug", 3)
                if slug_r == "ok" then want = "maiden_blood_slug" end
            end
            if want == nil then
                for k = 1, #boss_symbols do
                    local b_r = t.npc.nearest(boss_symbols[k], 60)
                    if b_r == "ok" then want = boss_symbols[k] break end
                end
            end
            if want ~= nil and want ~= focus and not flick_hold then
                local ar, ad = t.player.attack(want, 2, 1)
                local _, at_tick = t.tick()
                if ar == "ok" or ar == "timeout" then
                    focus = want
                    focus_tick = at_tick
                    if want == "maiden_elemental" then crab_swings = crab_swings + 1 end
                    if want == "maiden_blood_slug" then slug_swings = slug_swings + 1 end
                else
                    focus = nil
                    trace = trace .. "[attack " .. want .. " " .. tostring(ar) .. " t" .. at_tick .. "]"
                    if me.x < 6436 or me.x > 6443 then t.player.walk_to(6439, 91, 6) end
                end
            end
            do
                local _, st4 = t.tick()
                seg = seg .. " attack>" .. st4
                if st4 - now_tick > 3 then
                    trace = trace .. "[slow " .. seg .. "]"
                end
            end
            -- her hitpoints, read-only, one reading per pass (the readout names the server's own tick)
            t.cheat("::tobwhy")
            t.ticks(1)
            local _, lines = t.msg.last(40)
            local read_clock = nil
            local read_boss = nil
            local read_crab = nil
            local read_slug = nil
            for l = 1, #lines do
                local text = lines[l].text
                if read_clock == nil then
                    local mc = string.match(text, "map_clock=(%d+)")
                    if mc ~= nil then read_clock = tonumber(mc) end
                end
                if read_boss == nil and string.find(text, "Maiden", 1, true) then read_boss = tonumber(string.match(text, "hp=(%d+)")) end
                if read_crab == nil and string.find(text, "Matomenos", 1, true) then read_crab = tonumber(string.match(text, "hp=(%d+)")) end
                if read_slug == nil and string.find(text, "Blood spawn", 1, true) then read_slug = tonumber(string.match(text, "hp=(%d+)")) end
            end
            if read_clock ~= nil and read_clock ~= last_read_clock and read_boss ~= nil then
                last_read_clock = read_clock
                hp_reads[#hp_reads + 1] = { clock = read_clock, hp = read_boss }
                if read_crab ~= nil and (crab_hp == nil or read_crab > crab_hp) then crab_hp = read_crab end
                if read_slug ~= nil and (slug_hp == nil or read_slug > slug_hp) then slug_hp = read_slug end
            end
        end
        if #trace > 700 then trace = string.sub(trace, -700) end
        t.check("fight.done", done_detail ~= "not finished", done_detail .. "; dodges " .. dodges .. ", eats " .. eats .. ", crab targets " .. crab_swings .. ", slug targets " .. slug_swings .. ", restores " .. restores .. ", lowest hp " .. lowest_hp .. ", crab hp " .. tostring(crab_hp) .. ", blood spawn hp " .. tostring(slug_hp) .. ", stand ended tick " .. tostring(stand_end_tick) .. " flicks " .. #flicks .. " far-dodges " .. far_dodges .. " reads " .. #hp_reads .. " trace " .. trace)
        t.ticks(4)
        -- ANALYSIS BEGIN
        local seq_blood = 8091 -- maiden_attack_blood
        local seq_auto = 8092 -- maiden_attack_special
        local seq_death_a = 8093 -- maiden_death_a
        local seq_death_b = 8094 -- maiden_death_b
        local proj_auto = 1577 -- maiden_shadow_proj
        local proj_blood = 1578 -- maiden_blood_proj
        local fx_pool = 1579 -- maiden_lingering_blood
        local maiden_size = 6 -- the cache record's size: her footprint is the south-west tile plus six
        local specs = {}
        local texts = {}
        local boss_x, boss_z = string.match(tostring(ed), "at (%d+),(%d+)%)")
        boss_x = tonumber(boss_x)
        boss_z = tonumber(boss_z)
        local _, anim_rows = t.ticklog.rows({ kind = "npc_anim", slot = ws })
        t.ticks(1)
        local attacks = {}
        local death_a_tick = nil
        local death_b_tick = nil
        for k = 1, #anim_rows do
            local sq = anim_rows[k].seq
            if sq == seq_blood or sq == seq_auto then
                attacks[#attacks + 1] = { tick = anim_rows[k].tick, blood = (sq == seq_blood) }
            elseif sq == seq_death_a and death_a_tick == nil then
                death_a_tick = anim_rows[k].tick
            elseif sq == seq_death_b and death_b_tick == nil then
                death_b_tick = anim_rows[k].tick
            end
        end
        local gap_values = {}
        for k = 2, #attacks do gap_values[#gap_values + 1] = attacks[k].tick - attacks[k - 1].tick end
        local first_offset = attacks[1].tick - mark_tick + 1
        specs[#specs + 1] = { "cadence_entry", gap_values, "ticks", (#gap_values) .. " gaps between attack rows 8091 and 8092 on her slot in the Entry room", "10", "D", "exact" }
        specs[#specs + 1] = { "first_attack_entry", { first_offset }, "ticks", "first attack row minus the room start mark plus one - Entry room", "9", "E", "approx", "M121" }

        -- her animations as the client drew them, tick by tick (phase A read them every tick)
        local anim_at = {}
        for k = 1, #anim_reads do anim_at[anim_reads[k].tick] = anim_reads[k].anim end
        local blood_len = {}
        local auto_len = {}
        for k = 1, #attacks do
            local sq = attacks[k].blood and seq_blood or seq_auto
            if anim_at[attacks[k].tick] == sq then
                local e = attacks[k].tick
                while anim_at[e] == sq do e = e + 1 end
                if anim_at[e] ~= nil then
                    if attacks[k].blood then blood_len[#blood_len + 1] = e - attacks[k].tick else auto_len[#auto_len + 1] = e - attacks[k].tick end
                end
            end
        end
        specs[#specs + 1] = { "anim_blood_len", blood_len, "ticks", #blood_len .. " blood throw(s): ticks the client drew seq 8091 from the attack row to the first other reading", "3", "A", "exact" }
        specs[#specs + 1] = { "anim_auto_len", auto_len, "ticks", #auto_len .. " blackstorm(s): ticks the client drew seq 8092 from the attack row to the first other reading", "5", "A", "exact" }

        local _, proj_rows = t.ticklog.rows({ kind = "projectile" })
        t.ticks(1)
        local _, hit_player_rows = t.ticklog.rows({ kind = "hit_player" })
        t.ticks(1)
        local _, hit_npc_rows = t.ticklog.rows({ kind = "hit_npc" })
        t.ticks(1)
        local _, retype_rows = t.ticklog.rows({ kind = "npc_retype" })
        t.ticks(1)
        local _, spawn_rows = t.ticklog.rows({ kind = "npc_spawn" })
        t.ticks(1)
        local _, death_rows = t.ticklog.rows({ kind = "npc_death" })
        t.ticks(1)
        local _, free_rows = t.ticklog.rows({ kind = "npc_free" })
        t.ticks(1)
        local _, loc_rows = t.ticklog.rows({ kind = "loc_set" })
        t.ticks(1)
        local _, fx_rows = t.ticklog.rows({ kind = "map_spotanim" })
        t.ticks(1)
        local _, tile_rows = t.ticklog.rows({ kind = "player_tile" })
        t.ticks(1)

        local first_retype = 1000000000
        for k = 1, #retype_rows do
            if retype_rows[k].slot == ws and retype_rows[k].tick < first_retype then first_retype = retype_rows[k].tick end
        end
        local death_tick = nil
        local free_tick = nil
        for k = 1, #death_rows do if death_rows[k].slot == ws then death_tick = death_rows[k].tick end end
        for k = 1, #free_rows do if free_rows[k].slot == ws then free_tick = free_rows[k].tick end end
        local player_at = {}
        for k = 1, #tile_rows do player_at[tile_rows[k].tick] = { x = tile_rows[k].x, z = tile_rows[k].z } end

        -- pools (a splat: loc plus the 1579 graphic on the same tick) and trails (a loc with no graphic)
        local splat_at = {}
        for k = 1, #fx_rows do
            if fx_rows[k].spotanim == fx_pool then splat_at[fx_rows[k].coord .. ":" .. fx_rows[k].tick] = true end
        end
        local open = {}
        local pools = {}
        local trails = {}
        local pool_life = {}
        local trail_life = {}
        for k = 1, #loc_rows do
            local row = loc_rows[k]
            if row.loc ~= -1 then
                open[row.coord] = { tick = row.tick, splat = splat_at[row.coord .. ":" .. row.tick] == true }
            elseif open[row.coord] ~= nil then
                local life = row.tick - open[row.coord].tick
                if open[row.coord].splat then
                    pool_life[#pool_life + 1] = life
                    pools[#pools + 1] = { x = row.x, z = row.z, from = open[row.coord].tick, to = row.tick }
                else
                    trails[#trails + 1] = { x = row.x, z = row.z, from = open[row.coord].tick, to = row.tick }
                    if death_tick == nil or row.tick < death_tick then trail_life[#trail_life + 1] = life end
                end
                open[row.coord] = nil
            end
        end
        specs[#specs + 1] = { "pool_life", pool_life, "ticks", #pool_life .. " splats from loc add to loc removal", "11", "D", "+-1" }

        -- blackstorm: launch delay, total flight, impact offset, protection
        t.ticks(1)
        local launch_values = {}
        local flight_values = {}
        for k = 1, #proj_rows do
            if proj_rows[k].spotanim == proj_auto then
                launch_values[#launch_values + 1] = proj_rows[k].start_cycle
                flight_values[#flight_values + 1] = proj_rows[k].end_cycle
            end
        end
        local impact_values = {}
        local unprotected_hits = {}
        local protected_hits = {}
        for k = 1, #attacks do
            t.ticks(1)
            if not attacks[k].blood then
                local found = nil
                for _, off in ipairs({ 5, 4, 6, 3, 7 }) do
                    for h = 1, #hit_player_rows do
                        if hit_player_rows[h].tick == attacks[k].tick + off and hit_player_rows[h].npc_slot == ws and found == nil then
                            found = off
                            attacks[k].hit = hit_player_rows[h].damage
                        end
                    end
                end
                if found ~= nil then impact_values[#impact_values + 1] = found end
            end
        end
        for a = 1, #attacks do
            if not attacks[a].blood and attacks[a].hit ~= nil and attacks[a].tick + 5 < first_retype then
                if prayer_on_tick ~= nil and attacks[a].tick >= prayer_on_tick then
                    protected_hits[#protected_hits + 1] = attacks[a].hit
                else
                    unprotected_hits[#unprotected_hits + 1] = attacks[a].hit
                end
            end
        end
        specs[#specs + 1] = { "auto_launch_cycles", launch_values, "cycles", #launch_values .. " blackstorm projectiles", "120", "E", "approx", "M122" }
        specs[#specs + 1] = { "auto_flight_total", flight_values, "cycles", #flight_values .. " blackstorm projectiles", "170", "E", "approx", "M122" }
        specs[#specs + 1] = { "auto_impact_offset", impact_values, "ticks", #impact_values .. " blackstorms", "5", "E", "approx", "M122" }
        local protect_ratios = {}
        local unprotected_top = 0
        local protected_top = 0
        for k = 1, #unprotected_hits do if unprotected_hits[k] > unprotected_top then unprotected_top = unprotected_hits[k] end end
        for k = 1, #protected_hits do
            if protected_hits[k] > protected_top then protected_top = protected_hits[k] end
            if #unprotected_hits > 0 then protect_ratios[#protect_ratios + 1] = protected_hits[k] / unprotected_hits[1] end
        end
        if #unprotected_hits > 0 then
            specs[#specs + 1] = { "auto_max_entry", { unprotected_top }, "hp", #unprotected_hits .. " unprotected blackstorm hits before the first transmog: " .. table.concat(unprotected_hits, " "), "18", "D", "exact" }
        end
        if #protect_ratios > 0 then
            specs[#specs + 1] = { "auto_protect_ratio", protect_ratios, "ratio", #protected_hits .. " protected hits " .. table.concat(protected_hits, " ") .. " over the first unprotected hit " .. tostring(unprotected_hits[1]) .. ", all before the first transmog (no leaks yet)", "0.5", "D", "exact" }
        end

        -- blood throws: the group of 1578 rows on one tick is the splat under the player plus the extras
        t.ticks(1)
        local throws = {}
        for k = 1, #attacks do
            t.ticks(1)
            if attacks[k].blood then
                local main = nil
                local extras = {}
                local group = {}
                for p = 1, #proj_rows do
                    if proj_rows[p].spotanim == proj_blood and proj_rows[p].tick == attacks[k].tick then group[#group + 1] = proj_rows[p] end
                end
                local shortest = nil
                for g = 1, #group do
                    local dur = group[g].end_cycle - group[g].start_cycle
                    if shortest == nil or dur < shortest then shortest = dur main = group[g] end
                end
                for g = 1, #group do
                    if group[g] ~= main then extras[#extras + 1] = group[g] end
                end
                throws[#throws + 1] = { tick = attacks[k].tick, main = main, extras = extras, group = group }
            end
        end
        local extra_counts = {}
        local extra_skipped = 0
        local extra_delays = {}
        local scatter_dev = 0
        local lead_ok = 0
        local lead_bad = 0
        local lead_blind = 0
        local step_values = {}
        local base_values = {}
        for k = 1, #throws do
            local th = throws[k]
            if th.main ~= nil then
                local before = player_at[th.tick - 1]
                local now_tile = player_at[th.tick]
                if before ~= nil and before.x <= 6447 then
                    extra_counts[#extra_counts + 1] = #th.extras
                else
                    extra_skipped = extra_skipped + 1
                end
                local gx = math.max(boss_x - th.main.dst_x, th.main.dst_x - (boss_x + maiden_size - 1), 0)
                local gz = math.max(boss_z - th.main.dst_z, th.main.dst_z - (boss_z + maiden_size - 1), 0)
                base_values[#base_values + 1] = th.main.end_cycle - 15 * math.max(gx, gz)
                for e = 1, #th.extras do
                    extra_delays[#extra_delays + 1] = (th.extras[e].end_cycle - th.extras[e].start_cycle) - (th.main.end_cycle - th.main.start_cycle)
                    if before ~= nil then
                        local dev = math.max(math.abs(th.extras[e].dst_x - before.x), math.abs(th.extras[e].dst_z - before.z))
                        if dev > scatter_dev then scatter_dev = dev end
                    end
                end
                if before ~= nil and now_tile ~= nil then
                    if before.x ~= now_tile.x or before.z ~= now_tile.z then
                        if th.main.dst_x == before.x and th.main.dst_z == before.z then lead_ok = lead_ok + 1
                        elseif th.main.dst_x == now_tile.x and th.main.dst_z == now_tile.z then lead_bad = lead_bad + 1 end
                    else
                        lead_blind = lead_blind + 1
                    end
                end
                for j = k + 1, #throws do
                    local other = throws[j]
                    if other.main ~= nil and other.main.dst_z == th.main.dst_z and other.main.dst_x ~= th.main.dst_x
                        and th.main.dst_z >= boss_z and th.main.dst_z <= boss_z + maiden_size - 1
                        and th.main.dst_x > boss_x + maiden_size - 1 and other.main.dst_x > boss_x + maiden_size - 1 then
                        local dd = (other.main.end_cycle - other.main.start_cycle) - (th.main.end_cycle - th.main.start_cycle)
                        local dx = other.main.dst_x - th.main.dst_x
                        if dx < 0 then dx = -dx dd = -dd end
                        step_values[#step_values + 1] = dd / dx
                    end
                end
            end
        end
        specs[#specs + 1] = { "blood_extra_splats", extra_counts, "count", #extra_counts .. " throws aimed from inside the arena, extras per throw (" .. extra_skipped .. " throw(s) from the entrance tile left out: the 5x5 meets the wall and the room clamps or skips it)", "2", "C", "exact" }
        specs[#specs + 1] = { "blood_extra_delay", extra_delays, "cycles", #extra_delays .. " extra splats against the splat under the player", "25", "D", "exact" }
        specs[#specs + 1] = { "blood_flight_base", base_values, "cycles", #base_values .. " main splats: end cycle minus 15 per tile of her size-6 footprint's gap", "50", "D", "exact" }
        if #step_values > 0 then
            specs[#specs + 1] = { "blood_flight_step", step_values, "cycles", #step_values .. " throw pairs one tile apart", "15", "D", "exact" }
        end
        specs[#specs + 1] = { "extra_scatter_size", { 2 * scatter_dev + 1 }, "tiles", "largest offset of an extra splat from the player tile was " .. scatter_dev, "5", "D", "exact" }
        specs[#specs + 1] = { "scan_lead", { (lead_ok > 0 and lead_bad == 0) and 1 or 0 }, "ticks", lead_ok .. " throws aimed at the tile of tick T-1 after a step that moved on tick T, " .. lead_bad .. " at the tile of T, " .. lead_blind .. " throws with no step", "1", "B", "exact" }

        -- blood cooldown
        local autos_since = 99
        local min_autos = 99
        for k = 1, #attacks do
            if attacks[k].blood then
                if autos_since < min_autos then min_autos = autos_since end
                autos_since = 0
            else
                autos_since = autos_since + 1
            end
        end
        specs[#specs + 1] = { "blood_cooldown", { min_autos }, "count", "fewest autos between two throws over " .. #attacks .. " attacks", "2", "B", "exact" }

        -- standing in a splat or on a trail: splat hits by tile and tick
        t.ticks(1)
        local pool_hit_ticks = {}
        local pool_hit_damage = {}
        local trail_hit_damage = {}
        for k = 1, #hit_player_rows do
            local row = hit_player_rows[k]
            if row.npc_slot == -1 and row.hitsplat == 28 then
                local stood = player_at[row.tick - 1]
                local in_pool = false
                for p = 1, #pools do
                    if stood ~= nil and row.tick >= pools[p].from and row.tick <= pools[p].to + 1 and stood.x == pools[p].x and stood.z == pools[p].z then
                        in_pool = true
                        break
                    end
                end
                if in_pool then
                    pool_hit_ticks[#pool_hit_ticks + 1] = row.tick
                    pool_hit_damage[#pool_hit_damage + 1] = row.damage
                else
                    for p = 1, #trails do
                        if stood ~= nil and row.tick >= trails[p].from and row.tick <= trails[p].to + 1 and stood.x == trails[p].x and stood.z == trails[p].z then
                            trail_hit_damage[#trail_hit_damage + 1] = row.damage
                            break
                        end
                    end
                end
            end
        end
        local cadence_values = {}
        for k = 2, #pool_hit_ticks do
            if pool_hit_ticks[k] - pool_hit_ticks[k - 1] <= 3 then cadence_values[#cadence_values + 1] = pool_hit_ticks[k] - pool_hit_ticks[k - 1] end
        end
        if #cadence_values > 0 then
            specs[#specs + 1] = { "pool_hit_cadence", cadence_values, "ticks", "hit ticks " .. table.concat(pool_hit_ticks, " "), "1", "C", "exact" }
        end
        if #pool_hit_damage > 0 then
            specs[#specs + 1] = { "pool_damage_entry", { pool_hit_damage[1] }, "hp", "first standing hits " .. table.concat(pool_hit_damage, " "), "10", "E", "approx", "M121" }
        end
        if #trail_hit_damage > 0 then
            specs[#specs + 1] = { "trail_damage_entry", trail_hit_damage, "hp", #trail_hit_damage .. " hits standing on a blood-spawn trail tile", "2-5", "D", "range" }
        end

        -- her hitpoints against the tick log: hp at a reading = start - her damage taken + what she healed, up to
        -- the reading's own tick (calibrated on the ::tobwhy readings of a green run)
        local leaks = {}
        local crab_set = {}
        for k = 1, #retype_rows do
            if retype_rows[k].slot == ws then
                for s = 1, #spawn_rows do
                    if spawn_rows[s].tick >= retype_rows[k].tick and spawn_rows[s].tick <= retype_rows[k].tick + 1 and spawn_rows[s].slot ~= ws then
                        crab_set[spawn_rows[s].slot .. ":" .. spawn_rows[s].tick] = spawn_rows[s]
                    end
                end
            end
        end
        local retype_ticks = {}
        for k = 1, #retype_rows do if retype_rows[k].slot == ws then retype_ticks[retype_rows[k].tick] = true end end
        local gap_arrive = {}
        for key, crab in pairs(crab_set) do
            for d = 1, #death_rows do
                if death_rows[d].slot == crab.slot and death_rows[d].tick > crab.tick then
                    local dr = death_rows[d]
                    local gx = math.max(boss_x - dr.x, dr.x - (boss_x + maiden_size - 1), 0)
                    local gz = math.max(boss_z - dr.z, dr.z - (boss_z + maiden_size - 1), 0)
                    local absorbed = nil
                    local hit_sum = 0
                    for h = 1, #hit_npc_rows do
                        if hit_npc_rows[h].slot == crab.slot and hit_npc_rows[h].tick >= crab.tick and hit_npc_rows[h].tick <= dr.tick then
                            if hit_npc_rows[h].tick == dr.tick then absorbed = hit_npc_rows[h].damage end
                        end
                    end
                    if math.max(gx, gz) <= 1 and absorbed ~= nil then
                        leaks[#leaks + 1] = { tick = dr.tick, absorbed = absorbed }
                        gap_arrive[#gap_arrive + 1] = math.max(gx, gz)
                    end
                    break
                end
            end
        end
        t.ticks(1)
        local heal_ratios = {}
        local leak_multipliers = {}
        local transmog_gain = {}
        local threshold_values = {}
        local thresholds = { 70, 50, 30 }
        local boss_retypes = {}
        for k = 1, #retype_rows do
            if retype_rows[k].slot == ws and #boss_retypes < 3 then boss_retypes[#boss_retypes + 1] = retype_rows[k] end
        end
        for r = 2, #hp_reads do
            if r % 10 == 0 then t.ticks(1) end
            local c0 = hp_reads[r - 1].clock
            local c1 = hp_reads[r].clock
            if c1 > c0 then
                local mine = 0
                for h = 1, #hit_npc_rows do
                    if hit_npc_rows[h].slot == ws and hit_npc_rows[h].tick > c0 and hit_npc_rows[h].tick <= c1 then mine = mine + hit_npc_rows[h].damage end
                end
                local pool = 0
                for h = 1, #hit_player_rows do
                    if hit_player_rows[h].npc_slot == -1 and hit_player_rows[h].hitsplat == 28 and hit_player_rows[h].tick > c0 and hit_player_rows[h].tick <= c1 then pool = pool + hit_player_rows[h].damage end
                end
                local absorbed = 0
                local leaked = 0
                for l = 1, #leaks do
                    if leaks[l].tick > c0 and leaks[l].tick <= c1 then
                        absorbed = absorbed + leaks[l].absorbed
                        leaked = leaked + 1
                    end
                end
                local residual = hp_reads[r].hp - hp_reads[r - 1].hp + mine - pool
                if leaked == 0 and pool > 0 then heal_ratios[#heal_ratios + 1] = (hp_reads[r].hp - hp_reads[r - 1].hp + mine) / pool end
                if leaked > 0 and absorbed > 0 then leak_multipliers[#leak_multipliers + 1] = residual / absorbed end
                for b = 1, #boss_retypes do
                    local rt = boss_retypes[b].tick
                    if rt > c0 and rt <= c1 and c1 - c0 <= 3 and leaked == 0 then
                        transmog_gain[#transmog_gain + 1] = math.max(0, residual)
                    end
                end
            end
        end
        for b = 1, #boss_retypes do
            local rt = boss_retypes[b].tick
            local base_read = nil
            for r = 1, #hp_reads do
                if hp_reads[r].clock <= rt - 2 then base_read = hp_reads[r] end
            end
            if base_read ~= nil and rt - 2 - base_read.clock <= 6 then
                local seen_before = base_read.hp
                local seen_at = base_read.hp
                for h = 1, #hit_npc_rows do
                    if hit_npc_rows[h].slot == ws and hit_npc_rows[h].tick > base_read.clock then
                        if hit_npc_rows[h].tick <= rt - 2 then seen_before = seen_before - hit_npc_rows[h].damage end
                        if hit_npc_rows[h].tick <= rt - 1 then seen_at = seen_at - hit_npc_rows[h].damage end
                    end
                end
                for h = 1, #hit_player_rows do
                    if hit_player_rows[h].npc_slot == -1 and hit_player_rows[h].hitsplat == 28 and hit_player_rows[h].tick > base_read.clock then
                        if hit_player_rows[h].tick <= rt - 2 then seen_before = seen_before + hit_player_rows[h].damage end
                        if hit_player_rows[h].tick <= rt - 1 then seen_at = seen_at + hit_player_rows[h].damage end
                    end
                end
                if 100 * seen_at / boss_hp <= thresholds[b] and 100 * seen_before / boss_hp > thresholds[b] then
                    threshold_values[#threshold_values + 1] = thresholds[b]
                else
                    threshold_values[#threshold_values + 1] = math.floor(100 * seen_at / boss_hp)
                end
            end
        end
        specs[#specs + 1] = { "pool_heal_ratio", heal_ratios, "ratio", #heal_ratios .. " reading pairs with splat hits and no leak: hitpoints gained plus her damage taken over the splat damage dealt", "1", "D", "exact" }
        specs[#specs + 1] = { "leak_heal_multiplier", leak_multipliers, "ratio", #leaks .. " Matomenos arrivals: hitpoints gained over the hitpoints each absorbed", "2", "D", "exact" }
        specs[#specs + 1] = { "crab_arrive_gap", gap_arrive, "tiles", #gap_arrive .. " arrivals: gap from the crab's tile to her size-6 footprint on the tick it was absorbed", "1", "C", "exact" }
        specs[#specs + 1] = { "transmog_hp_jump", transmog_gain, "hp", #transmog_gain .. " transmogs: hitpoints gained across the retype tick after her damage taken and the splats' heal", "0", "B", "exact" }
        specs[#specs + 1] = { "threshold_percent", threshold_values, "percent", #threshold_values .. " transmogs: her hitpoints as the npc phase saw them on the retype tick (damage of earlier ticks only) at or under the threshold, the tick before above it", "70,50,30", "B", "exact" }
        specs[#specs + 1] = { "hp_entry_unit", { boss_hp }, "hp", "her hitpoints read before the first attack", "500", "A", "exact" }
        if crab_hp ~= nil then specs[#specs + 1] = { "crab_hp_entry_unit", { crab_hp }, "hp", "readout when the first crab stood", "16", "A", "exact" } end
        if slug_hp ~= nil then specs[#specs + 1] = { "blood_spawn_hp_entry_unit", { slug_hp }, "hp", "readout when the first blood spawn stood", "10", "A", "exact" } end

        -- crab sets and walks
        local crab_counts = {}
        local crab_offsets = {}
        for k = 1, #boss_retypes do
            local rt = boss_retypes[k].tick
            local n = 0
            for s = 1, #spawn_rows do
                if spawn_rows[s].tick >= rt and spawn_rows[s].tick <= rt + 1 and spawn_rows[s].slot ~= ws then
                    n = n + 1
                    crab_offsets[#crab_offsets + 1] = spawn_rows[s].tick - rt
                end
            end
            crab_counts[#crab_counts + 1] = n
        end
        specs[#specs + 1] = { "crab_count_entry", crab_counts, "count", "Matomenos per spawn event, solo Entry", "2", "D", "exact" }
        specs[#specs + 1] = { "crab_spawn_tick", crab_offsets, "ticks", #crab_offsets .. " crabs, spawn tick minus transmog tick", "0", "B", "exact" }
        local crab_step = {}
        for key, crab in pairs(crab_set) do
            t.ticks(1)
            local last = nil
            local _, own = t.ticklog.rows({ kind = "npc_tile", slot = crab.slot })
            t.ticks(1)
            for k = 1, #own do
                local row = own[k]
                if row.tick >= crab.tick then
                    if last ~= nil and row.tick - last.tick == 1 then
                        crab_step[#crab_step + 1] = math.max(math.abs(row.x - last.x), math.abs(row.z - last.z))
                    end
                    last = row
                end
            end
        end
        if #crab_step > 0 then
            specs[#specs + 1] = { "crab_walk", crab_step, "tiles", #crab_step .. " consecutive-tick steps", "1", "B", "exact" }
        end

        -- blood spawns: steps, and how many one throw's dodged splats let loose
        local slug_pairs = 0
        local slug_steps = 0
        local slug_stalls = 0
        local boss_end = 99999999
        for k = 1, #free_rows do
            if free_rows[k].slot == ws then boss_end = free_rows[k].tick end
        end
        local slug_spawns = {}
        for k = 1, #spawn_rows do
            if spawn_rows[k].slot ~= ws and not retype_ticks[spawn_rows[k].tick] and not retype_ticks[spawn_rows[k].tick - 1] then
                slug_spawns[#slug_spawns + 1] = spawn_rows[k]
            end
        end
        for s = 1, #slug_spawns do
            t.ticks(1)
            local sp = slug_spawns[s]
            local end_tick = boss_end
            for k = 1, #death_rows do
                if death_rows[k].slot == sp.slot and death_rows[k].tick >= sp.tick and death_rows[k].tick < end_tick then end_tick = death_rows[k].tick end
            end
            local pos = {}
            local cur = { x = sp.x, z = sp.z }
            local by_tick = {}
            local _, own = t.ticklog.rows({ kind = "npc_tile", slot = sp.slot })
            t.ticks(1)
            for k = 1, #own do
                if own[k].tick >= sp.tick then by_tick[own[k].tick] = own[k] end
            end
            for tk = sp.tick, end_tick do
                if by_tick[tk] ~= nil then cur = { x = by_tick[tk].x, z = by_tick[tk].z } end
                pos[tk] = cur
            end
            local run = 0
            for tk = sp.tick + 1, end_tick do
                local moved = math.max(math.abs(pos[tk].x - pos[tk - 1].x), math.abs(pos[tk].z - pos[tk - 1].z))
                if moved == 0 then
                    run = run + 1
                else
                    if run > 0 and run < 5 then slug_stalls = slug_stalls + 1 end
                    if run >= 5 then slug_pairs = slug_pairs - run end
                    run = 0
                end
                slug_pairs = slug_pairs + 1
                if moved == 1 then slug_steps = slug_steps + 1 end
            end
            if run >= 5 then slug_pairs = slug_pairs - run end
        end
        if slug_pairs > 0 then
            specs[#specs + 1] = { "blood_spawn_step", { math.floor(1000 * slug_steps / slug_pairs) }, "permille", slug_steps .. " one-tile steps of " .. slug_pairs .. " pairs over " .. #slug_spawns .. " slugs with " .. slug_stalls .. " stalls of 1-4 ticks", "997-1000", "B", "range" }
        end
        -- a throw none of whose splats I stood on: the blood spawns that appear at its splats' expiry
        local dodged_spawns = {}
        local dodged_throws = 0
        for k = 1, #throws do
            t.ticks(1)
            local th = throws[k]
            local stood_any = false
            local expiry = {}
            for g = 1, #th.group do
                for p = 1, #pools do
                    if pools[p].x == th.group[g].dst_x and pools[p].z == th.group[g].dst_z and pools[p].from >= th.tick and pools[p].from <= th.tick + 15 then
                        expiry[#expiry + 1] = pools[p]
                        for tk = pools[p].from, pools[p].to do
                            local at = player_at[tk - 1]
                            if at ~= nil and at.x == pools[p].x and at.z == pools[p].z then stood_any = true end
                        end
                    end
                end
            end
            if not stood_any and #expiry > 0 then
                dodged_throws = dodged_throws + 1
                local n = 0
                for s = 1, #slug_spawns do
                    for e = 1, #expiry do
                        if math.abs(slug_spawns[s].tick - expiry[e].to) <= 1 and math.max(math.abs(slug_spawns[s].x - expiry[e].x), math.abs(slug_spawns[s].z - expiry[e].z)) <= 1 then n = n + 1 break end
                    end
                end
                dodged_spawns[#dodged_spawns + 1] = n
            end
        end
        local dodged_top = 0
        for k = 1, #dodged_spawns do if dodged_spawns[k] > dodged_top then dodged_top = dodged_spawns[k] end end
        if #dodged_spawns > 0 then
            specs[#specs + 1] = { "blood_spawn_dodged_cap", { dodged_top }, "count", "most blood spawns from one throw whose splats I never stood on, over " .. dodged_throws .. " such throws: " .. table.concat(dodged_spawns, " "), "1", "D", "exact" }
        end

        -- her death
        t.ticks(1)
        if death_a_tick ~= nil and death_b_tick ~= nil and free_tick ~= nil then
            specs[#specs + 1] = { "death_a_len", { death_b_tick - death_a_tick }, "ticks", "first 8093 row to first 8094 row", "3", "A", "exact" }
            specs[#specs + 1] = { "death_b_len", { free_tick - death_b_tick }, "ticks", "first 8094 row to npc_free", "4", "A", "exact" }
            specs[#specs + 1] = { "death_total", { free_tick - death_tick }, "ticks", "killing blow to npc_free", "7", "A", "exact" }
        end

        -- the dodge technique: each far move of three tiles against the splat aimed at the tile it left
        local dodge_values = {}
        local dodge_hits = 0
        for m = 1, #far_moves do
            local mv = far_moves[m]
            for k = 1, #throws do
                local th = throws[k]
                if th.main ~= nil and th.main.dst_x == mv.fx and th.main.dst_z == mv.fz and th.tick <= mv.tick + 1 and th.tick >= mv.tick - 12 then
                    local land = th.tick + math.ceil(th.main.end_cycle / 30)
                    local at = player_at[land + 1]
                    if at ~= nil then dodge_values[#dodge_values + 1] = math.max(math.abs(at.x - mv.fx), math.abs(at.z - mv.fz)) end
                    for h = 1, #pool_hit_ticks do
                        local stood = player_at[pool_hit_ticks[h] - 1]
                        if pool_hit_ticks[h] > mv.tick + 2 and stood ~= nil and stood.x == mv.fx and stood.z == mv.fz and pool_hit_ticks[h] <= land + 12 then dodge_hits = dodge_hits + 1 end
                    end
                    break
                end
            end
        end
        if #dodge_values > 0 then
            specs[#specs + 1] = { "dodge_distance", dodge_values, "tiles", #dodge_values .. " far moves: tiles from the tile she aimed at to where I stood on the tick the extras landed (one after the splat)", "3", "D", "range" }
        end

        -- text rows
        local drain_ok = 0
        local drain_text = ""
        for k = 1, #flicks do
            local f = flicks[k]
            drain_text = drain_text .. "A" .. f.a .. " equip t" .. f.equip_tick .. " Ranged -" .. f.dr .. " Attack -" .. f.da .. " Strength -" .. f.ds .. ", "
            if f.dr > 0 and f.da == 0 and f.ds == 0 and f.equip_tick <= f.a + 4 then drain_ok = drain_ok + 1 end
        end
        local drain_measured = "target_time"
        if drain_ok == 0 then drain_measured = "none observed" end
        texts[#texts + 1] = { "drain_stat", drain_measured, "target_time", "bow worn on her aim tick, whip put on before the impact: " .. drain_text .. drain_ok .. " of " .. #flicks .. " blackstorms drained Ranged only", "C" }

        local techs = {}
        techs[#techs + 1] = { "tech.sidestep_scan", lead_ok > 0 and lead_bad == 0, "stepped on tick T so the throw at T aimed at the tick T-1 tile: " .. lead_ok .. " throws aimed at the old tile, " .. lead_bad .. " at the new tile; sidesteps issued " .. sidesteps .. ", resolved on the next tick " .. sidesteps_plus1 }
        techs[#techs + 1] = { "tech.protect_magic", #protected_hits > 0 and #unprotected_hits > 0 and protected_hits[1] * 2 <= unprotected_hits[1] + 1, "Protect from Magic lit after the first unprotected blackstorm: first protected hit " .. tostring(protected_hits[1]) .. " against the first unprotected " .. tostring(unprotected_hits[1]) .. " (" .. #protected_hits .. " protected, " .. #unprotected_hits .. " unprotected hits before the first transmog)" }
        techs[#techs + 1] = { "tech.far_dodge", #dodge_values > 0 and dodge_hits == 0, #far_moves .. " three-tile moves on the tick a throw was aimed at me; " .. #dodge_values .. " resolved; splat hits on the tile I left after the move: " .. dodge_hits }
        techs[#techs + 1] = { "tech.bow_flick", drain_ok > 0, "whip equipped by tick A+4 of her aim and the Ranged level fell on the impact: " .. drain_ok .. " of " .. #flicks .. " flicks" }
        -- ANALYSIS END

        -- (7) the room's end: cleared, and the barrier is the way out
        local cl_r, cl_state = t.raid.state()
        t.check("room.cleared", cl_r == "ok" and string.find(tostring(cl_state.line), "cleared=1", 1, true) ~= nil, tostring(cl_state and cl_state.line))
        local _, before_exit = t.world.tile()
        local ex_r, ex_d = t.player.click_loc("tob_arena_barrier", 1)
        t.ticks(3)
        local _, after_exit = t.world.tile()
        t.check("room.exit", ex_r == "ok" and (before_exit.x ~= after_exit.x or before_exit.z ~= after_exit.z), "barrier clicked after the clear: " .. tostring(ex_d) .. "; tile " .. before_exit.x .. "," .. before_exit.z .. " -> " .. after_exit.x .. "," .. after_exit.z)

        for k = 1, #specs do
            local sp = specs[k]
            local vals = sp[2]
            local seen = {}
            local distinct = {}
            for v = 1, #vals do
                local key = string.format("%g", vals[v])
                if not seen[key] then
                    seen[key] = true
                    distinct[#distinct + 1] = vals[v]
                end
            end
            table.sort(distinct)
            local parts = {}
            for v = 1, #distinct do parts[#parts + 1] = string.format("%g", distinct[v]) end
            local measured = table.concat(parts, ",")
            local detail = "measured " .. measured .. " " .. sp[3] .. ", " .. sp[4] .. " (spec " .. sp[5] .. " " .. sp[3] .. ", grade " .. sp[6] .. ", tol " .. sp[7] .. ")"
            if sp[7] == "approx" then detail = detail .. "; approximation, " .. sp[8] end
            t.check("spec.maiden." .. sp[1], #distinct > 0, detail)
        end
        for k = 1, #texts do
            local tx = texts[k]
            t.check("spec.maiden." .. tx[1], tx[2] == tx[3], "measured " .. tx[2] .. "; " .. tx[4] .. " (spec " .. tx[3] .. " text, grade " .. tx[5] .. ", tol exact)")
        end
        for k = 1, #techs do
            t.check(techs[k][1], techs[k][2], techs[k][3])
        end
        -- the rows above are every spec row of her Entry table that the room let this run measure. Four of them
        -- read the room's own numbers against the table; the file ends here, after the whole room was driven.
        t.blocked("content_bug: spec rows measured out of tolerance: maiden.blood_spawn_step 983 permille vs 997-1000 (slugs stall 1-4 ticks in the east entrance pocket, CONTENT_BUGS open row, tob_maiden.rs2:1455-1490 slug walk); maiden.death_a_len 4 vs 3 and maiden.death_total 9 vs 7 (the dying_a retype lands at the engine's corpse stage K+3, tob.rs2 [ai_queue3], CONTENT_BUGS open row); maiden.blood_extra_splats 1,2 vs 2 ([proc,tob_maiden_blood_extra] tob_maiden.rs2:906-913 returns false when the scattered tile is blocked or claimed, so a throw can carry one extra). Also unmeasured: maiden.trail_damage_entry (no trail hit was taken; tob_maiden.rs2:1758-1767 gives a trail the splat damage 10+2c against the table's 2-5) and maiden.freeze_full_bonus (no ice spell or magic-bonus read in the driver)")
        return
    end,
}
