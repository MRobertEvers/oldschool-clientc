-- Theatre of Blood, Verzik Vitur, Entry mode, solo party.
return {
    id = "tob_verzik",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        -- combat stats an Entry-mode Verzik player has
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel ranged 99", "::setlevel magic 99",
        -- melee weapon and armour (Verzik P1 melee cap is 10 per hit)
        "::give abyssal_whip 1", "::give rune_full_helm 1", "::give rune_chainbody 1",
        "::give rune_platelegs 1", "::give rune_kiteshield 1",
        -- food for the fight
        -- one of each potion the Entry debug kit hands out, so it adds no more and the backpack stays food
        "::give br_4dosepotionofsaradomin 1", "::give br_4dose2restore 1",
        "::give shark 26",
    },
    run = function(t)
        for _, w in ipairs({ "abyssal_whip", "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield" }) do
            t.exec("wear-" .. w, t.player.equip, w)
        end
        local r, d = t.raid.enter("tob", "verzik", { mode = "entry" })
        t.check("verzik.enter", r == "ok", tostring(d))
        local sr, st = t.raid.state()
        t.check("verzik.state", sr == "ok" and st.started == false, tostring(st and st.line))
        t.ticklog.start()
        local boss_found, boss = t.npc.nearest("verzik_initial", 30)
        t.check("verzik.boss_present", boss_found == "ok", "boss row")
        local tr, td = t.player.talk_to("verzik_initial", 1)
        t.check("verzik.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("verzik.begin", cr == "ok", tostring(cd))
        t.ticklog.mark("room start")
        local slot_res, boss_slot = t.ticklog.slot(boss)
        t.check("verzik.slot", slot_res == "ok", "server slot " .. tostring(boss_slot))
        t.cheat("::tobboss")
        t.ticks(2)
        local _, bm = t.msg.last(6)
        local boss_line = {}
        for _, m in ipairs(bm) do boss_line[#boss_line + 1] = tostring(m.text) end
        t.check("verzik.tobboss", #boss_line > 0, table.concat(boss_line, " | "))
        local hpr, hpv = t.skill.read("hitpoints")
        t.check("verzik.hp_read", hpr == "ok", "hitpoints " .. tostring(hpv.current or hpv.level))
        local _, mk = t.ticklog.rows({ kind = "mark" })
        local mark_tick = mk[#mk].tick
        local next_t = mark_tick + 17
        local win = 0
        local hides = 3
        local hide_log = {}
        -- safety net, not the technique: protect from magic halves a bolt that finds the player
        t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)
        local ar, ad = t.player.attack("verzik_phase1", 2, 6)
        t.check("verzik.engage", ar == "ok", tostring(ad))
        local last_eat = -10
        local hidden_windows, hidden_hits, hidden_targets = 0, 0, {}
        local open_max, open_n = 0, 0
        local impact_gaps = {}
        local sinceser = mk[#mk].serial
        while true do
            local _, rt = t.ticklog.rows({ kind = "npc_retype", since = sinceser })
            if #rt > 0 then break end
            win = win + 1
            local hide = win <= hides
            local hid, back = false, false
            local eaten = 0
            while true do
                local _, tk = t.tick()
                local _, rt2 = t.ticklog.rows({ kind = "npc_retype", since = sinceser })
                if tk >= next_t + 9 or #rt2 > 0 then break end
                local _, hpn = t.skill.read("hitpoints")
                local hpv = hpn.current or hpn.level
                if hide and not hid and tk >= next_t - 4 then
                    t.player.walk_to(6426, 93, 8)
                    hid = true
                elseif hide and hid and not back and tk >= next_t + 3 then
                    t.player.walk_to(6430, 98, 8)
                    back = true
                    t.player.attack("verzik_phase1", 2, 2)
                elseif hpv < 70 and tk >= last_eat + 3 and not (hid and not back) then
                    local _, nsh = t.inv.count("shark")
                    if nsh == 0 then
                        t.check("p1.food", false, "out of sharks in window " .. win .. " at hp " .. hpv)
                        t.finish(1)
                        return
                    end
                    t.player.inv_op("shark", 1)
                    last_eat = tk
                    eaten = eaten + 1
                    t.ticks(1)
                elseif tk % 3 == 0 and not (hid and not back) then
                    t.player.attack("verzik_phase1", 2, 1)
                    t.ticks(1)
                else
                    t.ticks(1)
                end
            end
            local _, wr = t.ticklog.rows({ kind = "npc_anim", seq = 8109, since = sinceser })
            local first
            for _, r in ipairs(wr) do
                if r.tick >= next_t - 1 then first = r break end
            end
            if not first then break end
            local actual = first.tick
            sinceser = first.serial
            local _, pj = t.ticklog.rows({ kind = "projectile", since = first.serial })
            local _, hh = t.ticklog.rows({ kind = "hit_player", since = first.serial })
            local hit_txt = {}
            for _, r in ipairs(hh) do
                if r.tick <= actual + 7 then hit_txt[#hit_txt + 1] = r.tick .. ":" .. tostring(r.damage) end
            end
            local pj_txt = {}
            for _, r in ipairs(pj) do
                if r.tick <= actual + 4 then pj_txt[#pj_txt + 1] = r.tick .. ">" .. tostring(r.dst_x) .. "," .. tostring(r.dst_z) end
            end
            if hide then
                hidden_windows = hidden_windows + 1
                hidden_hits = hidden_hits + #hit_txt
                hidden_targets[#hidden_targets + 1] = (pj[1] and (pj[1].dst_x .. "," .. pj[1].dst_z) or "none")
            else
                for _, r in ipairs(hh) do
                    if r.tick <= actual + 7 then
                        open_n = open_n + 1
                        if r.damage > open_max then open_max = r.damage end
                        if pj[1] then impact_gaps[#impact_gaps + 1] = r.tick - pj[1].tick end
                    end
                end
            end
            local _, hp_end = t.skill.read("hitpoints")
            local _, sharks_left = t.inv.count("shark")
            t.check("p1.window" .. win, true, "windup " .. actual .. " (expected " .. next_t .. "); hid " .. tostring(hide) .. "; projectiles " .. table.concat(pj_txt, " ") .. "; hits " .. table.concat(hit_txt, " ") .. "; hp " .. tostring(hp_end.current or hp_end.level) .. ", ate " .. eaten .. ", sharks left " .. tostring(sharks_left))
            next_t = actual + 14
            if win >= 40 then break end
        end
        local _, rts = t.ticklog.rows({ kind = "npc_retype" })
        local rtxt = {}
        for _, r in ipairs(rts) do rtxt[#rtxt + 1] = r.tick .. ":" .. tostring(r.b) .. ">" .. tostring(r.c) end
        t.check("p1.done", true, "retypes " .. table.concat(rtxt, " "))
        -- P2: the 4-tick walk. Verzik's tiles (6431..6437, 89..95); A touches her west edge, C is one tile off it.
        local tile_a_x, tile_c_x, tile_z = 6430, 6429, 90
        local _, pre = t.ticklog.rows({ kind = "npc_retype", to_type = 8372 })
        local p2_serial = sinceser
        t.player.walk_to(tile_c_x, tile_z, 8)
        local p2_eats = 0
        while p2_eats < 4 do
            local _, hp0 = t.skill.read("hitpoints")
            local _, att0 = t.ticklog.rows({ kind = "npc_anim", type = 8372, since = p2_serial })
            local _, tk_now = t.tick()
            if (hp0.current or hp0.level) >= 90 or #att0 > 0 or tk_now >= 266 then break end
            t.player.inv_op("shark", 1)
            p2_eats = p2_eats + 1
            t.ticks(3)
        end
        t.await({ level = function()
            local _, r = t.ticklog.rows({ kind = "npc_anim", type = 8372, since = p2_serial })
            return #r > 0
        end, note = "p2 first attack" }, 80)
        local p2_cycle = 0
        local p2_rows = {}
        local hit_serial = p2_serial
        local spawn_serial = p2_serial
        local proj_serial = p2_serial
        local p2_log = {}
        local p2_steps_on_time = 0
        while true do
            local _, hp_top = t.skill.read("hitpoints")
            if (hp_top.current or hp_top.level) < 25 then break end
            local hr0, home = t.world.tile()
            if hr0 == "ok" and not (home.x == tile_c_x and home.z == tile_z) then
                t.player.walk_to(tile_c_x, tile_z, 8)
            end
            local _, att = t.ticklog.rows({ kind = "npc_anim", type = 8372, since = p2_serial })
            local last_tick, heal_tick = nil, nil
            for _, r in ipairs(att) do
                if r.seq == 8114 or r.seq == 8116 then last_tick = r.tick end
                if r.seq == 8117 then heal_tick = r.tick end
            end
            local _, rt3 = t.ticklog.rows({ kind = "npc_retype", since = p2_serial })
            local ended = false
            for _, r in ipairs(rt3) do
                if r.to_type == 8373 or r.to_type == 8374 then ended = true end
            end
            local alive_now = (t.player.alive() == "ok")
            if ended or p2_cycle >= 70 or not alive_now then break end
            local tk0 = last_tick + 4
            if heal_tick and heal_tick > last_tick then tk0 = heal_tick + 12 end
            local _, now_tick = t.tick()
            while tk0 < now_tick + 1 do tk0 = tk0 + 4 end
            p2_cycle = p2_cycle + 1
            t.await({ level = function() local _, tk = t.tick(); return tk >= tk0 - 1 end, note = "scan tick" }, 40)
            local _, hpn = t.skill.read("hitpoints")
            local hpv = hpn.current or hpn.level
            local mode = "walk"
            local target
            for _, sym in ipairs({ "tob_verzik_phase2_bloodnylocas", "tob_verzik_phase2_armourednylocas" }) do
                local nr, nrow = t.npc.nearest(sym, 8)
                if nr == "ok" and not target and math.abs(nrow.x - tile_a_x) <= 1 and math.abs(nrow.z - tile_z) <= 1 then target = sym end
            end
            if hpv < 58 then mode = "eat" elseif target then mode = "kill" end
            local vr, vrow = t.npc.state("verzik_phase2")
            local vz = (vr == "ok") and (vrow.health_ratio .. "/" .. vrow.health_scale) or vr
            local ar2, arow = t.npc.state("tob_verzik_phase2_armourednylocas")
            if ar2 == "ok" then vz = vz .. " ath " .. arow.health_ratio .. "/" .. arow.health_scale .. "@" .. arow.x .. "," .. arow.z end
            local sr, sd = "skipped", "no step"
            if true then
                sr, sd = t.player.step_tick(tile_a_x, tile_z)
                if tostring(sd):find("%(%+1%)") then p2_steps_on_time = p2_steps_on_time + 1 end
            end
            t.await({ level = function() local _, tk = t.tick(); return tk >= tk0 end, note = "attack tick" }, 20)
            if mode == "eat" then
                t.player.inv_op("shark", 1)
            elseif mode == "kill" then
                t.player.attack(target, 2, 1)
            else
                t.player.attack("verzik_phase2", 2, 1)
            end
            t.await({ level = function() local _, tk = t.tick(); return tk >= tk0 + 2 end, note = "leave tick" }, 20)
            local tlr, tl = t.world.tile()
            if tlr == "ok" and tl.x == tile_a_x and tl.z == tile_z then
                t.player.step_tick(tile_c_x, tile_z)
            elseif tlr == "ok" and not (tl.x == tile_c_x and tl.z == tile_z) and mode ~= "kill" then
                t.player.walk_to(tile_c_x, tile_z, 8)
            end
            sd = mode .. ": " .. tostring(sd) .. (target and (" target " .. target) or "")
            p2_log[#p2_log + 1] = tk0 .. ":" .. hpv
            local _, hr = t.ticklog.rows({ kind = "hit_player", since = hit_serial })
            local ht = {}
            for _, r in ipairs(hr) do
                ht[#ht + 1] = r.tick .. ":" .. tostring(r.damage) .. "/" .. tostring(r.npc_type) .. "/" .. tostring(r.seq)
                hit_serial = r.serial
            end
            local _, spn = t.ticklog.rows({ kind = "npc_spawn", since = spawn_serial })
            local st = {}
            for _, r in ipairs(spn) do
                st[#st + 1] = r.tick .. ":" .. tostring(r.type) .. "@" .. tostring(r.x) .. "," .. tostring(r.z)
                spawn_serial = r.serial
            end
            local _, prj = t.ticklog.rows({ kind = "projectile", since = proj_serial })
            local pt = {}
            for _, r in ipairs(prj) do
                pt[#pt + 1] = r.tick .. ":" .. tostring(r.spotanim) .. ">" .. tostring(r.dst_x) .. "," .. tostring(r.dst_z)
                proj_serial = r.serial
            end
            p2_rows[#p2_rows + 1] = { "p2.cycle" .. p2_cycle, "attack tick " .. tk0 .. ", hp before " .. hpv .. ", verzik " .. vz .. ", step A " .. tostring(sd) .. "; hits " .. table.concat(ht, " ") .. "; spawns " .. table.concat(st, " ") .. "; proj " .. table.concat(pt, " ") }
        end
        for _, row in ipairs(p2_rows) do t.check(row[1], true, row[2]) end
        t.check("p2.walk", true, "cycles " .. p2_cycle .. " hp by cycle " .. table.concat(p2_log, " "))
        t.check("p2.walk", true, "cycles " .. p2_cycle .. " hp by cycle " .. table.concat(p2_log, " ") .. "; step A resolved on the scan tick (+1) in " .. p2_steps_on_time .. " of " .. p2_cycle)
        -- technique rows, from the tick log
        t.check("tech.p1_pillar_cover", hidden_windows == 3 and hidden_hits == 0,
            "windows behind the west pillar: " .. hidden_windows .. ", hit_player rows in them: " .. hidden_hits .. ", bolt aimed at " .. table.concat(hidden_targets, " | "))
        t.check("tech.p1_prayer_halves", open_n > 0 and open_max <= 68,
            "unhidden bolts that landed: " .. open_n .. ", largest hit under Protect from Magic " .. open_max .. " (unprayed max 137, halved 68)")
        -- spec rows measured from the log
        local _, mks = t.ticklog.rows({ kind = "mark" })
        local room_start = mks[1].tick - 1
        local _, w1 = t.ticklog.rows({ kind = "npc_anim", slot = boss_slot, seq = 8109 })
        local _, pj1 = t.ticklog.rows({ kind = "projectile", spotanim = 1580 })
        local _, rt_all = t.ticklog.rows({ kind = "npc_retype", slot = boss_slot })
        local leave_tick, id_tick
        for _, r in ipairs(rt_all) do
            if r.from_type == 8370 and r.to_type == 8371 then leave_tick = r.tick end
            if r.from_type == 8371 and r.to_type == 8372 then id_tick = r.tick end
        end
        local gaps_text, gaps_list = select(2, t.ticklog.gaps(boss_slot, "npc_anim", { seq = 8109 })), select(3, t.ticklog.gaps(boss_slot, "npc_anim", { seq = 8109 }))
        local distinct = {}
        local seen = {}
        for _, g in ipairs(gaps_list) do
            if not seen[g] then seen[g] = true distinct[#distinct + 1] = g end
        end
        t.check("spec.verzik.p1_cadence", #gaps_list >= 10 and #distinct == 1,
            "measured " .. table.concat(distinct, ",") .. " ticks, " .. #gaps_list .. " of " .. #gaps_list .. " gaps (spec 14 ticks, grade B, tol exact)")
        t.check("spec.verzik.p1_first_windup", #w1 > 0,
            "measured " .. (w1[1].tick - room_start) .. " ticks, room start taken as the mark tick minus 1 (DRIVER_NOTES) (spec 19 ticks, grade B, tol +-1)")
        local launch_gaps, flights = {}, {}
        local seen_l, seen_f = {}, {}
        for _, w in ipairs(w1) do
            for _, pr in ipairs(pj1) do
                if pr.tick >= w.tick and pr.tick <= w.tick + 5 and pr.src_x ~= nil then
                    local d = pr.tick - w.tick
                    if not seen_l[d] then seen_l[d] = true launch_gaps[#launch_gaps + 1] = d end
                    local f = pr.end_cycle
                    if not seen_f[f] then seen_f[f] = true flights[#flights + 1] = f end
                    break
                end
            end
        end
        t.check("spec.verzik.p1_launch_after_windup", #launch_gaps > 0,
            "measured " .. table.concat(launch_gaps, ",") .. " ticks, over " .. #w1 .. " wind-ups (spec 3 ticks, grade D, tol exact)")
        t.check("spec.verzik.p1_bolt_flight", #flights > 0,
            "measured " .. table.concat(flights, ",") .. " cycles, projectile 1580 end_cycle (spec 110 cycles, grade D, tol exact)")
        local seen_i, imp = {}, {}
        for _, g in ipairs(impact_gaps) do
            if not seen_i[g] then seen_i[g] = true imp[#imp + 1] = g end
        end
        t.check("spec.verzik.p1_bolt_impact_after_launch", #imp > 0,
            "measured " .. table.concat(imp, ",") .. " ticks, " .. #impact_gaps .. " bolts that hit the player (spec 3 ticks, grade D, tol exact)")
        local collapsed = 0
        for _, r in ipairs(rt_all) do end
        local _, rt_pillars = t.ticklog.rows({ kind = "npc_retype" })
        for _, r in ipairs(rt_pillars) do
            if r.from_type == 8379 and r.to_type == 8377 then collapsed = collapsed + 1 end
        end
        t.check("spec.verzik.pillar_count", collapsed > 0,
            "measured " .. collapsed .. " count, pillars that changed form 8379 to 8377 when the shield broke (spec 6 count, grade B, tol exact)")
        local _, att2 = t.ticklog.rows({ kind = "npc_anim", slot = boss_slot, type = 8372 })
        local atk = {}
        for _, r in ipairs(att2) do
            if r.seq == 8114 or r.seq == 8116 then atk[#atk + 1] = r.tick end
        end
        t.check("spec.verzik.p2_id_after_phase_event", leave_tick ~= nil and id_tick ~= nil,
            "measured " .. (id_tick - leave_tick) .. " ticks, P1 form 8370 to 8371 at " .. leave_tick .. ", P2 id at " .. id_tick .. " (spec 13 ticks, grade B, tol +-1)")
        t.check("spec.verzik.p2_first_attack_after_id", #atk > 0,
            "measured " .. (atk[1] - id_tick) .. " ticks (spec 3 ticks, grade B, tol exact)")
        t.check("spec.verzik.p2_first_attack_after_phase_event", #atk > 0,
            "measured " .. (atk[1] - leave_tick) .. " ticks (spec 16 ticks, grade B, tol +-1)")
        local cad, seen_c = {}, {}
        for k = 2, #atk do
            local g = atk[k] - atk[k - 1]
            if not seen_c[g] then seen_c[g] = true cad[#cad + 1] = g end
        end
        t.check("spec.verzik.p2_cadence", #atk >= 8 and #cad == 1,
            "measured " .. table.concat(cad, ",") .. " ticks, " .. (#atk - 1) .. " of " .. (#atk - 1) .. " gaps (spec 4 ticks, grade B, tol exact)")
        local _, zaps = t.ticklog.rows({ kind = "projectile", spotanim = 1585 })
        local zg = {}
        for k = 2, #zaps do zg[#zg + 1] = (zaps[k].tick - zaps[k - 1].tick) // 4 - 1 end
        t.check("spec.verzik.p2_zap_floor", #zg > 0,
            "measured " .. table.concat(zg, ",") .. " count, attacks between " .. #zaps .. " lightning balls (spec 4 count, grade B, tol exact)")
        local _, purples = t.ticklog.rows({ kind = "projectile", spotanim = 1586 })
        local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
        local pcast = purples[1] and purples[1].tick
        local crab_dt, land_dt
        for _, r in ipairs(spawns) do
            if pcast and (r.type == 8381 or r.type == 8382 or r.type == 8383) and not crab_dt then crab_dt = r.tick - pcast end
            if pcast and r.type == 8384 and not land_dt then land_dt = r.tick - pcast end
        end
        t.check("spec.verzik.p2_purple_first", pcast ~= nil,
            "measured " .. ((pcast - atk[1]) // 4) .. " count, first Athanatos cast on tick " .. tostring(pcast) .. ", first attack " .. atk[1] .. " (spec 0-12 count, grade B, tol range)")
        t.check("spec.verzik.p2_crabs_with_purple", crab_dt ~= nil,
            "measured " .. tostring(crab_dt) .. " ticks, exploding nylocas spawn row against the cast (spec 0 ticks, grade B, tol +-1)")
        t.check("spec.verzik.p2_purple_land", land_dt ~= nil,
            "measured " .. tostring(land_dt) .. " ticks, Athanatos 8384 spawn row against the cast (spec 6 ticks, grade B, tol +-1)")
        local _, bombs = t.ticklog.rows({ kind = "projectile", spotanim = 1583 })
        local _, bomb_gfx = t.ticklog.rows({ kind = "map_spotanim", spotanim = 1584 })
        local bf, seen_b = {}, {}
        for _, g in ipairs(bomb_gfx) do
            local best
            for _, b in ipairs(bombs) do
                if b.tick <= g.tick and (best == nil or b.tick > best) then best = b.tick end
            end
            if best and not seen_b[g.tick - best] then seen_b[g.tick - best] = true bf[#bf + 1] = g.tick - best end
        end
        t.check("spec.verzik.p2_bomb_flight", #bf > 0,
            "measured " .. table.concat(bf, ",") .. " ticks, urnbomb 1583 launch to the 1584 ground graphic (spec ? ticks, grade E, tol approx); approximation, M80")
        -- the fight cannot finish: Phase 2's Athanatos is the Normal 180-hitpoint record in Entry
        t.blocked("content_bug: tob.npc:2058 [tob_verzik_phase2_armourednylocas] hitpoints=180 in Entry mode (spec verzik.entry_athanatos_hp 30, grade A, cache_npc_verzik.txt:700); the 180-hp Athanatos heals Verzik every purple_heal tick, her Phase 2 bar held at 21/30 for " .. p2_cycle .. " cycles of the 4-tick walk, and the Normal-figure crab blast (58 of 63, spec gap) plus lightning 33-48 outpace the sharks, so Phase 3 and the exit are not reachable solo")
        return
    end,
}
