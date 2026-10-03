-- Theatre of Blood, room 4 (Sotetseg), Entry mode, solo party.
-- Spec table: docs/minigames/theater_of_blood/encounters/sotetseg.tsv.
-- The fight is driven in three stages: darts from range with Protect from Magic up
-- until the first maze, darts from a second distance until the second maze, then the
-- rapier at melee range until he dies.  The shadow-realm path is never lit by the
-- content (tob_sotetseg.rs2 ~tob_sote_light_path), so the mazes are run from the
-- start tile with the timing rows measured and the unlit path reported at the end.
return {
    id = "tob_sotetseg",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        -- the combat stats an Entry-mode Sotetseg raider has
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel ranged 99",
        -- thrown weapon for the range stages and a melee weapon for the last one
        "::give dragon_dart 800", "::give scythe_of_vitur 1",
        -- a longbow from further out for the second range stage
        "::give twisted_bow 1", "::give dragon_arrow 600",
        -- food and prayer restore a player brings to the room
        "::give shark 18", "::give 4doseprayerrestore 3",
    },

    run = function(t)
        local BOSS = "tob_sotetseg_combat_story"
        local MELEE, BALL = 8138, 8139            -- attack seqs (ENCOUNTER_TIMING 5.1)
        local PORTAL = 8142                       -- maze proc seq
        local DEATH_BALL, PLAIN_BALL = 1604, 1606 -- projectile spotanims

        ------------------------------------------------------------------
        -- 0. the three modes' cache records, read from each room's own boss
        ------------------------------------------------------------------
        local modes = { "normal", "hard", "entry" }
        local rec = {}
        for i = 1, 3 do
            local er, ed = t.raid.enter("tob", "sotetseg", { mode = modes[i] })
            t.check("enter." .. modes[i], er == "ok", tostring(ed))
            t.cheat("::tobboss")
            t.ticks(2)
            local _, msgs = t.msg.last(4)
            local line = ""
            for _, m in ipairs(msgs) do
                if string.find(m.text, "tobboss record=", 1, true) then
                    line = m.text
                    break
                end
            end
            local hp = tonumber(string.match(line, "hp=(%d+)"))
            local def = tonumber(string.match(line, "def=(%d+)"))
            local defbase = tonumber(string.match(line, "def=%d+ of (%d+)"))
            rec[modes[i]] = { hp = hp, def = def, defbase = defbase }
            t.check("boss." .. modes[i], hp ~= nil and defbase ~= nil, line)
            if i < 3 then
                local lr, ld = t.raid.leave()
                t.check("leave." .. modes[i], lr == "ok", tostring(ld))
            end
        end

        ------------------------------------------------------------------
        -- 1. the Entry room, log on, boss slot, barrier by the player's own click
        ------------------------------------------------------------------
        local sr, st = t.raid.state()
        t.check("state", sr == "ok" and st.mode == "entry" and st.started == false, tostring(st and st.line))
        local lr0, lg0 = t.ticklog.start()
        t.check("ticklog", lr0 == "ok", tostring(lg0))
        local nr, nrow = t.npc.by_symbol(BOSS)
        t.check("boss.row", nr == "ok", "client slot " .. tostring(nrow and nrow.slot))
        local wr, wslot = t.ticklog.slot(nrow)
        t.check("boss.slot", wr == "ok", "world slot " .. tostring(wslot))
        local bx, bz = nrow.x, nrow.z
        t.exec("equip.darts", t.player.equip, "dragon_dart")
        -- Protect from Magic is up before the first ball is thrown
        t.exec("prayer.magic", t.prayer.set, "protectfrommagic", true)
        local fr, fight, ftext = t.raid.start_tile()
        t.check("start_tile", fr == "ok", tostring(ftext))
        t.player.walk_to(fight.x, fight.z - 2, 20)
        local _, wt = t.world.tile()
        t.check("walk.barrier", wt.z == fight.z - 2, "at " .. wt.x .. "," .. wt.z)
        local cr, cd = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", cr == "ok", tostring(cd))
        local pr, pd = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.begin", pr == "ok", tostring(pd))
        t.ticklog.mark("room start")
        local mr, mrows = t.ticklog.rows({ kind = "mark" })
        local mark_tick, mark_serial = mrows[#mrows].tick, mrows[#mrows].serial
        t.expect("fight.begins", t.msg.expect("The fight begins"))
        -- the mark is written one tick after the room start (DRIVER_NOTES, the tick log has no room-start row)
        local room_start = mark_tick - 1

        ------------------------------------------------------------------
        -- 2. stage 0: darts from range, first ball UNPRAYED (hit size, prayer block)
        ------------------------------------------------------------------
        t.exec("attack.start", t.player.attack, BOSS, 2, 40)
        local H, hit_dmg, off_launch = nil, nil, nil
        local prayer_dropped = false
        local refused_n, ok_issue = 0, nil
        local prayer_disable = -1
        -- drain his Defence the way a defence-lowering special does and read the floor
        t.cheat("::tobsotedrain 120")
        t.ticks(3)
        t.cheat("::tobsotestate")
        t.ticks(2)
        local _, dmsgs = t.msg.last(4)
        local floor_def = nil
        for _, m in ipairs(dmsgs) do
            local v = string.match(m.text, "tobsotestate form=%a+ def=(%d+)")
            if v then
                floor_def = tonumber(v)
                break
            end
        end
        t.check("defence.floor_read", floor_def ~= nil, "defence after a drain of 120: " .. tostring(floor_def))

        ------------------------------------------------------------------
        -- 3. the stages
        ------------------------------------------------------------------
        local cursor = mark_serial
        local procs, reacts = {}, {}         -- retype rows: proc k, re-activation k
        local maze = {}                      -- per-maze measurements
        local distances = {}                 -- player distance at each attack, filled afterwards
        local last_attack_press = 0
        local sharks_gone = false
        local death_tick = nil
        local restore_def = nil
        local pts_low_drinks = 0
        local mazes_done = 0
        local hp_trail, trail_pending = {}, false
        local healed, hp_pre_maze, hp_post_maze = false, nil, nil
        for stage = 1, 4 do
            if healed then break end
            if stage >= 2 then
                -- a defence-lowering special again: the maze restored his Defence
                t.cheat("::tobsotedrain 120")
            end
            if stage == 2 then
                -- a second distance for the death ball: the bow from six tiles out
                t.exec("stage2.bow", t.player.equip, "twisted_bow")
                t.exec("stage2.arrows", t.player.equip, "dragon_arrow")
                t.player.walk_to(bx + 1, bz - 6, 30)
                local _, at = t.world.tile()
                t.check("stage2.stand", at.z == bz - 6, "standing at " .. at.x .. "," .. at.z)
                t.exec("stage2.attack", t.player.attack, BOSS, 2, 40)
            end
            if stage >= 3 then
                if stage == 3 then
                    t.exec("stage3.equip", t.player.equip, "scythe_of_vitur")
                end
                t.exec("stage" .. stage .. ".attack", t.player.attack, BOSS, 2, 60)
            end
            local budget = 250
            if stage == 1 then budget = 200 end
            if stage == 2 then budget = 120 end
            local event = nil
            local fguard = 0
            local last_hit_tick = select(2, t.tick())
            while event == nil and fguard < budget do
                t.ticks(1)
                fguard = fguard + 1
                local now = select(2, t.tick())
                local _, hpr = t.skill.read("hitpoints")
                local hp = hpr.level
                if trail_pending then
                    local _, tm = t.msg.last(4)
                    for _, m in ipairs(tm) do
                        local v = string.match(m.text, "tobboss record=%a+ room_mode=%d+ hp=(%d+)")
                        if v then
                            hp_trail[#hp_trail + 1] = now .. ":" .. v
                            break
                        end
                    end
                    trail_pending = false
                end
                if fguard % 10 == 0 then
                    local _, tpp = t.skill.read("prayer")
                    local _, _, tps = t.prayer.read()
                    t.check("stage" .. stage .. ".watch", true, "tick " .. now .. " hp " .. hp .. " prayer pts " .. tostring(type(tpp) == "table" and tpp.level or "?") .. " magic prayer " .. tostring(tps and tps.protectfrommagic) .. " boss " .. tostring(hp_trail[#hp_trail]))
                    t.cheat("::tobboss")
                    trail_pending = true
                end
                local eat_below = 50
                if stage >= 3 then eat_below = 70 end
                if hp < eat_below then
                    local _, nsh = t.inv.count("shark")
                    if nsh and nsh > 0 then t.player.inv_op("shark", 1) end
                end
                if stage == 1 and H == nil then
                    local _, pj = t.ticklog.rows({ kind = "projectile", since = mark_serial })
                    local nb, lt = 0, nil
                    for _, r in ipairs(pj) do
                        if r.spotanim == PLAIN_BALL then
                            nb = nb + 1
                            lt = r.tick
                        end
                    end
                    if nb >= 9 and hp < 80 and not prayer_dropped then
                        t.player.inv_op("shark", 1)
                    end
                    if nb >= 10 and not prayer_dropped and hp >= 70 then
                        -- the tenth ball is the last one before the death ball: let it land unprayed
                        local dr0, dd0 = t.prayer.set("protectfrommagic", false)
                        t.check("prayer.drop", dr0 == "ok", "prayer off after the tenth ball launched at tick " .. tostring(lt) .. ": " .. tostring(dd0))
                        prayer_dropped = true
                        off_launch = lt
                    end
                    if prayer_dropped then
                        local _, hq = t.ticklog.rows({ kind = "hit_player", since = mark_serial })
                        for _, r in ipairs(hq) do
                            if r.npc_slot == wslot and r.damage > 0 and r.tick >= off_launch then
                                H = r.tick
                                hit_dmg = r.damage
                                break
                            end
                        end
                        if H ~= nil then
                            for k = 1, 12 do
                                local _, issue = t.tick()
                                local prr = t.prayer.set("protectfrommagic", true)
                                if prr == "ok" then
                                    ok_issue = issue
                                    break
                                end
                                refused_n = refused_n + 1
                                t.ticks(1)
                            end
                            t.check("prayer.disable_window", ok_issue ~= nil and refused_n >= 1,
                                "refused " .. refused_n .. " presses after the unprayed splat of " .. hit_dmg .. " at tick " .. H .. ", accepted on the press issued at tick " .. tostring(ok_issue))
                            if ok_issue then prayer_disable = ok_issue + 1 - H end
                        end
                    end
                end
                if fguard % 4 == 0 and not (prayer_dropped and H == nil) then
                    local _, _, pset = t.prayer.read()
                    if pset ~= nil and not pset.protectfrommagic then
                        t.prayer.set("protectfrommagic", true)
                    end
                    local _, ppts = t.skill.read("prayer")
                    local plevel = type(ppts) == "table" and ppts.level or 0
                    if plevel <= 25 then
                        local names = { "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore" }
                        for _, nm in ipairs(names) do
                            local _, have = t.inv.count(nm)
                            if have and have > 0 then
                                t.player.inv_op(nm, 1)
                                pts_low_drinks = pts_low_drinks + 1
                                break
                            end
                        end
                    end
                end
                local _, hn = t.ticklog.rows({ kind = "hit_npc", since = cursor, slot = wslot })
                if #hn > 0 then
                    last_hit_tick = hn[#hn].tick
                    cursor = math.max(cursor, hn[#hn].serial)
                end
                if now - last_hit_tick > 14 then
                    t.player.attack(BOSS, 2, 30)
                    last_hit_tick = now
                end
                local _, rt = t.ticklog.rows({ kind = "npc_retype", since = mark_serial, slot = wslot })
                if #rt > #procs + #reacts and mazes_done < 2 then
                    event = "proc"
                    procs[#procs + 1] = rt[#rt]
                end
                local _, dr = t.ticklog.rows({ kind = "npc_death", since = mark_serial, slot = wslot })
                if #dr > 0 then
                    event = "death"
                    death_tick = dr[1].tick
                end
            end
            t.check("stage" .. stage .. ".event", true, tostring(event or "budget") .. " after " .. fguard .. " ticks")
            if event == "death" then
                break
            end
            if event == "proc" then

            --------------------------------------------------------------
            -- the maze, run from the start tile
            --------------------------------------------------------------
            local mi = mazes_done + 1
            local mz = { stage = mi }
            maze[mi] = mz
            local pt = procs[mi]
            local P = pt.tick
            mz.proc = P
            local lv, lg = 0, 0
            while lv ~= 3 and lg < 12 do
                t.ticks(1)
                lg = lg + 1
                local _, l = t.world.level()
                lv = l
            end
            local _, spt = t.world.tile()
            local sx, sz = spt.x, spt.z
            mz.sx, mz.sz = sx, sz
            t.check("maze" .. mi .. ".landed", lv == 3, "level " .. tostring(lv) .. " tile " .. sx .. "," .. sz
                .. " at tick " .. tostring(select(2, t.tick())) .. " proc tick " .. P)
            -- the grid: columns west and east of the start tile that carry the darktile loc, and rows
            local _, hz0 = t.world.hazard_at(sx, sz, 3)
            local f0 = hz0.locs[1]
            local dark_id = f0 and (f0.id or f0.loc or f0.loc_id) or -1
            local west, east = 0, 0
            for dx = 1, 16 do
                local _, hh = t.world.hazard_at(sx - dx, sz, 3)
                local ff = hh.locs[1]
                if ff and (ff.id or ff.loc or ff.loc_id) == dark_id then west = dx else break end
            end
            for dx = 1, 16 do
                local _, hh = t.world.hazard_at(sx + dx, sz, 3)
                local ff = hh.locs[1]
                if ff and (ff.id or ff.loc or ff.loc_id) == dark_id then east = dx else break end
            end
            local rows_n = 0
            for dz = 0, 17 do
                local _, hh = t.world.hazard_at(sx, sz + dz, 3)
                local ff = hh.locs[1]
                if ff and (ff.id or ff.loc or ff.loc_id) == dark_id then rows_n = rows_n + 1 else break end
            end
            mz.grid_w = west + east + 1
            mz.grid_h = rows_n
            mz.start_col = west
            t.check("maze" .. mi .. ".grid", mz.grid_w > 0, "grid " .. mz.grid_w .. " wide, " .. mz.grid_h .. " high, start column " .. west)
            -- stunned: a step asked on the landing tick is dropped, the first one that takes is proc+5
            local hp_by_tick = {}
            local _, hpr0 = t.skill.read("hitpoints")
            local _, issue0 = t.tick()
            local s0r, s0d = t.player.step_tick(sx, sz + 1, 1)
            t.check("maze" .. mi .. ".stall", s0r == "timeout",
                "step asked at tick " .. issue0 .. " during the stun: " .. tostring(s0r) .. " " .. tostring(s0d))
            local wt0 = 0
            while select(2, t.tick()) < P + 4 and wt0 < 6 do
                t.ticks(1)
                wt0 = wt0 + 1
            end
            local _, issue1 = t.tick()
            hp_by_tick[issue1 + 1] = hpr0.level
            local s1r, s1d = t.player.step_tick(sx, sz + 1)
            mz.first_move = tonumber(string.match(tostring(s1d), "resolved at tick (%d+)"))
            t.check("maze" .. mi .. ".first_step", s1r == "ok" and mz.first_move ~= nil,
                "proc tick " .. P .. ": " .. tostring(s1d))
            -- the realm's path is never lit: the whole log since the proc holds no loc_set row
            local _, lrows = t.ticklog.rows({ kind = "loc_set", since = pt.serial })
            mz.lit_rows = #lrows
            -- stage 2 walks to the fourth row and back (tornado and rag rows); stage 1 only steps back
            local route = { { 0, 0 } }
            if mi == 2 then
                route = { { 0, 2 }, { 0, 3 }, { 0, 3 }, { 0, 3 }, { 0, 2 }, { 0, 1 }, { 0, 0 } }
                local hp_now = hpr0.level
                local ate = 0
                while hp_now < 90 and ate < 5 do
                    t.player.inv_op("shark", 1)
                    ate = ate + 1
                    t.ticks(3)
                    local _, hq = t.skill.read("hitpoints")
                    hp_now = hq.level
                end
            end
            local cur = { 1 }
            local ended_early = false
            for i = 1, #route do
                local _, lvq = t.world.level()
                if lvq ~= 3 then
                    ended_early = true
                    break
                end
                local want = route[i][2]
                local step_to = sz + want
                local _, hq = t.skill.read("hitpoints")
                local _, issue = t.tick()
                hp_by_tick[issue + 1] = hq.level
                if want ~= cur[1] then
                    local rr, rd = t.player.step_tick(sx, step_to)
                    cur[1] = want
                    t.check("maze" .. mi .. ".walk" .. i, rr == "ok", tostring(rd))
                else
                    t.ticks(1)
                end
                if hq.level < 40 then
                    t.player.inv_op("shark", 1)
                end
            end
            mz.hp_by_tick = hp_by_tick
            -- linger on the start tile (on the path) until three chips have landed
            local linger = 0
            local chips = 0
            while chips < 3 and linger < 40 and not ended_early do
                t.ticks(1)
                linger = linger + 1
                local _, lvq = t.world.level()
                if lvq ~= 3 then
                    ended_early = true
                    break
                end
                local _, hq = t.skill.read("hitpoints")
                if hq.level < 40 then
                    t.player.inv_op("shark", 1)
                end
                local _, hh = t.ticklog.rows({ kind = "hit_player", since = pt.serial })
                chips = 0
                for _, r in ipairs(hh) do
                    if r.npc_slot == -1 and r.damage >= 1 and r.damage <= 3 and r.tick > P + 3 then chips = chips + 1 end
                end
            end
            mz.ended_early = ended_early
            t.check("maze" .. mi .. ".chips", chips >= 3 or ended_early,
                chips .. " chip hits on the start tile after " .. linger .. " ticks, maze ended under the runner early: " .. tostring(ended_early))
            -- leave the grid: south edge, the step resolving on cycle tick 3 (stage 1) or 0 (stage 2)
            local want_mod = 2
            if mi == 2 then want_mod = 3 end
            local wguard = 0
            while select(2, t.tick()) % 4 ~= want_mod and wguard < 8 and not ended_early do
                t.ticks(1)
                wguard = wguard + 1
            end
            if not ended_early then
                local xr, xd = t.player.step_tick(sx, sz - 1)
                mz.off_tick = tonumber(string.match(tostring(xd), "resolved at tick (%d+)"))
                t.check("maze" .. mi .. ".leave_grid", xr == "ok" and mz.off_tick ~= nil, tostring(xd))
            end
            -- the re-activation: the next retype row on his slot
            local rguard = 0
            local react = nil
            while react == nil and rguard < 30 do
                t.ticks(1)
                rguard = rguard + 1
                local _, rt2 = t.ticklog.rows({ kind = "npc_retype", since = pt.serial, slot = wslot })
                if #rt2 >= 1 then react = rt2[1] end
            end
            t.check("maze" .. mi .. ".reactivated", react ~= nil, "re-activation tick " .. tostring(react and react.tick) .. " proc tick " .. P)
            if react == nil then
                t.blocked("maze " .. mi .. " did not re-activate within 30 ticks of stepping off the grid")
                return
            end
            reacts[#reacts + 1] = react
            mz.react = react.tick
            -- back in the arena, Defence restored after the maze
            local lw = 0
            local lvl2 = 3
            while lvl2 ~= 0 and lw < 12 do
                t.ticks(1)
                lw = lw + 1
                local _, l2 = t.world.level()
                lvl2 = l2
            end
            t.check("maze" .. mi .. ".returned", lvl2 == 0, "back on level " .. lvl2 .. " after " .. lw .. " ticks")
            if mi == 1 then
                t.cheat("::tobsotestate")
                t.ticks(2)
                local _, rm = t.msg.last(4)
                for _, m in ipairs(rm) do
                    local v = string.match(m.text, "tobsotestate form=%a+ def=(%d+) of (%d+)")
                    if v then
                        restore_def = { def = tonumber(v), base = tonumber(string.match(m.text, "def=%d+ of (%d+)")) }
                        break
                    end
                end
                t.check("maze1.defence_read", restore_def ~= nil, restore_def and ("defence " .. restore_def.def .. " of " .. restore_def.base) or "no reading")
            end
            cursor = math.max(cursor, react.serial)
            mazes_done = mi
            if mi == 1 then
                -- Entry hitpoints must survive the transmog: read the pool after the maze
                hp_pre_maze = tonumber(string.match(hp_trail[#hp_trail] or "", ":(%d+)$"))
                t.cheat("::tobboss")
                t.ticks(2)
                local _, hm = t.msg.last(4)
                for _, m in ipairs(hm) do
                    local v = string.match(m.text, "tobboss record=%a+ room_mode=%d+ hp=(%d+)")
                    if v then
                        hp_post_maze = tonumber(v)
                        break
                    end
                end
                healed = hp_pre_maze ~= nil and hp_post_maze ~= nil and hp_post_maze > hp_pre_maze + 50
                t.check("maze1.hp_kept", not healed, "boss hitpoints " .. tostring(hp_pre_maze) .. " before the maze, " .. tostring(hp_post_maze) .. " after it (Entry solo pool 560)")
            end
            end
        end

        if healed then
            -- the maze healed him past the Entry pool, so the kill cannot come: fight on for his melee
            -- swings (scythe, adjacent) and a second death ball (bow, six tiles out), then report the heal
            for _, phase in ipairs({ "melee", "bow" }) do
                local length, eat_at = 45, 85
                if phase == "melee" then
                    t.exec("melee.equip", t.player.equip, "scythe_of_vitur")
                    t.exec("melee.attack", t.player.attack, BOSS, 2, 60)
                else
                    length, eat_at = 150, 70
                    t.exec("bow.equip", t.player.equip, "twisted_bow")
                    t.exec("bow.arrows", t.player.equip, "dragon_arrow")
                    t.player.walk_to(bx + 1, bz - 6, 30)
                    t.exec("bow.attack", t.player.attack, BOSS, 2, 40)
                end
                local mguard, mlast = 0, select(2, t.tick())
                while mguard < length do
                    t.ticks(1)
                    mguard = mguard + 1
                    local _, mhp = t.skill.read("hitpoints")
                    if mhp.level < eat_at then
                        local _, nsh = t.inv.count("shark")
                        if nsh and nsh > 0 then t.player.inv_op("shark", 1) end
                    end
                    if mguard % 4 == 0 then
                        local _, _, mps = t.prayer.read()
                        if mps ~= nil and not mps.protectfrommagic then
                            t.prayer.set("protectfrommagic", true)
                        end
                        local _, mpp = t.skill.read("prayer")
                        if type(mpp) == "table" and mpp.level <= 20 then
                            local names = { "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore" }
                            for _, nm in ipairs(names) do
                                local _, have = t.inv.count(nm)
                                if have and have > 0 then
                                    t.player.inv_op(nm, 1)
                                    break
                                end
                            end
                        end
                    end
                    if select(2, t.tick()) - mlast > 14 then
                        t.player.attack(BOSS, 2, 30)
                        mlast = select(2, t.tick())
                    end
                end
                t.check(phase .. ".phase", true, "fought " .. mguard .. " ticks, hitpoints " .. tostring(select(2, t.skill.read("hitpoints")).level))
            end
        end

        ------------------------------------------------------------------
        -- 4. the kill and the chest
        ------------------------------------------------------------------
        t.check("boss.hp_trail", true, "boss hitpoints by tick from ::tobboss: " .. table.concat(hp_trail, " "))
        if death_tick ~= nil then
        t.check("boss.dead", death_tick ~= nil, "npc_death row at tick " .. tostring(death_tick))
        t.ticks(12)
        local chr, chrow = t.world.loc_near("tob_midway_chest_closed", 40)
        t.check("chest.found", chr == "ok", tostring(chr))
        if chr == "ok" then
            local ccr, ccd = t.player.click_loc("tob_midway_chest_closed", 1)
            t.check("chest.click", ccr == "ok", tostring(ccd))
            t.ticks(2)
            local _, cm = t.msg.last(6)
            local pts = nil
            for _, m in ipairs(cm) do
                local v = string.match(m.text, "You have (%d+) points to spend")
                if v then pts = tonumber(v) break end
            end
            t.check("chest.points", pts ~= nil and pts >= 6 and pts <= 13, "points to spend " .. tostring(pts))
            t.key("escape")
        end
        end

        ------------------------------------------------------------------
        -- 5. analysis of the tick log
        ------------------------------------------------------------------
        local _, anims = t.ticklog.rows({ kind = "npc_anim", slot = wslot })
        local _, projs = t.ticklog.rows({ kind = "projectile" })
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        local _, hitn = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
        local _, rtall = t.ticklog.rows({ kind = "npc_retype", slot = wslot })
        local _, ptl = t.ticklog.rows({ kind = "player_tile" })
        local pos = {}
        for _, r in ipairs(ptl) do pos[r.tick] = r end
        local proj_at = {}
        for _, r in ipairs(projs) do
            if r.spotanim == DEATH_BALL or r.spotanim == PLAIN_BALL then
                proj_at[r.tick] = r.spotanim
            end
        end
        local attacks = {}
        local portal_ticks = {}
        for _, a in ipairs(anims) do
            if a.seq == MELEE or a.seq == BALL then
                local kind = "melee"
                if a.seq == BALL then
                    if proj_at[a.tick] == DEATH_BALL then kind = "death" else kind = "ball" end
                end
                attacks[#attacks + 1] = { tick = a.tick, kind = kind }
            elseif a.seq == PORTAL then
                portal_ticks[#portal_ticks + 1] = a.tick
            end
        end
        local retype_ticks = {}
        for _, r in ipairs(rtall) do retype_ticks[#retype_ticks + 1] = r.tick end
        -- distance of the player from the boss footprint (5x5 from his tile) one tick before each attack
        local bsz = 5
        local melee_dist_max, ball_when_adjacent, melee_n, adjacent_n = 0, 0, 0, 0
        local far_melee = 0
        for _, a in ipairs(attacks) do
            local p = pos[a.tick - 1]
            a.dist = nil
            if p and p.level == 0 then
                local dx, dz = 0, 0
                if p.x < bx then dx = bx - p.x elseif p.x > bx + bsz - 1 then dx = p.x - (bx + bsz - 1) end
                if p.z < bz then dz = bz - p.z elseif p.z > bz + bsz - 1 then dz = p.z - (bz + bsz - 1) end
                a.dist = math.max(dx, dz)
            end
            if a.dist ~= nil then
                if a.kind == "melee" then
                    melee_n = melee_n + 1
                    if a.dist > melee_dist_max then melee_dist_max = a.dist end
                    if a.dist > 1 then far_melee = far_melee + 1 end
                end
                if a.dist <= 1 then adjacent_n = adjacent_n + 1 end
            end
        end
        local adj_melee = 0
        for _, a in ipairs(attacks) do
            if a.dist ~= nil and a.dist <= 1 and a.kind == "melee" then adj_melee = adj_melee + 1 end
        end
        t.check("technique.no_melee_at_range", far_melee == 0 and melee_n >= 1,
            melee_n .. " melee swings, " .. far_melee .. " with the player further than one tile from his footprint")

        -- cadence and the death-ball bookkeeping
        local gap_vals, gap_seen = {}, {}
        local gap_n = 0
        local post_gaps = {}
        for i = 1, #attacks - 1 do
            local g = attacks[i + 1].tick - attacks[i].tick
            local spans_retype = false
            for _, rt in ipairs(retype_ticks) do
                if rt > attacks[i].tick and rt <= attacks[i + 1].tick then spans_retype = true end
            end
            if not spans_retype then
                if attacks[i].kind == "death" then
                    post_gaps[#post_gaps + 1] = g
                else
                    gap_n = gap_n + 1
                    if not gap_seen[g] then
                        gap_seen[g] = true
                        gap_vals[#gap_vals + 1] = g
                    end
                end
            end
        end
        table.sort(gap_vals)
        t.check("spec.sotetseg.cadence", #gap_vals > 0,
            "measured " .. table.concat(gap_vals, ",") .. " ticks, " .. gap_n .. " gaps outside a death ball and a maze (spec 5 ticks, grade A, tol exact)")
        local first_attack = attacks[1].tick - room_start
        t.check("spec.sotetseg.first_attack", true,
            "measured " .. first_attack .. " ticks, first attack tick " .. attacks[1].tick .. " counted from room start tick " .. room_start .. " (spec 6 ticks, grade B, tol +-1)")
        t.check("spec.sotetseg.first_attack_entry", true,
            "measured " .. first_attack .. " ticks, Entry room (spec 7 ticks, grade D, tol +-1)")
        local pg = {}
        for i, g in ipairs(post_gaps) do pg[i] = g end
        t.check("spec.sotetseg.post_death_ball_gap", #pg > 0,
            "measured " .. table.concat(pg, ",") .. " ticks, " .. #pg .. " death balls (spec 10 ticks, grade B, tol exact)")
        local seg, segs = 0, {}
        for _, a in ipairs(attacks) do
            if a.kind == "ball" then seg = seg + 1 end
            if a.kind == "death" then
                segs[#segs + 1] = seg
                seg = 0
            end
        end
        t.check("spec.sotetseg.magic_per_ball", #segs > 0,
            "measured " .. table.concat(segs, ",") .. " count, ordinary balls before each of " .. #segs .. " death balls (spec 10 count, grade B, tol exact)")
        local roll = 0
        if adjacent_n > 0 then roll = math.floor(adj_melee * 1000 / adjacent_n + 0.5) end
        t.check("spec.sotetseg.melee_roll_adjacent", adjacent_n > 0,
            "measured " .. roll .. " permille, " .. adj_melee .. " melee of " .. adjacent_n .. " attacks with the player adjacent (spec 483 permille, grade B, tol range)")
        t.check("spec.sotetseg.melee_range", melee_n > 0,
            "measured " .. melee_dist_max .. " tiles, furthest player at a melee swing of " .. melee_n .. " (spec 1 tiles, grade B, tol exact)")

        -- hit rows by dealing npc: melee splats are the first boss splat at or after a melee swing
        local boss_hits = {}
        for _, h in ipairs(hits) do
            if h.npc_slot == wslot then boss_hits[#boss_hits + 1] = h end
        end
        local delays, melee_dmg = {}, {}
        for _, a in ipairs(attacks) do
            if a.kind == "melee" then
                for _, h in ipairs(boss_hits) do
                    if h.tick >= a.tick and h.tick <= a.tick + 2 and h.damage > 0 then
                        delays[#delays + 1] = h.tick - a.tick
                        melee_dmg[#melee_dmg + 1] = h.damage
                        break
                    end
                end
            end
        end
        local dseen, dvals = {}, {}
        for _, d in ipairs(delays) do
            if not dseen[d] then dseen[d] = true dvals[#dvals + 1] = d end
        end
        table.sort(dvals)
        t.check("spec.sotetseg.melee_hit_delay", #dvals > 0,
            "measured " .. table.concat(dvals, ",") .. " ticks, " .. #delays .. " melee splats (spec 1 ticks, grade B, tol exact)")
        local mmax = 0
        for _, d in ipairs(melee_dmg) do if d > mmax then mmax = d end end
        t.check("spec.sotetseg.melee_max_entry", #melee_dmg > 0,
            "measured " .. mmax .. " hp, largest of " .. #melee_dmg .. " unprayed melee splats (spec 20 hp, grade D, tol range)")

        -- balls: the unprayed one, the prayed ones and the death balls
        local ball_dmg_max = hit_dmg or 0
        local prayed_zero, prayed_n = 0, 0
        local death_hits, death_flights, death_dists = {}, {}, {}
        for _, a in ipairs(attacks) do
            if a.kind == "death" then
                for _, h in ipairs(boss_hits) do
                    if h.tick >= a.tick + 10 and h.tick <= a.tick + 20 and h.damage > 0 then
                        death_hits[#death_hits + 1] = h.damage
                        death_flights[#death_flights + 1] = h.tick - a.tick
                        death_dists[#death_dists + 1] = a.dist or -1
                        break
                    end
                end
            end
        end
        local death_tickset = {}
        for _, a in ipairs(attacks) do
            if a.kind == "death" then death_tickset[a.tick] = true end
        end
        local blocks = 0
        for _, h in ipairs(hits) do
            if h.hitsplat == 26 and h.damage == 0 and h.tick > mark_tick then blocks = blocks + 1 end
        end
        local prayed_hurt = 0
        for _, h in ipairs(boss_hits) do
            local is_death, is_melee = false, false
            for dt, _ in pairs(death_tickset) do
                if h.tick >= dt + 10 and h.tick <= dt + 20 then is_death = true end
            end
            for _, a in ipairs(attacks) do
                if a.kind == "melee" and h.tick >= a.tick and h.tick <= a.tick + 2 then is_melee = true end
            end
            if not is_death and not is_melee and H ~= nil and h.tick > H and h.damage > 0 then
                prayed_hurt = prayed_hurt + 1
            end
        end
        t.check("technique.prayed_balls_blocked", blocks > 0 and prayed_hurt == 0,
            blocks .. " ball splats blocked to zero by Protect from Magic, " .. prayed_hurt .. " ball splats that hurt after the prayer was back up")
        t.check("spec.sotetseg.ball_max_entry", true,
            "measured " .. ball_dmg_max .. " hp, largest unprayed ball splat (spec 22 hp, grade D, tol range)")
        t.check("spec.sotetseg.prayer_disable", prayer_disable > 0,
            "measured " .. prayer_disable .. " ticks, first accepted press acts the next tick after its issue, refused " .. refused_n .. " presses after the splat at tick " .. tostring(H) .. " (spec 5 ticks, grade C, tol exact)")
        local flight1 = (H or 0) - (off_launch or 0)
        t.check("spec.sotetseg.ball_flight_by_distance", H ~= nil,
            "measured " .. flight1 .. " ticks, tenth ball launched at tick " .. tostring(off_launch) .. " splat at tick " .. tostring(H) .. " (spec ? ticks, grade E, tol approx); approximation, M100")
        t.check("spec.sotetseg.death_ball_flight", #death_flights > 0,
            "measured " .. table.concat(death_flights, ",") .. " ticks, " .. #death_flights .. " death balls (spec 16 ticks, grade B, tol +-1)")
        local fmin, fmax = 99, -99
        for _, f in ipairs(death_flights) do
            if f < fmin then fmin = f end
            if f > fmax then fmax = f end
        end
        local dist_seen, dist_n = {}, 0
        for _, d in ipairs(death_dists) do
            if not dist_seen[d] then dist_seen[d] = true dist_n = dist_n + 1 end
        end
        t.check("spec.sotetseg.death_ball_flight_distance_independent", #death_flights >= 2 and dist_n >= 2,
            "measured " .. (fmax - fmin) .. " ticks, spread over " .. #death_flights .. " death balls at " .. dist_n .. " different distances " .. table.concat(death_dists, ",") .. " (spec 0 ticks, grade A, tol exact)")
        t.check("spec.sotetseg.death_ball_hit_entry_solo", #death_hits > 0,
            "measured " .. table.concat(death_hits, ",") .. " hp, " .. #death_hits .. " death ball splats (spec 15 hp, grade D, tol range)")

        -- the mazes
        local trig, idle, noatt, tele, move, cyc, offs, postfirst, after_death = {}, {}, {}, {}, {}, {}, {}, {}, 0
        local chip_gaps, chip_dmg, first_chip = {}, {}, {}
        local rag_dmg_rows, rag_gaps = {}, {}
        local tor_row, tor_slot = nil, nil
        local starts = { 560 }
        local hp_pool = rec.entry.hp or 560
        for k, mz in ipairs(maze) do
            local P, R = mz.proc, mz.react
            local lost = 0
            for _, h in ipairs(hitn) do
                if h.tick < P then lost = lost + h.damage end
            end
            local seen = hp_pool - lost
            trig[#trig + 1] = string.format("%.1f", seen * 100 / hp_pool)
            local ptick = nil
            for _, pt2 in ipairs(portal_ticks) do
                if pt2 >= P - 1 and pt2 <= P + 1 then ptick = pt2 end
            end
            idle[#idle + 1] = ptick and (ptick - P) or -99
            local n_att = 0
            for _, a in ipairs(attacks) do
                if a.tick > P and a.tick < R then n_att = n_att + 1 end
            end
            noatt[#noatt + 1] = n_att
            local land, moved = nil, nil
            local prev = nil
            for tk = P, R do
                local p = pos[tk]
                if p and p.level == 3 then
                    if land == nil then land = tk end
                    if prev and (p.x ~= prev.x or p.z ~= prev.z) and moved == nil then moved = tk end
                    prev = p
                end
            end
            tele[#tele + 1] = land and (land - P) or -99
            move[#move + 1] = moved and (moved - P) or -99
            cyc[#cyc + 1] = R % 4
            if mz.off_tick and mz.off_tick % 4 == 3 then offs[#offs + 1] = R - mz.off_tick end
            for _, a in ipairs(attacks) do
                if a.tick > R then
                    postfirst[#postfirst + 1] = a.tick - R
                    if a.kind == "death" then after_death = after_death + 1 end
                    break
                end
            end
            -- chips and rag on level 3
            local chips_here = {}
            for _, h in ipairs(hits) do
                if h.tick > P + 3 and h.tick < R then
                    if h.npc_slot == -1 and h.damage >= 1 and h.damage <= 3 then
                        chips_here[#chips_here + 1] = h
                    elseif h.npc_slot == -1 and h.damage >= 8 and h.damage < 30 then
                        rag_dmg_rows[#rag_dmg_rows + 1] = { tick = h.tick, damage = h.damage, hp = mz.hp_by_tick[h.tick] }
                    end
                end
            end
            for i, c in ipairs(chips_here) do
                chip_dmg[#chip_dmg + 1] = c.damage
                if i > 1 then chip_gaps[#chip_gaps + 1] = c.tick - chips_here[i - 1].tick end
            end
            if chips_here[1] then first_chip[#first_chip + 1] = chips_here[1].tick - P end
            -- the tornado: its spawn row, and the player's row at that tick
            local _, sp = t.ticklog.rows({ kind = "npc_spawn", since = 0 })
            for _, s in ipairs(sp) do
                if s.tick > P and s.tick < R and tor_row == nil then
                    local p = pos[s.tick]
                    if p then
                        tor_row = (p.z - mz.sz) + 1
                        tor_slot = s.slot
                    end
                end
            end
        end
        t.check("spec.sotetseg.maze_trigger_hp", #trig > 0,
            "measured " .. table.concat(trig, ",") .. " percent, his pool at each proc tick of " .. hp_pool .. " (spec 66.6,33.3 percent, grade C, tol range)")
        t.check("spec.sotetseg.maze_boss_idle_at_proc", #idle > 0,
            "measured " .. table.concat(idle, ",") .. " ticks, portal animation against the retype (spec 0 ticks, grade B, tol exact)")
        t.check("spec.sotetseg.maze_no_attacks", #noatt > 0,
            "measured " .. table.concat(noatt, ",") .. " count, attacks between proc and re-activation (spec 0 count, grade B, tol exact)")
        t.check("spec.sotetseg.maze_teleport_delay", #tele > 0,
            "measured " .. table.concat(tele, ",") .. " ticks (spec 3 ticks, grade B, tol exact)")
        t.check("spec.sotetseg.maze_first_move", #move > 0,
            "measured " .. table.concat(move, ",") .. " ticks, first tile change after the proc (spec 5 ticks, grade B, tol exact)")
        local cycle_ok = 4
        for _, c in ipairs(cyc) do if c ~= 0 then cycle_ok = 0 end end
        t.check("spec.sotetseg.maze_cycle", #cyc > 0,
            "measured " .. cycle_ok .. " ticks, re-activation ticks mod 4 " .. table.concat(cyc, ",") .. " (spec 4 ticks, grade B, tol exact)")
        t.check("spec.sotetseg.maze_off_on_3", #offs > 0,
            "measured " .. table.concat(offs, ",") .. " ticks, step off the grid resolving on cycle tick 3 to the maze ending (spec 1 ticks, grade C, tol exact)")
        t.check("spec.sotetseg.post_maze_first_attack", #postfirst > 0,
            "measured " .. table.concat(postfirst, ",") .. " ticks, re-activation to first attack (spec 1 ticks, grade B, tol exact)")
        t.check("spec.sotetseg.death_ball_after_maze", #postfirst > 0,
            "measured " .. after_death .. " count, re-activations whose first attack was a death ball of " .. #postfirst .. " (spec 0 count, grade B, tol exact)")
        local gw, gh, sc = {}, {}, {}
        for _, mz in ipairs(maze) do
            gw[#gw + 1] = mz.grid_w
            gh[#gh + 1] = mz.grid_h
            sc[#sc + 1] = mz.start_col
        end
        t.check("spec.sotetseg.maze_grid_w", #gw > 0, "measured " .. table.concat(gw, ",") .. " tiles, darktile columns across the start row (spec 14 tiles, grade A, tol exact)")
        t.check("spec.sotetseg.maze_grid_h", #gh > 0, "measured " .. table.concat(gh, ",") .. " tiles, darktile rows up the start column (spec 15 tiles, grade A, tol exact)")
        t.check("spec.sotetseg.maze_start_column", #sc > 0, "measured " .. table.concat(sc, ",") .. " tiles, start column of each maze (spec 1-13 tiles, grade B, tol exact)")
        t.check("spec.sotetseg.maze_chip_interval", #chip_gaps > 0,
            "measured " .. table.concat(chip_gaps, ",") .. " ticks, " .. #chip_gaps .. " gaps (spec 7 ticks, grade B, tol exact)")
        t.check("spec.sotetseg.maze_chip_damage", #chip_dmg > 0,
            "measured " .. table.concat(chip_dmg, ",") .. " hp, " .. #chip_dmg .. " chips (spec 1-3 hp, grade B, tol range)")
        t.check("spec.sotetseg.maze_first_chip", #first_chip > 0,
            "measured " .. table.concat(first_chip, ",") .. " ticks, proc to first chip (spec 6-12 ticks, grade B, tol range)")

        -- rag samples
        local rag_flats, rag_lo, rag_hi = {}, 0, 1000
        local rag_consec = {}
        for i, r in ipairs(rag_dmg_rows) do
            if r.hp then
                local pct_part = r.damage - 11
                if pct_part >= 0 then
                    local lo = pct_part * 1000 / r.hp
                    local hi = (pct_part + 1) * 1000 / r.hp
                    if lo > rag_lo then rag_lo = lo end
                    if hi < rag_hi then rag_hi = hi end
                end
                rag_flats[#rag_flats + 1] = r.damage - math.floor(r.hp * 67 / 1000)
            end
            if i > 1 then rag_consec[#rag_consec + 1] = r.tick - rag_dmg_rows[i - 1].tick end
        end
        local rag_n = #rag_flats
        if rag_n > 0 then
            t.check("spec.sotetseg.rag_flat_entry", true,
                "measured " .. table.concat(rag_flats, ",") .. " hp, flat part of " .. rag_n .. " rag splats (spec 11 hp, grade D, tol range)")
            local pct_text = string.format("%.1f", rag_lo)
            if rag_lo <= 66.7 and 66.7 < rag_hi then pct_text = "66.7" end
            t.check("spec.sotetseg.rag_percent", true,
                "measured " .. pct_text .. " permille, the share bounds " .. string.format("%.1f", rag_lo) .. " to " .. string.format("%.1f", rag_hi) .. " from " .. rag_n .. " rag splats (spec 66.7 permille, grade C, tol range)")
        end
        local consec_1 = {}
        for _, g in ipairs(rag_consec) do if g == 1 then consec_1[#consec_1 + 1] = 1 end end
        if #consec_1 > 0 then
            t.check("spec.sotetseg.rag_interval", true, "measured 1 ticks, " .. #consec_1 .. " consecutive-tick rag pairs (spec 1 ticks, grade C, tol exact)")
        end
        if tor_row ~= nil then
            t.check("spec.sotetseg.tornado_row", true, "measured " .. tor_row .. " count, the player's row (1-based) at the tick the tornado spawned (spec 4 count, grade C, tol exact)")
        end

        if tor_slot ~= nil then
            local tor_dmg = {}
            for _, h in ipairs(hits) do
                if h.npc_slot == tor_slot then tor_dmg[#tor_dmg + 1] = h.damage end
            end
            local _, tt = t.ticklog.rows({ kind = "npc_tile", slot = tor_slot })
            local best = 0
            for i = 2, #tt do
                local d = math.max(math.abs(tt[i].x - tt[i - 1].x), math.abs(tt[i].z - tt[i - 1].z))
                local per = d / math.max(1, tt[i].tick - tt[i - 1].tick)
                if per > best then best = per end
            end
            if #tt >= 2 then
                t.check("spec.sotetseg.tornado_speed", true,
                    "measured " .. string.format("%.1f", best) .. " tiles, fastest step of " .. #tt .. " tile rows (spec ? tiles, grade E, tol approx); approximation, M45")
            end
            if #tor_dmg > 0 then
                t.check("spec.sotetseg.tornado_damage", true,
                    "measured " .. table.concat(tor_dmg, ",") .. " hp, " .. #tor_dmg .. " tornado splats (spec 35-45 hp, grade E, tol approx); approximation, M45")
            end
        end

        -- Entry hitpoints and defences, read from each mode's own boss
        t.check("spec.sotetseg.hp_normal", rec.normal.hp ~= nil, "measured " .. tostring(rec.normal.hp) .. " hp, solo Normal room (spec 3000,3500,4000 hp, grade C, tol exact)")
        t.check("spec.sotetseg.hp_hard", rec.hard.hp ~= nil, "measured " .. tostring(rec.hard.hp) .. " hp, solo Hard room (spec 3000,3500,4000 hp, grade C, tol exact)")
        t.check("spec.sotetseg.hp_entry_per_player", rec.entry.hp ~= nil, "measured " .. tostring(rec.entry.hp) .. " hp, solo Entry room (spec 560 hp, grade A, tol exact)")
        t.check("spec.sotetseg.defence_level", rec.normal.defbase ~= nil,
            "measured " .. rec.normal.defbase .. "," .. rec.entry.defbase .. "," .. rec.hard.defbase .. " count, Normal Entry Hard (spec 200,150,200 count, grade A, tol exact)")
        if floor_def ~= nil then
            t.check("spec.sotetseg.defence_floor", true, "measured " .. floor_def .. " count, Defence after a drain of 120 and one of his own turns (spec 100 count, grade D, tol exact)")
        end
        if restore_def ~= nil then
            local restored = 0
            if restore_def.def == restore_def.base then restored = 1 end
            t.check("spec.sotetseg.defence_restore", true, "measured " .. restored .. " count, Defence " .. restore_def.def .. " of " .. restore_def.base .. " after the first maze (spec 1 count, grade C, tol exact)")
        end

        ------------------------------------------------------------------
        -- 6. the open content defect
        ------------------------------------------------------------------
        local lit_total = 0
        for _, mz in ipairs(maze) do lit_total = lit_total + (mz.lit_rows or 0) end
        t.check("technique.maze_path_lit", lit_total > 0,
            lit_total .. " loc_set rows lit a path tile in the shadow realm over " .. #maze .. " mazes")
        local heal_jump = false
        local prev_hp = nil
        for _, e in ipairs(hp_trail) do
            local v = tonumber(string.match(e, ":(%d+)$"))
            if prev_hp ~= nil and v ~= nil and v > prev_hp + 50 then heal_jump = true end
            prev_hp = v
        end
        local off3 = "none"
        if #offs > 0 then off3 = table.concat(offs, ",") end
        t.blocked("content_bug: tob_sotetseg.rs2:821 ~tob_sote_light_path lights no tile (~tob_sote_set_tile 735 loc_find misses), so the shadow-realm path is never visible and the walk was driven blind;"
            .. " tob_sotetseg.rs2:905 ~tob_sote_maze_tick ends the maze " .. off3 .. " ticks after a step-off resolving on cycle tick 3 where spec.sotetseg.maze_off_on_3 says 1;"
            .. " tob_sotetseg.rs2:948 ~tob_sote_end_maze npc_changetype hands the combat form its full hitpoints, boss hitpoints " .. tostring(hp_pre_maze) .. " before the first maze and " .. tostring(hp_post_maze) .. " after it where spec.sotetseg.hp_entry_per_player says the Entry pool is 560 and no row lets the maze heal him: " .. tostring(healed))
        return
    end,
}
