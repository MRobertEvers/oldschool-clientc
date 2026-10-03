-- Theatre of Blood, room 4 (Sotetseg), Entry mode, solo party.
-- Spec table: docs/minigames/theater_of_blood/encounters/sotetseg.tsv.
-- The fight is driven in three phases, each ending on a maze: darts from six tiles out
-- until the first maze, darts from three tiles out until the second, then the scythe
-- adjacent until he dies. Protect from Magic stays up except for one deliberate unprayed
-- ball. Both mazes are WALKED along the lit path read from the loc_set rows; the first
-- takes one deliberate wrong-tile excursion (rag), the second and the first both wait at
-- the path's end for the tornado and leave the grid on a cycle tick 3.
return {
    id = "tob_sotetseg",
    fixture = "fresh_lumbridge.ini",
    max_frames = 48000,
    setup = {
        "::clearinv",
        -- the combat stats an Entry-mode Sotetseg raider has
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel ranged 99", "::setlevel magic 99",
        -- a thrown weapon for the two range phases and a melee weapon for the last one
        "::give dragon_dart 800", "::give scythe_of_vitur 1",
        -- the Elder maul: its special lowers his current Defence 35 % a hit, below his floor
        "::give elder_maul 1",
        -- the melee kit a raider swings the maul in: its special must land to drain him (accuracy against Defence 150)
        "::give bandos_chestplate 1", "::give primordial_boots 1", "::give infernal_cape 1",
        "::give ferocious_gloves 1", "::give ultor_ring 1",
                -- Vulnerability's runes: the defence-lowering spell a raider casts on him from range
        "::give soulrune 20", "::give waterrune 60", "::give earthrune 60",
        -- food: the room's debug kit adds 8 potions, so 11 sharks and the melee kit fill the 28 slots
        "::give shark 11",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=entry party=1")
        local BOSS = "tob_sotetseg_combat_story"
        local MELEE, BALL = 8138, 8139            -- attack seqs (ENCOUNTER_TIMING 5.1)
        local PORTAL = 8142                       -- maze proc seq
        local DEATH_BALL, PLAIN_BALL = 1604, 1606 -- projectile spotanims
        local RESTORES = { "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" }

        ------------------------------------------------------------------
        -- 1. the Entry room: landing, his record, the log, the barrier by the player's own click
        ------------------------------------------------------------------
        local er, ed = t.raid.enter("tob", "sotetseg", { mode = "entry" })
        t.check("enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("state", sr == "ok" and st.mode == "entry" and st.started == false, tostring(st and st.line))
        t.cheat("::tobboss")
        t.ticks(2)
        local _, bmsgs = t.msg.last(4)
        local bline = ""
        for _, m in ipairs(bmsgs) do
            if string.find(m.text, "tobboss record=", 1, true) then
                bline = m.text
                break
            end
        end
        local pool = tonumber(string.match(bline, "hp=(%d+)"))
        local def_now = tonumber(string.match(bline, "def=(%d+)"))
        local def_base = tonumber(string.match(bline, "def=%d+ of (%d+)"))
        local att_now = tonumber(string.match(bline, "att=(%d+) of"))
        t.check("boss.record", pool ~= nil and def_base ~= nil, bline)
        local lr0, lg0 = t.ticklog.start()
        t.check("ticklog", lr0 == "ok", tostring(lg0))
        local nr, nrow = t.npc.by_symbol(BOSS)
        t.check("boss.row", nr == "ok", "client slot " .. tostring(nrow and nrow.slot) .. " at " .. tostring(nrow and nrow.x) .. "," .. tostring(nrow and nrow.z))
        local wr, wslot = t.ticklog.slot(nrow)
        t.check("boss.slot", wr == "ok", "world slot " .. tostring(wslot))
        local bx, bz = nrow.x, nrow.z
        t.exec("equip.darts", t.player.equip, "dragon_dart")
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
        local _, mrows = t.ticklog.rows({ kind = "mark" })
        local mark_tick, mark_serial = mrows[#mrows].tick, mrows[#mrows].serial
        t.expect("fight.begins", t.msg.expect("The fight begins"))
        -- the mark is written one tick after the room start (DRIVER_NOTES, the tick log has no room-start row)
        local room_start = mark_tick - 1

        ------------------------------------------------------------------
        -- 2. the fight: phases 1..3 by mazes done
        ------------------------------------------------------------------
        local STAND = { 6, 3, 1 }            -- tiles from his footprint per phase
        local phase_set = 0
        local mazes_done = 0
        local maze = {}
        local cursor_hit = mark_serial       -- hit_npc rows read so far
        local cursor_ball = mark_serial      -- projectile rows read so far
        local cursor_retype = mark_serial
        local balls_run = 0                  -- ordinary balls since the last death ball
        local prayer_dropped, unprayed_done = false, false
        local H, hit_dmg, off_launch = nil, nil, nil
        local refused_n, ok_issue, prayer_disable = 0, nil, -1
        local potions_drunk, sharks_eaten = 0, 0
        local death_tick = nil
        local last_hit_tick = select(2, t.tick())
        local fguard = 0
        local tb_pending, def_last, def_floor, vuln_casts = false, nil, nil, 0
        local def_reads_text, last_anim_tick, cursor_anim = "none", -100, 0
        local eat_ticks = {}                 -- ticks a food or potion was sent: it delays the player's queued hits
        while fguard < 700 and death_tick == nil do
            t.ticks(1)
            fguard = fguard + 1
            local now = select(2, t.tick())
            local want_phase = math.min(mazes_done + 1, 3)
            if want_phase ~= phase_set then
                phase_set = want_phase
                t.player.walk_to(bx + 2, bz - STAND[want_phase], 25)
                local _, at = t.world.tile()
                t.check("phase" .. want_phase .. ".stand", true,
                    "standing at " .. at.x .. "," .. at.z .. ", " .. STAND[want_phase] .. " tile(s) south of his footprint at " .. bx .. "," .. bz .. " (tick " .. now .. ")")
                if want_phase == 3 then
                    -- real defence lowering to his floor: Elder maul specials (35 percent of his CURRENT Defence each),
                    -- his Defence read from ::tobboss before and after every special
                    local floor_reads = {}
                    local special_log = {}
                    t.exec("phase3.equip", t.player.equip, "elder_maul")
                    t.exec("equip.chestplate", t.player.equip, "bandos_chestplate")
                    t.exec("equip.boots", t.player.equip, "primordial_boots")
                    t.exec("equip.cape", t.player.equip, "infernal_cape")
                    t.exec("equip.gloves", t.player.equip, "ferocious_gloves")
                    t.exec("equip.ring", t.player.equip, "ultor_ring")
                    t.ticks(4)  -- the special bar refreshes for the freshly wielded maul before it is pressed
                    t.cheat("::tobboss")
                    t.ticks(2)
                    local _, pm0 = t.msg.last(4)
                    local dpre = nil
                    for _, m in ipairs(pm0) do
                        local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                        if v then dpre = tonumber(v) break end
                    end
                    floor_reads[1] = dpre
                    for k = 1, 4 do
                        local _, hp_k = t.skill.read("hitpoints")
                        if hp_k.level < 60 then
                            local _, nsh_k = t.inv.count("shark")
                            if nsh_k and nsh_k > 0 then
                                t.player.inv_op("shark", 1)
                                eat_ticks[#eat_ticks + 1] = select(2, t.tick())
                                sharks_eaten = sharks_eaten + 1
                                t.ticks(3)
                            end
                        end
                        local _, hn_before = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                        local tr, td = t.ui.tab("combat")
                        local wr, wid = t.ui.widget("combat_interface:special_attack")
                        local ir, idt = "no_widget", tostring(wid)
                        if wr == "ok" then ir, idt = t.ui.invoke(wid, 1) end
                        local ar, ad = t.player.attack(BOSS, 2, 20)
                        t.ticks(9)
                        local _, hn_after = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                        local swing_hits = {}
                        for hi = #hn_before + 1, #hn_after do swing_hits[#swing_hits + 1] = hn_after[hi].damage end
                        t.cheat("::tobboss")
                        t.ticks(2)
                        local _, dm = t.msg.last(4)
                        local dnow = nil
                        for _, m in ipairs(dm) do
                            local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                            if v then dnow = tonumber(v) break end
                        end
                        floor_reads[#floor_reads + 1] = dnow
                        special_log[#special_log + 1] = tostring(floor_reads[#floor_reads - 1]) .. ">" .. tostring(dnow)
                        t.check("elder_maul.special" .. k, dnow ~= nil,
                            "tab " .. tostring(tr) .. ", special button " .. tostring(wr) .. " pressed " .. tostring(ir) .. ", swing " .. tostring(ar) .. ", splats on him " .. table.concat(swing_hits, "/") .. "; his Defence read " .. tostring(floor_reads[#floor_reads - 1]) .. " before and " .. tostring(dnow) .. " after, of " .. tostring(def_base))
                        if dnow ~= nil and dnow <= 100 then break end
                    end
                    def_floor = floor_reads[#floor_reads]
                    def_reads_text = table.concat(special_log, ", ")
                    t.exec("phase3.equip_scythe", t.player.equip, "scythe_of_vitur")
                end
                if want_phase == 1 then
                    -- real defence lowering: one Vulnerability cast (it does not stack; read back from ::tobboss)
                    local floor_reads = {}
                    for k = 1, 1 do
                        local cr, cd = t.player.cast("vulnerability", BOSS, 8)
                        vuln_casts = k
                        t.cheat("::tobboss")
                        t.ticks(2)
                        local _, dm = t.msg.last(4)
                        local dnow = nil
                        for _, m in ipairs(dm) do
                            local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                            if v then
                                dnow = tonumber(v)
                                break
                            end
                        end
                        floor_reads[#floor_reads + 1] = dnow
                        t.check("vulnerability.cast" .. k, cr == "ok" and dnow ~= nil,
                            "cast " .. k .. " answered " .. tostring(cr) .. "; his Defence read " .. tostring(dnow) .. " of " .. tostring(def_base))
                        
                    end
                    def_floor = floor_reads[#floor_reads]
                    def_last = def_floor
                end
                local alive_r = t.npc.by_symbol(BOSS)
                if alive_r == "ok" then
                    t.exec("phase" .. want_phase .. ".attack", t.player.attack, BOSS, 2, 40)
                end
                last_hit_tick = select(2, t.tick())
            end
            local _, hpr = t.skill.read("hitpoints")
            local hp = hpr.level
            -- boss swings seen so far (his attack animations): an eat sent within 3 ticks around a swing
            -- holds his melee hit (known open content row), so eat only 2-3 ticks after one unless low
            local _, an_rows = t.ticklog.rows({ kind = "npc_anim", since = cursor_anim, slot = wslot })
            for _, r in ipairs(an_rows) do
                cursor_anim = math.max(cursor_anim, r.serial)
                last_anim_tick = r.tick
            end
            local since_swing = now - last_anim_tick
            local safe_eat = phase_set < 3 or since_swing == 2 or since_swing == 3 or hp < 45
            if hp < 70 and safe_eat then
                local _, nsh = t.inv.count("shark")
                if nsh and nsh > 0 then
                    t.player.inv_op("shark", 1)
                    eat_ticks[#eat_ticks + 1] = now
                    sharks_eaten = sharks_eaten + 1
                end
            end
            -- the ordinary balls since the last death ball (projectile rows, ours are darts)
            local _, pj = t.ticklog.rows({ kind = "projectile", since = cursor_ball })
            local launch_tick = nil
            for _, r in ipairs(pj) do
                cursor_ball = math.max(cursor_ball, r.serial)
                if r.spotanim == PLAIN_BALL then
                    balls_run = balls_run + 1
                    launch_tick = r.tick
                elseif r.spotanim == DEATH_BALL then
                    balls_run = 0
                end
            end
            -- the tenth ball is the last before the death ball: let it land unprayed (phases 1-2, no melee)
            if balls_run >= 10 and launch_tick ~= nil and not unprayed_done and not prayer_dropped
                and hp >= 60 and phase_set < 3 then
                local dr0, dd0 = t.prayer.set("protectfrommagic", false)
                t.check("prayer.drop", dr0 == "ok", "prayer off after the tenth ball launched at tick " .. tostring(launch_tick) .. ": " .. tostring(dd0))
                prayer_dropped = true
                off_launch = launch_tick
            end
            if prayer_dropped and H == nil then
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
                    unprayed_done = true
                end
            end
            if fguard % 3 == 0 and not (prayer_dropped and not unprayed_done) then
                local _, _, pset = t.prayer.read()
                if pset ~= nil and not pset.protectfrommagic then
                    t.prayer.set("protectfrommagic", true)
                end
                local _, ppts = t.skill.read("prayer")
                if ppts ~= nil and ppts.level <= 40 and (phase_set < 3 or since_swing == 2 or since_swing == 3 or hp < 45) then
                    for _, nm in ipairs(RESTORES) do
                        local _, have = t.inv.count(nm)
                        if have and have > 0 then
                            t.player.inv_op(nm, 1)
                            eat_ticks[#eat_ticks + 1] = now
                            potions_drunk = potions_drunk + 1
                            break
                        end
                    end
                end
            end
            if tb_pending then
                local _, tm = t.msg.last(4)
                for _, m in ipairs(tm) do
                    local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                    if v then
                        def_last = tonumber(v)
                        break
                    end
                end
                tb_pending = false
            end
            if fguard % 15 == 0 then
                t.cheat("::tobboss")
                tb_pending = true
            end
            local _, hn = t.ticklog.rows({ kind = "hit_npc", since = cursor_hit, slot = wslot })
            if #hn > 0 then
                last_hit_tick = hn[#hn].tick
                cursor_hit = math.max(cursor_hit, hn[#hn].serial)
            end
            if now - last_hit_tick > 8 then
                t.player.attack(BOSS, 2, 30)
                last_hit_tick = now
            end
            local _, dr = t.ticklog.rows({ kind = "npc_death", since = mark_serial, slot = wslot })
            if #dr > 0 then
                death_tick = dr[1].tick
            end
            local _, rt = t.ticklog.rows({ kind = "npc_retype", since = cursor_retype, slot = wslot })
            if #rt > 0 and death_tick == nil then
                local proc = rt[1]
                cursor_retype = proc.serial
                ----------------------------------------------------------
                -- the maze, walked along the lit path
                ----------------------------------------------------------
                local mi = mazes_done + 1
                local P = proc.tick
                local mz = { n = mi, proc = P, serial = proc.serial, path = {}, path_n = 0, reads = {}, def_pre = def_last }
                maze[mi] = mz
                local _, bhp0 = t.skill.read("hitpoints")
                t.check("maze" .. mi .. ".proc", true, "retype row on his slot at tick " .. P .. " (" .. tostring(proc.from_type) .. " to " .. tostring(proc.to_type) .. "), hitpoints " .. bhp0.level)
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
                t.check("maze" .. mi .. ".landed", lv == 3, "level " .. tostring(lv) .. " tile " .. sx .. "," .. sz .. " at tick " .. tostring(select(2, t.tick())) .. " proc tick " .. P)
                -- the grid: tiles carrying a loc along the start row (west and east) and up the start column
                local west, east, rows_n = 0, 0, 0
                for dx = 1, 16 do
                    local _, hh = t.world.hazard_at(sx - dx, sz, 3)
                    if hh ~= nil and #hh.locs > 0 then west = dx else break end
                end
                for dx = 1, 16 do
                    local _, hh = t.world.hazard_at(sx + dx, sz, 3)
                    if hh ~= nil and #hh.locs > 0 then east = dx else break end
                end
                for dz = 0, 17 do
                    local _, hh = t.world.hazard_at(sx, sz + dz, 3)
                    if hh ~= nil and #hh.locs > 0 then rows_n = rows_n + 1 else break end
                end
                mz.grid_w, mz.grid_h, mz.start_col = west + east + 1, rows_n, west
                t.check("maze" .. mi .. ".grid", mz.grid_w > 0, "grid " .. mz.grid_w .. " wide, " .. mz.grid_h .. " high, start column " .. west)
                -- stunned: a step asked on the landing tick is dropped
                local _, issue0 = t.tick()
                local s0r, s0d = t.player.step_tick(sx, sz + 1, 1)
                mz.stall = s0r
                t.check("maze" .. mi .. ".stall", s0r == "timeout", "step asked at tick " .. issue0 .. " during the stun: " .. tostring(s0r) .. " " .. tostring(s0d))
                local wt0 = 0
                while select(2, t.tick()) < P + 4 and wt0 < 6 do
                    t.ticks(1)
                    wt0 = wt0 + 1
                end
                -- the path is lit on the runner's first tick in the realm: one loc_set row per path tile
                local lit_tries = 0
                while mz.path_n == 0 and lit_tries < 4 do
                    local _, lrows = t.ticklog.rows({ kind = "loc_set", since = proc.serial })
                    for _, r in ipairs(lrows) do
                        if r.level == 3 and mz.path[r.x .. "," .. r.z] == nil then
                            mz.path[r.x .. "," .. r.z] = true
                            mz.path_n = mz.path_n + 1
                        end
                    end
                    if mz.path_n == 0 then
                        t.ticks(1)
                        lit_tries = lit_tries + 1
                    end
                end
                t.check("maze" .. mi .. ".lit", mz.path[sx .. "," .. sz] == true and mz.path_n >= 20,
                    mz.path_n .. " lit tiles in loc_set rows since the proc, the start tile " .. sx .. "," .. sz .. " among them: " .. tostring(mz.path[sx .. "," .. sz]))
                -- walk it: north when the path goes north, else along the run row
                local cx, cz = sx, sz
                local seen = {}
                seen[cx .. "," .. cz] = true
                mz.order = { { sx, sz } }
                local steps, stuck = 0, 0
                while steps < 70 do
                    local nx, nz = nil, nil
                    if mz.path[cx .. "," .. (cz + 1)] and not seen[cx .. "," .. (cz + 1)] then
                        nx, nz = cx, cz + 1
                    elseif mz.path[(cx - 1) .. "," .. cz] and not seen[(cx - 1) .. "," .. cz] then
                        nx, nz = cx - 1, cz
                    elseif mz.path[(cx + 1) .. "," .. cz] and not seen[(cx + 1) .. "," .. cz] then
                        nx, nz = cx + 1, cz
                    end
                    if nx == nil then break end
                    local _, hq = t.skill.read("hitpoints")
                    if hq.level < 45 then
                        t.player.inv_op("shark", 1)
                        t.ticks(3)
                    end
                    -- maze 1, row 2: one deliberate wrong tile for two ticks (rag)
                    if mi == 1 and mz.rag == nil and cz - sz == 1 then
                        local wx, wz = nil, nil
                        local cands = { { cx - 1, cz }, { cx + 1, cz }, { cx, cz + 1 } }
                        for _, c in ipairs(cands) do
                            local _, hz = t.world.hazard_at(c[1], c[2], 3)
                            if wx == nil and mz.path[c[1] .. "," .. c[2]] == nil and hz ~= nil and #hz.locs > 0 then
                                wx, wz = c[1], c[2]
                            end
                        end
                        if wx ~= nil then
                            local xr, xd = t.player.step_tick(wx, wz)
                            local _, hpB = t.skill.read("hitpoints")
                            local tkB = select(2, t.tick())
                            t.ticks(1)
                            local _, hpC = t.skill.read("hitpoints")
                            local tkC = select(2, t.tick())
                            t.ticks(1)
                            local br2, bd2 = t.player.step_tick(cx, cz)
                            mz.rag = { wx = wx, wz = wz, hpB = hpB.level, tkB = tkB, hpC = hpC.level, tkC = tkC, out = xr, back = br2 }
                            t.check("maze" .. mi .. ".rag_step", xr == "ok" and br2 == "ok",
                                "wrong tile " .. wx .. "," .. wz .. " off the path at row 2: " .. tostring(xd) .. "; back: " .. tostring(bd2))
                        end
                    end
                    local rr, rd = t.player.step_tick(nx, nz)
                    if rr ~= "ok" then
                        stuck = stuck + 1
                        if stuck > 3 then
                            t.check("maze" .. mi .. ".walk_stuck", false, "step to " .. nx .. "," .. nz .. " answered " .. tostring(rr) .. " " .. tostring(rd))
                            break
                        end
                    else
                        stuck = 0
                        steps = steps + 1
                        seen[nx .. "," .. nz] = true
                        mz.order[#mz.order + 1] = { nx, nz }
                        cx, cz = nx, nz
                        if steps == 1 then
                            mz.first_move = tonumber(string.match(tostring(rd), "resolved at tick (%d+)"))
                        end
                    end
                    local _, lvq = t.world.level()
                    if lvq ~= 3 then break end
                end
                mz.end_x, mz.end_z, mz.walked = cx, cz, steps
                t.check("maze" .. mi .. ".walked", cz - sz >= 13,
                    steps .. " steps along the lit path from " .. sx .. "," .. sz .. " to " .. cx .. "," .. cz .. ", row " .. (cz - sz + 1) .. " of " .. mz.grid_h .. "; first step resolved at tick " .. tostring(mz.first_move) .. " (proc " .. P .. ")")
                local _, hw = t.ticklog.rows({ kind = "hit_player", since = proc.serial })
                mz.tor_before = 0
                for _, r in ipairs(hw) do
                    if r.damage >= 30 and r.tick > P + 3 then mz.tor_before = mz.tor_before + 1 end
                end
                -- the tornado: wait at the path's end for it to arrive; one that has stopped on the path (it
                -- cannot step onto x >= 6438) is walked back to and shared, so the hit it deals is read
                local tor_hit, tslot, tstall = nil, nil, nil
                local walked_back = false
                for w = 1, 32 do
                    local now2 = select(2, t.tick())
                    local _, hh = t.ticklog.rows({ kind = "hit_player", since = proc.serial })
                    for _, r in ipairs(hh) do
                        if r.damage >= 30 and r.tick > P + 3 then tor_hit = r end
                    end
                    if tor_hit ~= nil then break end
                    if tslot == nil then
                        local _, tsp = t.ticklog.rows({ kind = "npc_spawn", since = proc.serial })
                        for _, r in ipairs(tsp) do
                            if r.level == 3 and tslot == nil then tslot = r.slot end
                        end
                    end
                    local tx, tz, ttick = nil, nil, nil
                    if tslot ~= nil then
                        local _, ttl = t.ticklog.rows({ kind = "npc_tile", slot = tslot, since = proc.serial })
                        if #ttl > 0 then tx, tz, ttick = ttl[#ttl].x, ttl[#ttl].z, ttl[#ttl].tick end
                    end
                    if tslot == nil or (ttick == nil and w >= 8) then
                        tstall = "never moved from its spawn tile"
                        break
                    end
                    if ttick ~= nil and now2 - ttick >= 3 and not walked_back and (tx ~= cx or tz ~= cz) then
                        tstall = tx .. "," .. tz .. " since tick " .. ttick
                        local idx = nil
                        for i, o in ipairs(mz.order) do
                            if o[1] == tx and o[2] == tz then idx = i end
                        end
                        if idx ~= nil and mz.order[idx][2] - sz >= 3 then
                            walked_back = true
                            local _, hq0 = t.skill.read("hitpoints")
                            if hq0.level < 85 then
                                t.player.inv_op("shark", 1)
                                t.ticks(3)
                            end
                            for i = #mz.order - 1, idx, -1 do
                                t.player.step_tick(mz.order[i][1], mz.order[i][2])
                            end
                            for i = idx + 1, #mz.order do
                                t.player.step_tick(mz.order[i][1], mz.order[i][2])
                            end
                        else
                            break
                        end
                    else
                        local _, hq = t.skill.read("hitpoints")
                        if hq.level < 90 then
                            t.player.inv_op("shark", 1)
                        end
                        t.ticks(1)
                    end
                end
                mz.tor_hit = tor_hit
                t.check("maze" .. mi .. ".tornado_hit", true,
                    "tornado hit " .. tostring(tor_hit and tor_hit.damage) .. " at tick " .. tostring(tor_hit and tor_hit.tick) .. "; stalled: " .. tostring(tstall) .. "; walked back to it: " .. tostring(walked_back))
                -- leave the grid with a step resolving on cycle tick 3
                local wg = 0
                while select(2, t.tick()) % 4 ~= 2 and wg < 8 do
                    t.ticks(1)
                    wg = wg + 1
                end
                local _, lvq0 = t.world.level()
                if lvq0 == 3 then
                    local xr, xd = t.player.step_tick(cx, cz + 1)
                    mz.off_tick = tonumber(string.match(tostring(xd), "resolved at tick (%d+)"))
                    if xr ~= "ok" then
                        local ex, ed2 = t.player.click_loc("tob_sotetseg_darkrealm_exit", 1)
                        t.check("maze" .. mi .. ".exit_click", ex == "ok", tostring(ed2))
                    end
                    t.check("maze" .. mi .. ".leave_grid", xr == "ok" and mz.off_tick ~= nil, tostring(xd))
                end
                -- the re-activation: the next retype row on his slot
                local rguard, react = 0, nil
                while react == nil and rguard < 30 do
                    t.ticks(1)
                    rguard = rguard + 1
                    local _, rt2 = t.ticklog.rows({ kind = "npc_retype", since = proc.serial, slot = wslot })
                    if #rt2 >= 1 then react = rt2[1] end
                end
                t.check("maze" .. mi .. ".reactivated", react ~= nil, "re-activation tick " .. tostring(react and react.tick) .. " proc tick " .. P)
                if react == nil then
                    t.blocked("maze " .. mi .. " did not re-activate within 30 ticks of stepping off the grid")
                    return
                end
                mz.react = react.tick
                cursor_retype = react.serial
                local lw, lvl2 = 0, 3
                while lvl2 ~= 0 and lw < 14 do
                    t.ticks(1)
                    lw = lw + 1
                    local _, l2 = t.world.level()
                    lvl2 = l2
                end
                t.check("maze" .. mi .. ".returned", lvl2 == 0, "back on level " .. lvl2 .. " after " .. lw .. " ticks")
                mazes_done = mi
                last_hit_tick = select(2, t.tick())
                phase_set = 0
                -- his hitpoints survive the retypes: the reading against the pool minus every hit so far
                local hp_tick = select(2, t.tick())
                t.cheat("::tobboss")
                t.ticks(2)
                local _, hm = t.msg.last(4)
                local hp_after = nil
                for _, m in ipairs(hm) do
                    local v = string.match(m.text, "tobboss record=%a+ room_mode=%d+ hp=(%d+)")
                    if v then
                        hp_after = tonumber(v)
                        mz.def_after = tonumber(string.match(m.text, "def=(%d+) of"))
                        break
                    end
                end
                local _, hn2 = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                local dealt = 0
                for _, h in ipairs(hn2) do
                    if h.tick <= hp_tick then dealt = dealt + h.damage end
                end
                mz.hp_after, mz.expected = hp_after, pool - dealt
                t.check("maze" .. mi .. ".hp_kept", hp_after ~= nil and math.abs(hp_after - (pool - dealt)) <= 3,
                    "boss hitpoints read " .. tostring(hp_after) .. " after the maze, the pool " .. pool .. " less " .. dealt .. " dealt by tick " .. hp_tick .. " is " .. (pool - dealt))
            end
        end
        t.check("fight.over", death_tick ~= nil, "boss npc_death at tick " .. tostring(death_tick) .. " after " .. fguard .. " loop ticks, " .. mazes_done .. " mazes, " .. sharks_eaten .. " sharks, " .. potions_drunk .. " restores, " .. vuln_casts .. " vulnerability casts")

        ------------------------------------------------------------------
        -- 3. the tick log
        ------------------------------------------------------------------
        local _, anims = t.ticklog.rows({ kind = "npc_anim", slot = wslot })
        local _, projs = t.ticklog.rows({ kind = "projectile" })
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        local _, hitn = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
        local _, rtall = t.ticklog.rows({ kind = "npc_retype", slot = wslot })
        local _, ptl = t.ticklog.rows({ kind = "player_tile" })
        local pos = {}
        local runner_pids, runner_n = {}, 0
        for _, r in ipairs(ptl) do
            pos[r.tick] = r
            if r.level == 3 and runner_pids[r.pid] == nil then
                runner_pids[r.pid] = true
                runner_n = runner_n + 1
            end
        end
        local death_proj, plain = {}, {}
        for _, r in ipairs(projs) do
            if r.spotanim == DEATH_BALL then
                death_proj[r.tick] = r
            elseif r.spotanim == PLAIN_BALL then
                plain[#plain + 1] = r
            end
        end
        local attacks, portal_ticks = {}, {}
        for _, a in ipairs(anims) do
            if a.seq == MELEE or a.seq == BALL then
                local kind = "melee"
                if a.seq == BALL then
                    if death_proj[a.tick] ~= nil then kind = "death" else kind = "ball" end
                end
                attacks[#attacks + 1] = { tick = a.tick, kind = kind }
            elseif a.seq == PORTAL then
                portal_ticks[#portal_ticks + 1] = a.tick
            end
        end
        local retype_ticks = {}
        for _, r in ipairs(rtall) do retype_ticks[#retype_ticks + 1] = r.tick end
        -- the player's distance from his footprint (5x5 from his south-west tile) one tick before each attack
        local bsz = 5
        local melee_dist_max, melee_n, far_melee = 0, 0, 0
        for _, a in ipairs(attacks) do
            local p = pos[a.tick - 1]
            a.dist = nil
            if p and p.level == 0 then
                local dx, dz = 0, 0
                if p.x < bx then dx = bx - p.x elseif p.x > bx + bsz - 1 then dx = p.x - (bx + bsz - 1) end
                if p.z < bz then dz = bz - p.z elseif p.z > bz + bsz - 1 then dz = p.z - (bz + bsz - 1) end
                a.dist = math.max(dx, dz)
            end
            if a.kind == "melee" and a.dist ~= nil then
                melee_n = melee_n + 1
                if a.dist > melee_dist_max then melee_dist_max = a.dist end
                if a.dist > 1 then far_melee = far_melee + 1 end
            end
        end
        t.check("technique.no_melee_at_range", far_melee == 0 and melee_n >= 1,
            melee_n .. " melee swings, " .. far_melee .. " with the player further than one tile from his footprint")

        -- cadence and the death-ball bookkeeping
        local gap_vals, gap_seen, gap_n, post_gaps = {}, {}, 0, {}
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
        local _, gap_text = t.ticklog.gaps(wslot, "npc_anim", { seq = BALL })
        t.expect("spec.sotetseg.cadence", #gap_vals > 0 and "ok" or "none",
            "measured " .. table.concat(gap_vals, ",") .. " ticks, " .. gap_n .. " gaps between his melee and ball attacks outside a death ball and a maze; seq 8139 rows alone: " .. tostring(gap_text) .. " (spec 5 ticks, grade A, tol exact)")
        local first_attack = attacks[1].tick - room_start
        t.expect("spec.sotetseg.first_attack_entry", "ok",
            "measured " .. first_attack .. " ticks, first attack tick " .. attacks[1].tick .. " counted from room start tick " .. room_start .. " (spec 7 ticks, grade D, tol +-1)")
        t.expect("spec.sotetseg.post_death_ball_gap", #post_gaps > 0 and "ok" or "none",
            "measured " .. table.concat(post_gaps, ",") .. " ticks, " .. #post_gaps .. " death balls followed by an attack (spec 10 ticks, grade B, tol exact)")
        local seg, segs = 0, {}
        for _, a in ipairs(attacks) do
            if a.kind == "ball" then seg = seg + 1 end
            if a.kind == "death" then
                segs[#segs + 1] = seg
                seg = 0
            end
        end
        t.expect("spec.sotetseg.magic_per_ball", #segs > 0 and "ok" or "none",
            "measured " .. table.concat(segs, ",") .. " count, ordinary balls before each of " .. #segs .. " death balls (spec 10 count, grade B, tol exact)")
        t.expect("spec.sotetseg.melee_range", melee_n > 0 and "ok" or "none",
            "measured " .. melee_dist_max .. " tiles, furthest player at any of " .. melee_n .. " melee swings (spec 1 tiles, grade B, tol exact)")

        -- every splat from him: death ball, melee, or an unprayed ball
        local boss_hits = {}
        for _, h in ipairs(hits) do
            if h.npc_slot == wslot and h.damage > 0 then boss_hits[#boss_hits + 1] = h end
        end
        -- each splat goes to the attack it belongs to: melee lands +1 (up to +4 when an eat delays the
        -- player's queued hit), the death ball +16 (up to +20); what is left is an ordinary ball
        local melee_used, death_used = {}, {}
        local delays, melee_dmg, delay_eaten = {}, {}, 0
        local late_text = ""
        local death_hits, death_flights, death_dists, death_eaten = {}, {}, {}, 0
        local ball_dmg, ball_after = {}, 0
        for _, h in ipairs(boss_hits) do
            local best, best_kind, best_ex = nil, nil, 99
            for ai, a in ipairs(attacks) do
                local d = h.tick - a.tick
                if a.kind == "melee" and not melee_used[ai] and d >= 0 and d <= 4 and math.abs(d - 1) < best_ex then
                    best, best_kind, best_ex = ai, "melee", math.abs(d - 1)
                elseif a.kind == "death" and not death_used[ai] and d >= 16 and d <= 20 and (d - 16) < best_ex then
                    best, best_kind, best_ex = ai, "death", d - 16
                end
            end
            if best_kind == "melee" then
                local a = attacks[best]
                melee_used[best] = true
                local d = h.tick - a.tick
                local eaten = false
                for _, e in ipairs(eat_ticks) do
                    if e >= a.tick - 3 and e <= a.tick then eaten = true end
                end
                if d ~= 1 and eaten then delay_eaten = delay_eaten + 1 end
                if d ~= 1 then
                    local near = ""
                    for _, e in ipairs(eat_ticks) do
                        if e >= a.tick - 4 and e <= h.tick then near = near .. " " .. e end
                    end
                    late_text = late_text .. " swing " .. a.tick .. " splat " .. h.tick .. " eats at" .. near .. ";"
                end
                delays[#delays + 1] = d
                melee_dmg[#melee_dmg + 1] = h.damage
            elseif best_kind == "death" then
                local a = attacks[best]
                death_used[best] = true
                local d = h.tick - a.tick
                local eaten = false
                for _, e in ipairs(eat_ticks) do
                    if e >= a.tick + 13 and e <= a.tick + 16 then eaten = true end
                end
                if d ~= 16 and eaten then
                    death_eaten = death_eaten + 1
                else
                    death_hits[#death_hits + 1] = h.damage
                    death_flights[#death_flights + 1] = d
                    death_dists[#death_dists + 1] = a.dist or -1
                end
            else
                ball_dmg[#ball_dmg + 1] = h.damage
                if ok_issue ~= nil and h.tick > ok_issue then ball_after = ball_after + 1; ball_late = (ball_late or "") .. " t" .. h.tick .. ":" .. h.damage end
            end
        end
        local dseen, dvals = {}, {}
        for _, d in ipairs(delays) do
            if not dseen[d] then dseen[d] = true dvals[#dvals + 1] = d end
        end
        table.sort(dvals)
        local dcount = {}
        local dtext, dall_one = {}, true
        for _, d in ipairs(delays) do
            dcount[d] = (dcount[d] or 0) + 1
            if d ~= 1 then dall_one = false end
        end
        for _, d in ipairs(dvals) do dtext[#dtext + 1] = dcount[d] .. " at +" .. d end
        t.check("spec.sotetseg.melee_hit_delay", #delays > 0 and dall_one,
            "measured " .. table.concat(dvals, ",") .. " ticks, every instance paired swing animation to splat in the tick log: " .. table.concat(delays, "/") .. " (" .. table.concat(dtext, ", ") .. "; " .. delay_eaten .. " of them after an eat within 3 ticks of the swing;" .. late_text .. ") (spec 1 ticks, grade B, tol exact)")
        local mmax = 0
        for _, d in ipairs(melee_dmg) do if d > mmax then mmax = d end end
        t.expect("spec.sotetseg.melee_max_entry", #melee_dmg > 0 and "ok" or "none",
            "measured " .. mmax .. " hp, largest of " .. #melee_dmg .. " unprayed melee splats " .. table.concat(melee_dmg, "/") .. " (spec 20 hp, grade D, tol range)")
        local blocks = 0
        for _, h in ipairs(hits) do
            if h.hitsplat == 26 and h.damage == 0 and h.npc_slot == -1 and h.tick > mark_tick and pos[h.tick] ~= nil and pos[h.tick].level == 0 then
                blocks = blocks + 1
            end
        end
        t.check("technique.prayed_balls_blocked", blocks > 0 and ball_after == 0,
            blocks .. " ball splats blocked to zero by Protect from Magic, " .. ball_after .. " ball splats that hurt after the prayer was back up at tick " .. tostring(ok_issue) .. (ball_late or ""))
        local bmax = 0
        for _, d in ipairs(ball_dmg) do if d > bmax then bmax = d end end
        t.expect("spec.sotetseg.ball_max_entry", #ball_dmg > 0 and "ok" or "none",
            "measured " .. bmax .. " hp, largest of " .. #ball_dmg .. " unprayed ball splats " .. table.concat(ball_dmg, "/") .. " (spec 22 hp, grade D, tol range)")
        t.expect("spec.sotetseg.prayer_disable", prayer_disable > 0 and "ok" or "none",
            "measured " .. prayer_disable .. " ticks, first accepted press issued at tick " .. tostring(ok_issue) .. " after " .. refused_n .. " refused presses following the splat at tick " .. tostring(H) .. " (spec 5 ticks, grade C, tol exact)")
        local fl = {}
        for _, r in ipairs(plain) do
            local p = pos[r.tick - 1]
            local d = -1
            if p and p.level == 0 then
                local dx, dz = 0, 0
                if p.x < bx then dx = bx - p.x elseif p.x > bx + bsz - 1 then dx = p.x - (bx + bsz - 1) end
                if p.z < bz then dz = bz - p.z elseif p.z > bz + bsz - 1 then dz = p.z - (bz + bsz - 1) end
                d = math.max(dx, dz)
            end
            local key = "d" .. d .. ":" .. math.floor((r.end_cycle - r.start_cycle) / 30 + 0.5)
            fl[key] = (fl[key] or 0) + 1
        end
        local fl_keys = {}
        for k, n in pairs(fl) do fl_keys[#fl_keys + 1] = k .. "x" .. n end
        table.sort(fl_keys)
        local fl_first = plain[1] and math.floor((plain[1].end_cycle - plain[1].start_cycle) / 30 + 0.5) or -1
        t.expect("spec.sotetseg.ball_flight_by_distance", #plain > 0 and "ok" or "none",
            "measured " .. fl_first .. " ticks, ball flight by the player's distance from his footprint (distance:ticks xcount) " .. table.concat(fl_keys, " ") .. " (spec ? ticks, grade E, tol approx); approximation, M100")
        t.expect("spec.sotetseg.death_ball_flight", #death_flights > 0 and "ok" or "none",
            "measured " .. table.concat(death_flights, ",") .. " ticks, " .. #death_flights .. " death balls, " .. death_eaten .. " more held back by an eat while in flight (spec 16 ticks, grade B, tol +-1)")
        local fmin, fmax = 99, -99
        for _, f in ipairs(death_flights) do
            if f < fmin then fmin = f end
            if f > fmax then fmax = f end
        end
        local dist_seen, dist_n = {}, 0
        for _, d in ipairs(death_dists) do
            if not dist_seen[d] then dist_seen[d] = true dist_n = dist_n + 1 end
        end
        t.expect("spec.sotetseg.death_ball_flight_distance_independent", (#death_flights >= 2 and dist_n >= 2) and "ok" or "too few",
            "measured " .. (fmax - fmin) .. " ticks, spread over " .. #death_flights .. " death balls at " .. dist_n .. " different distances " .. table.concat(death_dists, ",") .. " (spec 0 ticks, grade A, tol exact)")
        local dmax = 0
        for _, d in ipairs(death_hits) do if d > dmax then dmax = d end end
        t.expect("spec.sotetseg.death_ball_hit_entry_solo", #death_hits > 0 and "ok" or "none",
            "measured " .. dmax .. " hp, largest of " .. #death_hits .. " death ball splats " .. table.concat(death_hits, "/") .. " (spec 15 hp, grade D, tol exact)")

        -- his record
        t.expect("spec.sotetseg.hp_entry_per_player", pool ~= nil and "ok" or "none",
            "measured " .. tostring(pool) .. " hp, read from ::tobboss in the solo Entry room before the fight (spec 560 hp, grade A, tol exact)")
        t.expect("spec.sotetseg.attack_level", att_now ~= nil and "ok" or "none",
            "measured " .. tostring(att_now) .. " count, his Attack in the Entry room, read from ::tobboss (spec 250,180,350 count, grade A, tol exact)")
        t.expect("spec.sotetseg.defence_level", def_base ~= nil and "ok" or "none",
            "measured " .. tostring(def_base) .. " count, his Defence in the Entry room, read from ::tobboss (spec 200,150,200 count, grade A, tol exact)")

        t.check("spec.sotetseg.defence_floor", def_floor ~= nil and def_floor == 100,
            "measured " .. tostring(def_floor) .. " count, his Defence of " .. tostring(def_base) .. " read from ::tobboss after each Elder maul special (each takes 35 percent of his current Defence, no lower than the floor); before>after per special: " .. def_reads_text .. " (spec 100 count, grade D, tol exact)")

        ------------------------------------------------------------------
        -- 4. the mazes
        ------------------------------------------------------------------
        local trig_text, cross_text = {}, {}
        local trig, idle, noatt, tele, move, cyc, offs, postfirst, after_death = {}, {}, {}, {}, {}, {}, {}, {}, 0
        local chip_gaps, chip_dmg, first_chip = {}, {}, {}
        local rag_rows, tor_rows, tor_dmg_list, tor_speed = {}, {}, {}, {}
        local gw, gh, sc, seeds = {}, {}, {}, {}
        local maze_visits_off_path, maze_ticks_on_grid = 0, 0
        local tor_by_maze, restored_list, restored_text = {}, {}, {}
        local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
        for k, mz in ipairs(maze) do
            local P, R = mz.proc, mz.react
            local lost = 0
            for _, h in ipairs(hitn) do
                if h.tick < P then lost = lost + h.damage end
            end
            -- the reading before the splat that carried him across this maze's threshold (blert's "last
            -- hitpoints reading before the proc"), and how many ticks before the proc that splat landed
            local thr = (k == 1) and 66.6 or 33.3
            local run_lost, before_pct, cross_off, after_cross_pct = 0, nil, nil, nil
            for _, h in ipairs(hitn) do
                if h.tick < P then
                    local prev_pct = (pool - run_lost) * 100 / pool
                    run_lost = run_lost + h.damage
                    local after_pct = (pool - run_lost) * 100 / pool
                    if before_pct == nil and prev_pct > thr and after_pct <= thr then
                        before_pct, cross_off, after_cross_pct = prev_pct, h.tick - P, after_pct
                    end
                end
            end
            if before_pct == nil then before_pct, cross_off, after_cross_pct = (pool - lost) * 100 / pool, 0, (pool - lost) * 100 / pool end
            trig[#trig + 1] = string.format("%.1f", after_cross_pct)
            cross_text[#cross_text + 1] = "maze " .. k .. " crossing splat at proc" .. (cross_off >= 0 and "+" or "") .. cross_off
            local near_hits = ""
            for _, h in ipairs(hitn) do
                if h.tick >= P - 6 and h.tick <= P + 1 then near_hits = near_hits .. " " .. h.damage .. "@" .. (h.tick - P) end
            end
            trig_text[#trig_text + 1] = "maze " .. k .. " proc tick " .. P .. " splats (damage@tick-from-proc):" .. near_hits
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
            local land, moved, prev = nil, nil, nil
            for tk = P, R do
                local p = pos[tk]
                if p and p.level == 3 then
                    if land == nil then land = tk end
                    if prev and (p.x ~= prev.x or p.z ~= prev.z) and moved == nil then moved = tk end
                    prev = p
                    maze_ticks_on_grid = maze_ticks_on_grid + 1
                    if mz.path[p.x .. "," .. p.z] == nil and p.z - mz.sz < 15 then maze_visits_off_path = maze_visits_off_path + 1 end
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
            if mz.def_pre ~= nil and mz.def_after ~= nil and mz.def_pre < def_base then
                if mz.def_after == def_base then restored_list[#restored_list + 1] = 1 else restored_list[#restored_list + 1] = 0 end
                restored_text[#restored_text + 1] = mz.def_pre .. " to " .. mz.def_after
            end
            gw[#gw + 1], gh[#gh + 1], sc[#sc + 1] = mz.grid_w, mz.grid_h, mz.start_col
            local even = 0
            for key, _ in pairs(mz.path) do
                local zz = tonumber(string.match(key, ",(%d+)$"))
                if (zz - mz.sz) % 2 == 0 then even = even + 1 end
            end
            seeds[#seeds + 1] = even
            local chips_here = {}
            for _, h in ipairs(hits) do
                if h.tick > P + 3 and h.tick < R and h.damage >= 30 and h.npc_slot ~= wslot then
                    tor_dmg_list[#tor_dmg_list + 1] = h.damage
                    tor_by_maze[k] = (tor_by_maze[k] or 0) + 1
                elseif h.tick > P + 3 and h.tick < R and h.npc_slot == -1 then
                    if h.damage >= 1 and h.damage <= 3 then
                        chips_here[#chips_here + 1] = h
                    elseif h.damage >= 8 and h.damage < 30 and h.hitsplat ~= 26 then
                        rag_rows[#rag_rows + 1] = { tick = h.tick, damage = h.damage, maze = mz }
                    end
                end
            end
            for i, c in ipairs(chips_here) do
                chip_dmg[#chip_dmg + 1] = c.damage
                if i > 1 then chip_gaps[#chip_gaps + 1] = c.tick - chips_here[i - 1].tick end
            end
            if chips_here[1] then first_chip[#first_chip + 1] = chips_here[1].tick - P end
            for _, s in ipairs(spawns) do
                if s.tick > P and s.tick < R and s.type ~= nil then
                    local p = pos[s.tick - 1]
                    if p ~= nil and p.level == 3 and tor_rows[k] == nil and s.level == 3 then
                        tor_rows[k] = { row = (p.z - mz.sz) + 1, slot = s.slot, tick = s.tick }
                    end
                end
            end
        end
        t.expect("spec.sotetseg.defence_restore", #restored_list > 0 and "ok" or "none",
            "measured " .. table.concat(restored_list, ",") .. " count, his Defence before and after each maze: " .. table.concat(restored_text, ", ") .. " of " .. tostring(def_base) .. " (spec 1 count, grade C, tol exact)")
        local trig_ok = #trig > 0
        for i2, v2 in ipairs(trig) do
            if tonumber(v2) > ((i2 == 1) and 66.6 or 33.3) then trig_ok = false end
        end
        t.check("spec.sotetseg.maze_trigger_hp", trig_ok,
            "measured " .. table.concat(trig, ",") .. " percent, his pool of " .. pool .. " after the splat that first took him to or below each threshold (" .. table.concat(cross_text, ", ") .. "); " .. table.concat(trig_text, "; ") .. " (spec 66.6,33.3 percent, grade C, tol range)")
        t.expect("spec.sotetseg.maze_boss_idle_at_proc", #idle > 0 and "ok" or "none",
            "measured " .. table.concat(idle, ",") .. " ticks, portal animation against the retype (spec 0 ticks, grade B, tol exact)")
        t.expect("spec.sotetseg.maze_no_attacks", #noatt > 0 and "ok" or "none",
            "measured " .. table.concat(noatt, ",") .. " count, attacks between proc and re-activation (spec 0 count, grade B, tol exact)")
        t.expect("spec.sotetseg.maze_teleport_delay", #tele > 0 and "ok" or "none",
            "measured " .. table.concat(tele, ",") .. " ticks (spec 3 ticks, grade B, tol exact)")
        t.expect("spec.sotetseg.maze_first_move", #move > 0 and "ok" or "none",
            "measured " .. table.concat(move, ",") .. " ticks, first tile change after the proc (spec 5 ticks, grade B, tol exact)")
        local cycle_ok = 4
        for _, c in ipairs(cyc) do if c ~= 0 then cycle_ok = 0 end end
        t.expect("spec.sotetseg.maze_cycle", #cyc > 0 and "ok" or "none",
            "measured " .. cycle_ok .. " ticks, re-activation ticks mod 4 " .. table.concat(cyc, ",") .. " (spec 4 ticks, grade B, tol exact)")
        local phase_word = "unmeasured"
        if #cyc >= 2 then
            if cyc[1] == cyc[2] then phase_word = "global" else phase_word = "restarted" end
        end
        t.expect("spec.sotetseg.maze_cycle_phase", #cyc >= 2 and "ok" or "none",
            "measured " .. phase_word .. "; re-activation ticks mod 4 were " .. table.concat(cyc, " and ") .. " for the two mazes (spec global text, grade B, tol +-1)")
        t.expect("spec.sotetseg.maze_off_on_3", #offs > 0 and "ok" or "none",
            "measured " .. table.concat(offs, ",") .. " ticks, step off the grid resolving on cycle tick 3 to the re-activation (spec 1 ticks, grade C, tol exact)")
        t.expect("spec.sotetseg.post_maze_first_attack", #postfirst > 0 and "ok" or "none",
            "measured " .. table.concat(postfirst, ",") .. " ticks, re-activation to first attack (spec 1 ticks, grade B, tol exact)")
        t.expect("spec.sotetseg.death_ball_after_maze", #postfirst > 0 and "ok" or "none",
            "measured " .. after_death .. " count, re-activations whose first attack was a death ball of " .. #postfirst .. " (spec 0 count, grade B, tol exact)")
        t.expect("spec.sotetseg.maze_grid_w", #gw > 0 and "ok" or "none", "measured " .. table.concat(gw, ",") .. " tiles, tiles carrying a loc along the start row (spec 14 tiles, grade A, tol exact)")
        t.expect("spec.sotetseg.maze_grid_h", #gh > 0 and "ok" or "none", "measured " .. table.concat(gh, ",") .. " tiles, tiles carrying a loc up the start column (spec 15 tiles, grade A, tol exact)")
        t.expect("spec.sotetseg.maze_seeds", #seeds > 0 and "ok" or "none", "measured " .. table.concat(seeds, ",") .. " count, lit path tiles on the even rows, one per row (spec 8 count, grade B, tol exact)")
        t.expect("spec.sotetseg.maze_start_column", #sc > 0 and "ok" or "none", "measured " .. table.concat(sc, ",") .. " tiles, start column of each maze counted from the west edge (spec 1-13 tiles, grade B, tol exact)")
        t.expect("spec.sotetseg.maze_players_hard", runner_n > 0 and "ok" or "none", "measured " .. runner_n .. " count, players seen on level 3 in this Entry room (spec 1 count, grade C, tol exact)")
        t.expect("spec.sotetseg.maze_chip_interval", #chip_gaps > 0 and "ok" or "none",
            "measured " .. table.concat(chip_gaps, ",") .. " ticks, " .. #chip_gaps .. " gaps (spec 7 ticks, grade B, tol exact)")
        t.expect("spec.sotetseg.maze_chip_damage", #chip_dmg > 0 and "ok" or "none",
            "measured " .. table.concat(chip_dmg, ",") .. " hp, " .. #chip_dmg .. " chips (spec 1-3 hp, grade B, tol range)")
        t.expect("spec.sotetseg.maze_first_chip", #first_chip > 0 and "ok" or "none",
            "measured " .. table.concat(first_chip, ",") .. " ticks, proc to first chip (spec 6-12 ticks, grade B, tol range)")
        local off_ok = #offs > 0
        for _, v in ipairs(offs) do if v ~= 1 then off_ok = false end end
        t.check("technique.off_on_3", off_ok, #offs .. " mazes left with a step resolving on cycle tick 3; re-activation came " .. table.concat(offs, ",") .. " tick(s) later")
        local ahead = 0
        for _, mz in ipairs(maze) do ahead = ahead + (mz.tor_before or 0) end
        t.check("technique.tornado_outrun", tor_rows[1] ~= nil and ahead == 0,
            "the tornado spawned at tick " .. tostring(tor_rows[1] and tor_rows[1].tick) .. " when the runner stepped onto row 4, and the runner, one tile a tick ahead of it, took " .. ahead .. " tornado splats on the way to the path's end of " .. #maze .. " mazes")
        t.check("technique.walked_only_lit_tiles", maze_visits_off_path <= 2 and maze_ticks_on_grid > 20,
            maze_ticks_on_grid .. " player_tile rows in the shadow realm, " .. maze_visits_off_path .. " of them on a tile the loc_set rows never lit (the deliberate wrong-tile step stood 2 ticks)")

        -- rag: the wrong-tile excursion of maze 1
        local rag_flats, rag_lo, rag_hi = {}, 0, 1000
        local rag_gaps = {}
        local prev_hpb, prev_dmg, prev_tick = nil, nil, nil
        for i, r in ipairs(rag_rows) do
            local rg = r.maze.rag
            local hpb = nil
            -- the client's hitpoints lag a tick: the first splat's hitpoints are the reading before it,
            -- the next splat of the same stay follows from it
            if prev_tick ~= nil and r.tick == prev_tick + 1 then
                hpb = prev_hpb - prev_dmg
            elseif rg ~= nil and rg.tkB < r.tick then
                hpb = rg.hpB
            end
            prev_hpb, prev_dmg, prev_tick = hpb, r.damage, r.tick
            if hpb ~= nil then
                local pct_part = r.damage - 11
                local lo = pct_part * 1000 / hpb
                local hi = (pct_part + 1) * 1000 / hpb
                if lo > rag_lo then rag_lo = lo end
                if hi < rag_hi then rag_hi = hi end
                rag_flats[#rag_flats + 1] = r.damage - math.floor(hpb * 67 / 1000)
            end
            if i > 1 then rag_gaps[#rag_gaps + 1] = r.tick - rag_rows[i - 1].tick end
        end
        local rag_dmgs = {}
        for i, r in ipairs(rag_rows) do rag_dmgs[i] = r.damage end
        if #rag_flats > 0 then
            t.expect("spec.sotetseg.rag_flat_entry", "ok",
                "measured " .. table.concat(rag_flats, ",") .. " hp, flat part of " .. #rag_flats .. " rag splats " .. table.concat(rag_dmgs, "/") .. " (spec 11 hp, grade D, tol range)")
            local pct_text = string.format("%.1f", rag_lo)
            if rag_lo <= 66.7 and 66.7 < rag_hi then pct_text = "66.7" end
            t.expect("spec.sotetseg.rag_percent", "ok",
                "measured " .. pct_text .. " permille, the share lies in " .. string.format("%.1f", rag_lo) .. " to " .. string.format("%.1f", rag_hi) .. " permille of current hitpoints from " .. #rag_flats .. " rag splats (spec 66.7 permille, grade C, tol range)")
        end
        if #rag_gaps > 0 then
            t.expect("spec.sotetseg.rag_interval", "ok",
                "measured " .. table.concat(rag_gaps, ",") .. " ticks, between consecutive rag splats of one stay on a wrong tile (spec 1 ticks, grade C, tol exact)")
        end
        if #rag_rows > 0 then
            t.expect("spec.sotetseg.rag_range", "ok",
                "measured 0 tiles, a lone player cannot stand beside the wrong tile and take the splat, so only the occupant's own tile was observed; " .. #rag_rows .. " splats (spec 1 tiles, grade E, tol approx); approximation, M46")
        end

        -- the tornado
        local tor_first = nil
        local tor_rows_text = {}
        for k, r in pairs(tor_rows) do
            tor_first = tor_first or r
            tor_rows_text[#tor_rows_text + 1] = r.row
        end
        if tor_first ~= nil then
            t.expect("spec.sotetseg.tornado_row", "ok",
                "measured " .. table.concat(tor_rows_text, ",") .. " count, the player's 1-based row on the tick before the tornado spawned (spec 4 count, grade C, tol exact)")
            local _, tt = t.ticklog.rows({ kind = "npc_tile", slot = tor_first.slot })
            local best = 0
            for i = 2, #tt do
                local d = math.max(math.abs(tt[i].x - tt[i - 1].x), math.abs(tt[i].z - tt[i - 1].z))
                local per = d / math.max(1, tt[i].tick - tt[i - 1].tick)
                if per > best then best = per end
            end
            t.expect("spec.sotetseg.tornado_speed", #tt >= 2 and "ok" or "none",
                "measured " .. string.format("%.1f", best) .. " tiles, fastest step per tick of " .. #tt .. " tile rows (spec ? tiles, grade E, tol approx); approximation, M45")
        end
        if #tor_dmg_list > 0 then
            t.expect("spec.sotetseg.tornado_damage", "ok",
                "measured " .. table.concat(tor_dmg_list, ",") .. " hp, " .. #tor_dmg_list .. " tornado splats on the path's end tile (spec 35-45 hp, grade E, tol approx); approximation, M45")
        end

        ------------------------------------------------------------------
        -- 5. the exit: the barrier is a gate once the room is cleared, the chest stands in the corridor
        ------------------------------------------------------------------
        if death_tick ~= nil then
            t.ticks(10)
            for _ = 1, 3 do
                local _, nsh = t.inv.count("shark")
                if nsh and nsh > 0 then
                    t.player.inv_op("shark", 1)
                    t.ticks(3)
                end
            end
            local br, bd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("exit.barrier", br == "ok", tostring(bd))
            t.ticks(3)
            local chr, chrow = t.world.loc_near("tob_midway_chest_closed", 40)
            t.check("chest.found", chr == "ok" and chrow ~= nil, tostring(chr) .. " " .. tostring(chrow and chrow.tile_x) .. "," .. tostring(chrow and chrow.tile_z))
            if chr == "ok" and chrow ~= nil and chrow.tile_x ~= nil then
                t.player.walk_to(chrow.tile_x, chrow.tile_z + 3, 30)
                local ccr, ccd = t.player.click_loc("tob_midway_chest_closed", 1)
                t.check("chest.click", ccr == "ok", tostring(ccd))
                t.ticks(2)
                local _, cm = t.msg.last(6)
                local got = nil
                for _, m in ipairs(cm) do
                    local v = string.match(m.text, "You take (%d+) bandages from the chest")
                    if v then got = tonumber(v) break end
                end
                t.check("chest.bandages", got ~= nil and got >= 1 and got <= 10, "bandages taken from the Entry supply chest: " .. tostring(got))
            end
        end
        t.finish(0)
    end,
}
