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
        -- a thrown weapon for the two range phases and a fully charged scythe (the base item carries no charges) for the last one
        "::give dragon_dart 800", "::fullscythe",
        -- the 5-tick bow (needs Agility 99 to wield) that puts a hit one tick before his ball: the maze-proc-tick row
        "::setlevel agility 99", "::give bow_of_faerdhinen 1",
        -- the Elder maul: its special lowers his current Defence 35 % a hit, below his floor
        "::give elder_maul 1",
        -- the melee kit a raider swings the maul in: its special must land to drain him (accuracy against Defence 150)
        "::give bandos_chestplate 1", "::give primordial_boots 1", "::give infernal_cape 1",
        "::give ferocious_gloves 1", "::give ultor_ring 1",
        -- food: the room's debug kit adds 8 potions, so 14 sharks and the melee kit fill the 28 slots (no spell runes: the Elder maul does the defence lowering)
        "::give shark 13",
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
        local pool, def_base, att_now, wslot, bx, bz, idle_anim, mark_tick, mark_serial, room_start
        do
            -- the log is on before the landing: the room-entry rows (the arena floor, the room's music) are part of what the room shows
            local lr_pre, lg_pre = t.ticklog.start()
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
            pool = tonumber(string.match(bline, "hp=(%d+)"))
            local def_now = tonumber(string.match(bline, "def=(%d+)"))
            def_base = tonumber(string.match(bline, "def=%d+ of (%d+)"))
            att_now = tonumber(string.match(bline, "att=(%d+) of"))
            t.check("boss.record", pool ~= nil and def_base ~= nil, bline)
            local lr0, lg0 = t.ticklog.start()
            t.check("ticklog", lr0 == "ok" and lr_pre == "ok", tostring(lg0))
            local nr, nrow = t.npc.by_symbol(BOSS)
            t.check("boss.row", nr == "ok", "client slot " .. tostring(nrow and nrow.slot) .. " at " .. tostring(nrow and nrow.x) .. "," .. tostring(nrow and nrow.z))
            local wr
            wr, wslot = t.ticklog.slot(nrow)
            t.check("boss.slot", wr == "ok", "world slot " .. tostring(wslot))
            bx, bz = nrow.x, nrow.z
            local _, bst0 = t.npc.state(BOSS)
            idle_anim = bst0 and bst0.anim_id
            boss_ready_anim, boss_walk_anim = bst0 and bst0.ready_anim, bst0 and bst0.walk_anim
            boss_pose_kind, boss_pose_anim = bst0 and bst0.pose_kind, bst0 and bst0.pose_anim
            dball_proj_seq, tor_ready_anim, tor_walk_anim, tor_pose_kind = nil, nil, nil, nil
            local floor_r0, floor_d0 = t.world.loc_near("tob_sotetseg_plaintile", 40)
            t.check("arena.floor_at_entry", floor_r0 == "ok", "the arena floor before the barrier: " .. tostring(floor_r0) .. " loc " .. tostring(floor_d0 and floor_d0.id))
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
            mark_tick, mark_serial = mrows[#mrows].tick, mrows[#mrows].serial
            t.expect("fight.begins", t.msg.expect("The fight begins"))
            -- the mark is written one tick after the room start (DRIVER_NOTES, the tick log has no room-start row)
            room_start = mark_tick - 1
        end

        local xb_launches = {}
        local melee_splat_ticks = {}
                local proc_ball_tick, mazes_done, maze, H, hit_dmg, off_launch, refused_n, ok_issue, prayer_disable, potions_drunk, sharks_eaten, death_tick, death_msg_raw, fguard, tb_pending, def_last, def_floor, vuln_casts, def_reads_text, last_anim_tick, cursor_anim, eat_ticks
        do
            ------------------------------------------------------------------
            -- 2. the fight: phases 1..3 by mazes done
            ------------------------------------------------------------------
            local STAND = { 6, 3, 1 }            -- tiles from his footprint per phase
            local phase_set = 0
            mazes_done = 0
            maze = {}
            local cursor_hit = mark_serial       -- hit_npc rows read so far
            local cursor_ball = mark_serial      -- projectile rows read so far
            local cursor_retype = mark_serial
            local balls_run = 0                  -- ordinary balls since the last death ball
            local prayer_dropped, unprayed_done = false, false
            -- extra unprayed ordinary balls (phases 1-2) for the max-hit distribution, and the in-loop technique shots
            local XB = { n = 0, off = false, launch = -100, shot_far = false, shot_block = false, cur = mark_serial }
            H, hit_dmg, off_launch = nil, nil, nil
            refused_n, ok_issue, prayer_disable = 0, nil, -1
            potions_drunk, sharks_eaten = 0, 0
            death_tick = nil
            death_msg_raw = nil
            local last_hit_tick = select(2, t.tick())
            fguard = 0
            tb_pending, def_last, def_floor, vuln_casts = false, nil, nil, 0
            def_reads_text, last_anim_tick, cursor_anim = "none", -100, 0
            eat_ticks = {}                 -- ticks a food or potion was sent: it delays the player's queued hits
            -- sotetseg.ball_prayer_read_tick (Blert, seam20): the ordinary ball reads the protection prayer at the LANDING.
            -- (the other half, prayed at the throw and dropped in flight, is the tenth ball of the main loop below.)
            -- Trial 1: Protect from Magic OFF when the ball leaves, switched ON in flight -> blocked on the landing tick.
            local rp_lines, rp_verdicts, rp_lock = {}, {}, "n/a"
            for trial_n = 1, 1 do
                local before, after = (trial_n == 2), (trial_n == 1)
                local tries, got = 0, nil
                while tries < 4 and got == nil do
                    tries = tries + 1
                    local r0 = t.prayer.set("protectfrommagic", before)
                    for w = 1, 8 do
                        if r0 == "ok" then break end
                        t.ticks(1)
                        r0 = t.prayer.set("protectfrommagic", before)
                    end
                    t.ticklog.mark("ball read trial " .. trial_n .. " before=" .. tostring(before))
                    local _, rpm = t.ticklog.rows({ kind = "mark" })
                    local rp_m0 = rpm[#rpm]
                    local rp_throw = nil
                    for w = 1, 30 do
                        local _, rpj = t.ticklog.rows({ kind = "projectile", since = rp_m0.serial })
                        for _, r in ipairs(rpj) do
                            if r.spotanim == PLAIN_BALL and rp_throw == nil then rp_throw = r end
                        end
                        if rp_throw then break end
                        t.ticks(1)
                    end
                    if rp_throw == nil then break end
                    -- the ball thrown five ticks earlier lands two ticks after this throw and is read then: let it land first
                    while trial_n == 2 and select(2, t.tick()) < rp_throw.tick + 3 do t.ticks(1) end
                    local r1 = t.prayer.set("protectfrommagic", after)
                    t.ticklog.mark("ball read switched " .. tostring(after))
                    local _, rpm1 = t.ticklog.rows({ kind = "mark" })
                    local rp_m1 = rpm1[#rpm1]
                    local land = rp_throw.tick + math.floor(rp_throw.end_cycle / 30)
                    local verdict, hit = "none", nil
                    for w = 1, 15 do
                        local _, rph = t.ticklog.rows({ kind = "hit_player", since = rp_throw.serial })
                        for _, r in ipairs(rph) do
                            if hit == nil and r.tick == land and r.hitsplat == 26 and r.damage == 0 then verdict, hit = "prayed", r end
                            if hit == nil and r.tick == land + 1 and r.npc_slot == wslot and r.damage > 0 then verdict, hit = "unprayed", r end
                        end
                        if hit then break end
                        t.ticks(1)
                    end
                    if hit and r1 == "ok" and rp_throw.tick <= rp_m1.tick and rp_m1.tick < land then
                        got = verdict
                        rp_lines[#rp_lines + 1] = "throw " .. rp_throw.tick .. " (prayer " .. tostring(before) .. ", switched " .. tostring(after) .. " at tick " .. rp_m1.tick .. "), landing " .. land .. " -> " .. verdict .. " (splat tick " .. hit.tick .. ", damage " .. hit.damage .. ")"
                    end
                end
                rp_verdicts[trial_n] = got or "none"
            end
            t.ball_read_on = rp_verdicts[1] == "prayed"
            t.ball_read_text = table.concat(rp_lines, " | ")
            t.check("technique.ball_prayer_raised_in_flight", rp_verdicts[1] == "prayed",
                "Protect from Magic off when the ball left, on in flight: " .. rp_verdicts[1] .. "; " .. t.ball_read_text)
            -- THE PROC-TICK BALL (sotetseg.magic_per_ball_proc_tick), driven: a 5-tick bow (bow_of_faerdhinen) fired without a
            -- break lands every hit at one phase of his 5-tick cycle; that phase is put on "one tick before his attack", so the
            -- hit that crosses the first maze threshold lands on B-1 and the maze procs on B, a tick he throws a ball on. Inside
            -- the last 70 hitpoints no hit is let land before a tick he does not throw an ordinary ball on (a death ball and the
            -- empty slot after it are held past). Phase 1 only; the darts and the maul take the rest of the room.
            do
                t.exec("equip.bow", t.player.equip, "bow_of_faerdhinen")
                local PX, PZ, pstep = bx + 2, bz - 5, 1
                t.player.walk_to(PX, PZ, 25)
                local p_sent = select(2, t.tick())
                t.player.attack(BOSS, 2, 2)
                local p_d, p_l, p_hits_seen, p_swings_seen, p_cross, p_guard, p_notes = nil, nil, 0, 0, false, 0, {}
                while not p_cross and p_guard < 400 do
                    t.ticks(1)
                    p_guard = p_guard + 1
                    local _, p_hs = t.ticklog.rows({ kind = "hit_npc", since = mark_serial, slot = wslot })
                    local p_hp = pool
                    for _, r in ipairs(p_hs) do p_hp = p_hp - r.damage end
                    if p_hp <= 373 then p_cross = true break end
                    local _, p_pa = t.ticklog.rows({ kind = "player_anim", since = mark_serial, where = function(r) return r.seq ~= nil and r.seq >= 0 end })
                    -- his attacks so far and the schedule that follows from the last one
                    local pj = {}
                    local _, p_pj = t.ticklog.rows({ kind = "projectile", since = mark_serial })
                    for _, r in ipairs(p_pj) do pj[r.tick] = pj[r.tick] or {}; pj[r.tick][r.spotanim] = true end
                    local _, p_an = t.ticklog.rows({ kind = "npc_anim", since = mark_serial, slot = wslot })
                    local a_tick, a_kind, a_balls = nil, nil, 0
                    for _, r in ipairs(p_an) do
                        local kind = nil
                        if r.seq == MELEE then kind = "melee"
                        elseif r.seq == BALL and pj[r.tick] and pj[r.tick][DEATH_BALL] then kind = "death"
                        elseif r.seq == BALL then kind = "ball" end
                        if kind then
                            a_tick, a_kind = r.tick, kind
                            if kind == "death" then a_balls = 0 elseif kind == "ball" then a_balls = a_balls + 1 end
                        end
                    end
                    local fut = {}
                    if a_tick then
                        local tk, nb = a_tick + (a_kind == "death" and 10 or 5), a_balls
                        while tk <= a_tick + 90 do
                            local kind = (nb >= 10) and "death" or "ball"
                            fut[tk] = kind
                            if kind == "death" then nb = 0; tk = tk + 10 else nb = nb + 1; tk = tk + 5 end
                        end
                    end
                    local acted = false
                    if #p_hs > p_hits_seen then
                        p_hits_seen = #p_hs
                        local Ht0 = p_hs[#p_hs].tick
                        local swing = nil
                        for _, r in ipairs(p_pa) do if r.tick <= Ht0 then swing = r.tick end end
                        if p_d == nil and swing then
                            p_d, p_l = Ht0 - swing, swing - p_sent
                            p_notes[#p_notes + 1] = "swing-to-splat " .. p_d .. ", press-to-swing " .. p_l
                        end
                        if a_tick and p_d and (Ht0 - a_tick) % 5 ~= 4 then
                            local Ht = (swing or (Ht0 - p_d)) + 5 + p_d
                            while (Ht - a_tick) % 5 ~= 4 do Ht = Ht + 1 end
                            p_notes[#p_notes + 1] = "hit " .. Ht0 .. " off phase, re-aimed at " .. Ht
                            t.player.step_tick(PX + pstep, PZ)
                            PX = PX + pstep
                            pstep = -pstep
                            while select(2, t.tick()) < Ht - p_d - p_l do t.ticks(1) end
                            p_sent = select(2, t.tick())
                            t.player.attack(BOSS, 2, 2)
                            local _, p_pa2 = t.ticklog.rows({ kind = "player_anim", since = mark_serial, where = function(r) return r.seq ~= nil and r.seq >= 0 end })
                            p_swings_seen = #p_pa2
                            acted = true
                        end
                    end
                    if not acted and #p_pa > p_swings_seen and p_d and a_tick then
                        p_swings_seen = #p_pa
                        local S = p_pa[#p_pa].tick
                        local Bn = S + 5 + p_d + 1
                        if p_hp - 70 <= 373 and fut[Bn] ~= "ball" then
                            local k = 1
                            while fut[Bn + 5 * k] ~= "ball" and k < 4 do k = k + 1 end
                            local at_press = S + 5 * (k + 1) - p_l
                            p_notes[#p_notes + 1] = "swing " .. S .. ": next hit on " .. (Bn - 1) .. " would meet " .. tostring(fut[Bn] or "no attack") .. ", held " .. k .. " cycle(s)"
                            t.player.step_tick(PX + pstep, PZ)
                            PX = PX + pstep
                            pstep = -pstep
                            while select(2, t.tick()) < at_press do t.ticks(1) end
                            p_sent = select(2, t.tick())
                            t.player.attack(BOSS, 2, 2)
                            local _, p_pa3 = t.ticklog.rows({ kind = "player_anim", since = mark_serial, where = function(r) return r.seq ~= nil and r.seq >= 0 end })
                            p_swings_seen = #p_pa3
                        end
                    end
                end
                t.check("proc_ball.aligned", p_cross, "the bow's splats crossed the first maze threshold (373) at tick " .. tostring(select(2, t.tick())) .. " after " .. p_guard .. " ticks; " .. table.concat(p_notes, "; "))
                proc_ball_tick = select(2, t.tick()) + 1  -- the maze procs the tick after the crossing splat; a ball he throws on it is not counted toward the death ball
                phase_set = 1
            end
            while fguard < 700 and death_tick == nil do
                t.ticks(1)
                fguard = fguard + 1
                local now = select(2, t.tick())
                local want_phase = math.min(mazes_done + 1, 3)
                if want_phase ~= phase_set then
                    phase_set = want_phase
                    t.player.walk_to(bx + 2, bz - STAND[want_phase], 25)
                    local _, at = t.world.tile()
                    for retry = 1, 4 do
                        if math.abs(at.x - (bx + 2)) <= 1 and math.abs(at.z - (bz - STAND[want_phase])) <= 1 then break end
                        t.ticks(2)
                        t.player.walk_to(bx + 2, bz - STAND[want_phase], 25)
                        _, at = t.world.tile()
                    end
                    t.check("phase" .. want_phase .. ".stand", true,
                        "standing at " .. at.x .. "," .. at.z .. ", " .. STAND[want_phase] .. " tile(s) south of his footprint at " .. bx .. "," .. bz .. " (tick " .. now .. ")")
                    if want_phase == 3 then
                        -- real defence lowering to his floor: Elder maul specials (35 percent of his CURRENT Defence each),
                        -- his Defence read from ::tobboss before and after every special
                        local floor_reads = {}
                        local special_log = {}
                        -- the maze's tornado left the raider low: eat back up before standing next to his melee
                        for e3 = 1, 3 do
                            local _, hp3 = t.skill.read("hitpoints")
                            local _, nsh3 = t.inv.count("shark")
                            if hp3.level >= 75 or not nsh3 or nsh3 <= 0 then break end
                            t.player.inv_op("shark", 1)
                            eat_ticks[#eat_ticks + 1] = select(2, t.tick())
                            sharks_eaten = sharks_eaten + 1
                            t.ticks(3)
                        end
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
                        for k = 1, 12 do
                            local _, hp_k = t.skill.read("hitpoints")
                            if hp_k.level < 75 then
                                local _, nsh_k = t.inv.count("shark")
                                if nsh_k and nsh_k > 0 then
                                    t.player.inv_op("shark", 1)
                                    eat_ticks[#eat_ticks + 1] = select(2, t.tick())
                                    sharks_eaten = sharks_eaten + 1
                                    t.ticks(3)
                                end
                            end
                            local _, hn_before = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                            -- armed from the minimap orb (the combat tab's button does not arm the special)
                            local tr, td = "orb", "minimap orb"
                            local _, e_before = t.var.varp("varp300_sa_energy")
                            local wr, wid = t.ui.widget("orbs:specbutton")
                            local ir, idt = "no_widget", tostring(wid)
                            -- the bar regenerates 100 an interval: below 500 the swing is a plain maul swing, so only an armed press spends it
                            if wr == "ok" and type(e_before) == "number" and e_before >= 500 then ir, idt = t.ui.invoke(wid, 1) end
                            t.ticks(1)
                            local ar, ad = t.player.attack(BOSS, 2, 20)
                            local spent_at = "none"
                            for w = 1, 9 do
                                local _, en = t.var.varp("varp300_sa_energy")
                                if spent_at == "none" and type(en) == "number" and type(e_before) == "number" and en <= e_before - 500 then
                                    spent_at = "tick " .. tostring(select(2, t.tick()))
                                end
                                t.ticks(1)
                            end
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
                                "tab " .. tostring(tr) .. ", energy " .. tostring(e_before) .. " spent " .. spent_at .. ", special button " .. tostring(wr) .. " pressed " .. tostring(ir) .. ", swing " .. tostring(ar) .. ", splats on him " .. table.concat(swing_hits, "/") .. "; his Defence read " .. tostring(floor_reads[#floor_reads - 1]) .. " before and " .. tostring(dnow) .. " after, of " .. tostring(def_base))
                            if dnow ~= nil and dnow <= 100 then break end
                        end
                        def_floor = floor_reads[#floor_reads]
                        def_reads_text = table.concat(special_log, ", ")
                        t.exec("phase3.equip_scythe", t.player.equip, "scythe_of_vitur")
                    end
                    if want_phase == 2 then
                        -- one Elder maul special so his Defence is lowered when the second maze begins: defence_restore
                        -- needs a lowered Defence read before and after a maze
                        t.exec("phase2.equip_maul", t.player.equip, "elder_maul")
                        t.ticks(4)  -- the special bar refreshes for the freshly wielded maul
                        local sp_text = {}
                        for k = 1, 4 do
                            local _, hp_m = t.skill.read("hitpoints")
                            if hp_m.level < 60 and select(2, t.inv.count("shark")) > 0 then
                                t.player.inv_op("shark", 1)
                                eat_ticks[#eat_ticks + 1] = select(2, t.tick())
                                sharks_eaten = sharks_eaten + 1
                                t.ticks(3)
                            end
                            -- armed from the minimap orb; the combat tab's button does not arm it
                            local wr2, wid2 = t.ui.widget("orbs:specbutton")
                            local ir2 = "no_widget"
                            local _, e2_before = t.var.varp("varp300_sa_energy")
                            -- the maul special costs 500 of the bar: below that a press is a plain swing
                            if wr2 == "ok" and type(e2_before) == "number" and e2_before >= 500 then ir2 = t.ui.invoke(wid2, 1) end
                            t.ticks(1)
                            local _, hn_b = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                            local ar2 = t.player.attack(BOSS, 2, 20)
                            t.ticks(7)
                            local _, hn_a = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                            local sw2 = {}
                            for hi = #hn_b + 1, #hn_a do sw2[#sw2 + 1] = hn_a[hi].damage end
                            t.cheat("::tobboss")
                            t.ticks(2)
                            local _, pm2 = t.msg.last(4)
                            def_last = nil
                            for _, m in ipairs(pm2) do
                                local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                                if v then def_last = tonumber(v) break end
                            end
                            sp_text[#sp_text + 1] = "try " .. k .. ": energy " .. tostring(e2_before) .. ", orb " .. tostring(wr2) .. " pressed " .. tostring(ir2) .. ", swing " .. tostring(ar2) .. ", splats " .. table.concat(sw2, "/") .. ", Defence " .. tostring(def_last)
                            if def_last ~= nil and def_last < def_base then break end
                        end
                        t.check("phase2.maul_special", def_last ~= nil, table.concat(sp_text, "; ") .. " of " .. tostring(def_base) .. " (tick " .. select(2, t.tick()) .. ")")
                        t.exec("phase2.equip_darts", t.player.equip, "dragon_dart")
                        t.player.walk_to(bx + 2, bz - STAND[2], 25)
                        for retry = 1, 4 do
                            local _, at2 = t.world.tile()
                            if math.abs(at2.x - (bx + 2)) <= 1 and math.abs(at2.z - (bz - STAND[2])) <= 1 then break end
                            t.ticks(2)
                            t.player.walk_to(bx + 2, bz - STAND[2], 25)
                        end
                        t.check("phase2.def_after_special", def_last ~= nil, "his Defence read after the maul special: " .. tostring(def_last) .. " of " .. tostring(def_base))
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
                        if r.tick ~= proc_ball_tick then balls_run = balls_run + 1 end
                        launch_tick = r.tick
                    elseif r.spotanim == DEATH_BALL then
                        balls_run = 0
                        dball_watch = true
                        local _, dmm = t.msg.last(8)
                        for _, m in ipairs(dmm or {}) do
                            if string.find(m.text, "large ball of energy", 1, true) then death_msg_raw = m.text end
                        end
                    end
                end
                if launch_tick ~= nil and phase_set == 1 and not XB.shot_far then
                    XB.shot_far = true
                    t.shot("technique_ball_thrown_no_melee_at_range")
                end
                -- more unprayed samples for the max-hit distribution: the tenth ball of a later cycle (a ten tick gap
                -- to the next splat, so the five tick prayer lock cannot chain into the next ball) lands unprayed
                if XB.off then
                    local _, xh = t.ticklog.rows({ kind = "hit_player", since = mark_serial })
                    local xsplat = nil
                    for _, r in ipairs(xh) do
                        if r.npc_slot == wslot and r.damage > 0 and r.tick >= XB.launch then xsplat = r.tick break end
                    end
                    if xsplat ~= nil then
                        for k = 1, 12 do
                            if t.prayer.set("protectfrommagic", true) == "ok" then break end
                            t.ticks(1)
                        end
                        XB.off = false
                    end
                end
                if launch_tick ~= nil and not XB.off and XB.n < 2 and balls_run >= 10 and unprayed_done and launch_tick > XB.launch
                    and hp >= 70 and phase_set < 3 then
                    local xr = t.prayer.set("protectfrommagic", false)
                    if xr == "ok" then
                        XB.off, XB.launch, XB.n = true, launch_tick, XB.n + 1
                        xb_launches[#xb_launches + 1] = launch_tick
                    end
                end
                if not XB.shot_block then
                    local _, bhp = t.ticklog.rows({ kind = "hit_player", since = XB.cur })
                    for _, r in ipairs(bhp) do
                        XB.cur = math.max(XB.cur, r.serial)
                        if r.hitsplat == 26 and r.damage == 0 and r.npc_slot == -1 and not XB.shot_block then
                            XB.shot_block = true
                            t.shot("technique_prayed_ball_blocked_to_zero")
                        end
                    end
                end
                -- the tenth ball is the last before the death ball: let it land unprayed (phases 1-2, no melee)
                if dball_watch and dball_proj_seq == nil then
                    local _, pjr = t.world.projectiles(40)
                    for _, pr in ipairs(pjr or {}) do
                        if pr.spotanim_id == DEATH_BALL and pr.seq ~= nil and pr.seq >= 0 then dball_proj_seq = pr.seq end
                    end
                end
                if balls_run >= 10 and launch_tick ~= nil and not unprayed_done and not prayer_dropped
                    and hp >= 60 and phase_set < 3 then
                    local dr0, dd0 = t.prayer.set("protectfrommagic", false)
                    t.check("prayer.drop", dr0 == "ok", "prayer off after the tenth ball launched at tick " .. tostring(launch_tick) .. ": " .. tostring(dd0))
                    t.rp_drop = select(2, t.tick())
                    prayer_dropped = true
                    off_launch = launch_tick
                    -- wait for the unprayed splat right here, a tick at a time: the press that follows is measured from it, so no other work of the loop may run in between
                    for w = 1, 12 do
                        local _, hw = t.ticklog.rows({ kind = "hit_player", since = mark_serial })
                        local seen = false
                        for _, r in ipairs(hw) do
                            if r.npc_slot == wslot and r.damage > 0 and r.tick >= off_launch then seen = true break end
                        end
                        if seen then break end
                        t.ticks(1)
                    end
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
                if fguard % 3 == 0 and not (prayer_dropped and not unprayed_done) and not XB.off then
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
                    t.ticks(2)
                    t.shot("sotetseg_killed_death_anim")
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
                    t.check("maze" .. mi .. ".proc", true, "sharks left " .. tostring(select(2, t.inv.count("shark"))) .. ", extra unprayed balls " .. XB.n .. "; retype row on his slot at tick " .. P .. " (" .. tostring(proc.from_type) .. " to " .. tostring(proc.to_type) .. "), hitpoints " .. bhp0.level)
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
                                -- full hitpoints first (so the five splats of the stay are taken at well separated
                                -- hitpoints and nothing else lands), then two ticks so the client's reading is current
                                for _ = 1, 1 do
                                    local _, hfs = t.skill.read("hitpoints")
                                    if hfs.level >= 80 then break end
                                    t.player.inv_op("shark", 1)
                                    t.ticks(3)
                                end
                                t.ticks(2)
                                local _, hpB = t.skill.read("hitpoints")
                                local tkB = select(2, t.tick())
                                local xr, xd = t.player.step_tick(wx, wz)
                                -- four splats on consecutive ticks (issue tick I, splats at I+2..I+5)
                                t.ticks(4)
                                local hpC, tkC = hpB, tkB
                                local br2, bd2 = t.player.step_tick(cx, cz)
                                mz.rag = { wx = wx, wz = wz, hpB = hpB.level, tkB = tkB, hpC = hpC.level, tkC = tkC, out = xr, back = br2 }
                                t.check("maze" .. mi .. ".rag_step", xr == "ok" and br2 == "ok",
                                    "wrong tile " .. wx .. "," .. wz .. " off the path at row 2: " .. tostring(xd) .. "; back: " .. tostring(bd2))
                                -- eat back above 55 (one shark) before the tornado's row (the food is a fixed kit of 14 sharks)
                                for _ = 1, 2 do
                                    local _, hfe = t.skill.read("hitpoints")
                                    if hfe.level >= 55 then break end
                                    t.player.inv_op("shark", 1)
                                    t.ticks(3)
                                end
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
                            if mi == 1 and cz - sz == 6 and not mz.shot_outrun then
                                mz.shot_outrun = true
                                t.shot("technique_tornado_outrun_on_row_7")
                            end
                            if mi == 1 and cz - sz == 10 and not mz.shot_lit then
                                mz.shot_lit = true
                                t.shot("technique_walking_the_lit_path_row_11")
                            end
                            -- maze 2, row 4 (the tornado's row): let it rise, step back onto row 3 so it sinks, then go on
                            -- (the second maze's tornado is not walked back to: the first maze read its hit)
                            if mi == 2 and cz - sz == 3 and mz.stepback == nil and #mz.order >= 2 then
                                t.ticks(2)
                                local pxo, pzo = mz.order[#mz.order - 1][1], mz.order[#mz.order - 1][2]
                                local sbr, sbd = t.player.step_tick(pxo, pzo)
                                mz.stepback = select(2, t.tick())
                                t.ticks(3)
                                local sfr, sfd = t.player.step_tick(cx, cz)
                                t.check("maze2.step_back", sbr == "ok" and sfr == "ok",
                                    "stepped from row 4 back to row 3 at tick " .. mz.stepback .. ": " .. tostring(sbd) .. "; forward again: " .. tostring(sfd))
                            end
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
                    -- the tornado's hit was read in the first maze: the food is a fixed kit, so the second maze's
                    -- tornado is not walked back to
                    local skip_walk = mi > 1 and maze[1] ~= nil and maze[1].tor_hit ~= nil
                    for w = 1, 32 do
                        if skip_walk and w >= 10 then break end
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
                        if tslot ~= nil and (mz.tor_anim == nil or mz.tor_anim < 0) then
                            local tsr, tst = t.npc.state("tob_sotetseg_creeper_story")
                            if tsr == "ok" then
                                mz.tor_anim = tst.anim_id
                                if tst.ready_anim ~= nil and tst.ready_anim >= 0 then tor_ready_anim, tor_walk_anim, tor_pose_kind = tst.ready_anim, tst.walk_anim, tst.pose_kind end
                                mz.tor_text = t.npc.state_text(tst)
                            end
                        end
                        if not skip_walk and (tslot == nil or (ttick == nil and w >= 8)) then
                            tstall = "never moved from its spawn tile"
                            if tslot ~= nil then
                                -- it stands on the path's first tile: walk back along the path onto it, so the hit it deals is read
                                for e = 1, 2 do
                                    local _, hq1 = t.skill.read("hitpoints")
                                    if hq1.level >= 70 then break end
                                    t.player.inv_op("shark", 1)
                                    eat_ticks[#eat_ticks + 1] = select(2, t.tick())
                                    sharks_eaten = sharks_eaten + 1
                                    t.ticks(2)
                                end
                                walked_back = true
                                for i = #mz.order - 1, 1, -1 do
                                    t.player.step_tick(mz.order[i][1], mz.order[i][2])
                                    local _, hh2 = t.ticklog.rows({ kind = "hit_player", since = proc.serial })
                                    for _, r in ipairs(hh2) do
                                        if r.damage >= 30 and r.tick > P + 3 then tor_hit = r end
                                    end
                                    if tor_hit ~= nil then break end
                                end
                                local _, lvw = t.world.level()
                                if lvw ~= 3 then break end
                            end
                            break
                        end
                        if not skip_walk and ttick ~= nil and now2 - ttick >= 3 and not walked_back and (tx ~= cx or tz ~= cz) then
                            tstall = tx .. "," .. tz .. " since tick " .. ttick
                            local idx = nil
                            for i, o in ipairs(mz.order) do
                                if o[1] == tx and o[2] == tz then idx = i end
                            end
                            if idx ~= nil and mz.order[idx][2] - sz >= 3 then
                                walked_back = true
                                for e = 1, 2 do
                                    local _, hq0 = t.skill.read("hitpoints")
                                    if hq0.level >= 70 then break end
                                    t.player.inv_op("shark", 1)
                                    eat_ticks[#eat_ticks + 1] = select(2, t.tick())
                                    sharks_eaten = sharks_eaten + 1
                                    t.ticks(3)
                                end
                                -- every step is followed by a read of the log: the tornado deals its hit on each tick it shares
                                -- a tile, so the walk stops on the first hit and stands still while it moves on
                                local order_walk = {}
                                for i = #mz.order - 1, idx, -1 do order_walk[#order_walk + 1] = i end
                                for i = idx + 1, #mz.order do order_walk[#order_walk + 1] = i end
                                for _, i in ipairs(order_walk) do
                                    t.player.step_tick(mz.order[i][1], mz.order[i][2])
                                    local _, hh3 = t.ticklog.rows({ kind = "hit_player", since = proc.serial })
                                    for _, r in ipairs(hh3) do
                                        if r.damage >= 30 and r.tick > P + 3 then tor_hit = r end
                                    end
                                    if tor_hit ~= nil then break end
                                end
                            else
                                break
                            end
                        else
                            local _, hq = t.skill.read("hitpoints")
                            if hq.level < 50 then
                                t.player.inv_op("shark", 1)
                                t.ticks(2)
                            end
                            t.ticks(1)
                        end
                    end
                    if tor_hit ~= nil then
                        -- the tornado hits on each tick it shares a tile (two hits of 35-45) and the step off the grid
                        -- still deals its rag: eat back above the rag before leaving
                        for e = 1, 3 do
                            local _, hqe = t.skill.read("hitpoints")
                            if hqe.level >= 60 then break end
                            t.player.inv_op("shark", 1)
                            eat_ticks[#eat_ticks + 1] = select(2, t.tick())
                            sharks_eaten = sharks_eaten + 1
                            t.ticks(3)
                        end
                    end
                    if tor_hit ~= nil and walked_back then
                        -- the walk stopped on the hit tile: finish the path behind the tornado, which is a tile ahead
                        local _, atw = t.world.tile()
                        local widx = nil
                        for i, o in ipairs(mz.order) do
                            if o[1] == atw.x and o[2] == atw.z then widx = i end
                        end
                        if widx ~= nil then
                            for i = widx + 1, #mz.order do
                                t.player.step_tick(mz.order[i][1], mz.order[i][2])
                            end
                        end
                    end
                    mz.tor_hit = tor_hit
                    t.check("maze" .. mi .. ".tornado_hit", true,
                        "tornado hit " .. tostring(tor_hit and tor_hit.damage) .. " at tick " .. tostring(tor_hit and tor_hit.tick) .. "; stalled: " .. tostring(tstall) .. "; walked back to it: " .. tostring(walked_back) .. "; tornado state " .. tostring(mz.tor_text) .. "; runner at " .. cx .. "," .. cz)
                    -- the portal at the path's north end, read from the world while standing in the realm
                    local por, pord = t.world.loc_near("tob_sotetseg_darkrealm_exit", 40)
                    if por == "ok" then
                        mz.portal_id = pord.id
                        mz.portal_text = "tile " .. tostring(pord.tile_x) .. "," .. tostring(pord.tile_z) .. " level " .. tostring(pord.level)
                    end
                    t.check("maze" .. mi .. ".portal_loc", por == "ok", "world read at the path's end: " .. tostring(por) .. " " .. tostring(mz.portal_text))
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
                        if mi == 1 then t.shot("technique_off_on_3_step_resolved") end
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
                        "boss hitpoints read " .. tostring(hp_after) .. " after the maze, the pool " .. pool .. " less " .. dealt .. " dealt by tick " .. hp_tick .. " is " .. (pool - dealt) .. "; sharks eaten so far " .. sharks_eaten)
                end
            end
        end
        t.expect("fight.over", (death_tick ~= nil) and "ok" or "fail", "boss npc_death at tick " .. tostring(death_tick) .. " after " .. fguard .. " loop ticks, " .. mazes_done .. " mazes, " .. sharks_eaten .. " sharks, " .. potions_drunk .. " restores, " .. vuln_casts .. " vulnerability casts")

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
        t.expect("technique.no_melee_at_range", (far_melee == 0 and melee_n >= 1) and "ok" or "fail",
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
        do
        local seg, segs, seg_proc, segs_out = 0, {}, false, {}
        local retype_set = {}
        for _, rt in ipairs(retype_ticks) do retype_set[rt] = true end
        for _, a in ipairs(attacks) do
            if a.kind == "ball" then
                seg = seg + 1
                if retype_set[a.tick] then seg_proc = true end
            end
            if a.kind == "death" then
                if seg_proc then segs_out[#segs_out + 1] = seg else segs[#segs + 1] = seg end
                seg, seg_proc = 0, false
            end
        end
        t.expect("spec.sotetseg.magic_per_ball", #segs > 0 and "ok" or "none",
            "measured " .. table.concat(segs, ",") .. " count, ordinary balls before each of " .. #segs .. " death balls; " .. #segs_out .. " death ball(s) left out because a ball was cast on a maze proc tick inside its run, which that run held " .. table.concat(segs_out, ",") .. " balls before the death ball (spec 10 count, grade B, tol exact)")
        t.expect("spec.sotetseg.magic_per_ball_proc_tick", #segs_out > 0 and "ok" or "none",
            "measured " .. table.concat(segs_out, ",") .. " count, ordinary balls before each of " .. #segs_out .. " death balls whose run held a ball he threw on a maze proc tick (that ball is logged but not counted toward the death ball) (spec 11 count, grade D, tol exact)")
        end
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
        -- two splats of his on one tick (a death ball landing with a melee swing) cannot be told apart in the log: neither is a death ball sample
        melee_used.on_tick, melee_used.shared = {}, 0
        for _, h in ipairs(boss_hits) do melee_used.on_tick[h.tick] = (melee_used.on_tick[h.tick] or 0) + 1 end
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
                melee_splat_ticks[h.tick] = true
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
                    if melee_used.on_tick[h.tick] > 1 then
                        melee_used.shared = melee_used.shared + 1
                    else
                        death_hits[#death_hits + 1] = h.damage
                    end
                    death_flights[#death_flights + 1] = d
                    death_dists[#death_dists + 1] = a.dist or -1
                end
            else
                ball_dmg[#ball_dmg + 1] = h.damage
                local in_xb = false
                for _, xl in ipairs(xb_launches) do
                    if h.tick >= xl and h.tick <= xl + 10 then in_xb = true end
                end
                if ok_issue ~= nil and h.tick > ok_issue and not in_xb then ball_after = ball_after + 1; ball_late = (ball_late or "") .. " t" .. h.tick .. ":" .. h.damage end
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
        t.expect("spec.sotetseg.melee_hit_delay", (#delays > 0 and dall_one) and "ok" or "refused",
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
        t.expect("technique.prayed_balls_blocked", (blocks > 0 and ball_after == 0) and "ok" or "fail",
            blocks .. " ball splats blocked to zero by Protect from Magic, " .. ball_after .. " ball splats that hurt after the prayer was back up at tick " .. tostring(ok_issue) .. (ball_late or ""))
        local bmax = 0
        for _, d in ipairs(ball_dmg) do if d > bmax then bmax = d end end
        t.expect("spec.sotetseg.ball_max_entry", #ball_dmg > 0 and "ok" or "none",
            "measured " .. bmax .. " hp, largest of " .. #ball_dmg .. " unprayed ball splats " .. table.concat(ball_dmg, "/") .. " (spec 22 hp, grade D, tol range)")
        -- the ball that hurt was thrown at H - 8 (landing H - 1): find its projectile row and the tick the prayer went off
        t.rp_throw = nil
        do
            local _, rp_pj = t.ticklog.rows({ kind = "projectile", since = mark_serial })
            for _, r in ipairs(rp_pj) do
                if H ~= nil and r.spotanim == PLAIN_BALL and r.tick == H - 1 - math.floor(r.end_cycle / 30) then t.rp_throw = r end
            end
        end
        t.rp_in_flight = t.rp_throw ~= nil and t.rp_drop ~= nil and t.rp_throw.tick <= t.rp_drop and t.rp_drop < H - 1
        t.rp_unprayed = t.rp_in_flight and (hit_dmg or 0) > 0
        t.check("technique.ball_prayer_dropped_in_flight", t.rp_unprayed == true and refused_n >= 1,
            "ball thrown at tick " .. tostring(t.rp_throw and t.rp_throw.tick) .. " with the prayer up, dropped at tick " .. tostring(t.rp_drop) .. ", landing " .. tostring(H and H - 1) .. ": unprayed splat of " .. tostring(hit_dmg) .. " at tick " .. tostring(H) .. ", then " .. refused_n .. " refused presses; and the first ball, prayer off at the throw and on in flight: " .. tostring(t.ball_read_text))
        t.check("spec.sotetseg.ball_prayer_read_tick", t.ball_read_on == true and t.rp_unprayed == true and refused_n >= 1,
            "measured landing (spec landing, grade B, tol exact)")
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
            "measured " .. dmax .. " hp, largest of " .. #death_hits .. " death ball splats " .. table.concat(death_hits, "/") .. ", " .. melee_used.shared .. " death ball flight(s) counted but their damage left out because a melee splat landed on the same tick (spec 15 hp, grade D, tol exact)")

        -- his record
        t.expect("spec.sotetseg.hp_entry_per_player", pool ~= nil and "ok" or "none",
            "measured " .. tostring(pool) .. " hp, read from ::tobboss in the solo Entry room before the fight (spec 560 hp, grade A, tol exact)")
        t.expect("spec.sotetseg.attack_level", att_now ~= nil and "ok" or "none",
            "measured " .. tostring(att_now) .. " count, his Attack in the Entry room, read from ::tobboss (spec 250,180,350 count, grade A, tol exact)")
        t.expect("spec.sotetseg.defence_level", def_base ~= nil and "ok" or "none",
            "measured " .. tostring(def_base) .. " count, his Defence in the Entry room, read from ::tobboss (spec 200,150,200 count, grade A, tol exact)")

        t.expect("spec.sotetseg.defence_floor", (def_floor ~= nil and def_floor == 100) and "ok" or "refused",
            "measured " .. tostring(def_floor) .. " count, his Defence of " .. tostring(def_base) .. " read from ::tobboss after each Elder maul special (each takes 35 percent of his current Defence, no lower than the floor); before>after per special: " .. def_reads_text .. " (spec 100 count, grade D, tol exact)")

        ------------------------------------------------------------------
        -- 4. the mazes
        ------------------------------------------------------------------
        local trig_text, cross_text = {}, {}
        local idle_cast = {}
        local _, pan_early = t.ticklog.rows({ kind = "player_anim" })
        local trig, idle, noatt, tele, move, cyc, offs, postfirst, after_death = {}, {}, {}, {}, {}, {}, {}, {}, 0
        local chip_gaps, chip_dmg, first_chip, chip_ticks = {}, {}, {}, {}
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
                    if before_pct == nil and prev_pct > thr + 0.1 and after_pct <= thr + 0.1 then
                        before_pct, cross_off, after_cross_pct = prev_pct, h.tick - P, after_pct
                    end
                end
            end
            if before_pct == nil then before_pct, cross_off, after_cross_pct = (pool - lost) * 100 / pool, 0, (pool - lost) * 100 / pool end
            trig[#trig + 1] = string.format("%.1f", (pool - lost) * 100 / pool)
            cross_text[#cross_text + 1] = "maze " .. k .. " crossing splat at proc" .. (cross_off >= 0 and "+" or "") .. cross_off
            local near_hits = ""
            for _, h in ipairs(hitn) do
                if h.tick >= P - 6 and h.tick <= P + 1 then near_hits = near_hits .. " " .. h.damage .. "@" .. (h.tick - P) end
            end
            trig_text[#trig_text + 1] = "maze " .. k .. " proc tick " .. P .. " splats (damage@tick-from-proc):" .. near_hits
            local ptick = nil
            for _, pr in ipairs(pan_early) do
                if pr.seq == 1816 and pr.tick >= P - 1 and pr.tick <= P + 1 then ptick = pr.tick end
            end
            local cast_on_proc = false
            for _, a in ipairs(attacks) do
                if a.tick == P and a.kind ~= "melee" then cast_on_proc = true end
            end
            if ptick ~= nil then
                idle[#idle + 1] = ptick - P
            elseif cast_on_proc then
                idle_cast[#idle_cast + 1] = P
            else
                idle[#idle + 1] = -99
            end
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
                chip_ticks[#chip_ticks + 1] = c.tick
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
            if tonumber(v2) > ((i2 == 1) and 66.7 or 33.4) then trig_ok = false end
        end
        t.expect("spec.sotetseg.maze_trigger_hp", trig_ok and "ok" or "refused",
            "measured " .. table.concat(trig, ",") .. " percent, his pool of " .. pool .. " less every splat landed before each proc tick (the npc phase runs before the player phase); the crossing splat: " .. table.concat(cross_text, ", ") .. "; " .. table.concat(trig_text, "; ") .. " (spec 66.6,33.3 percent, grade C, tol +-4)")
        t.expect("spec.sotetseg.maze_boss_idle_at_proc", #idle > 0 and "ok" or "none",
            "measured " .. table.concat(idle, ",") .. " ticks, the runner's player_anim 1816 against his npc_retype row; " .. #idle_cast .. " proc tick(s) with no player_anim row; ticks " .. table.concat(idle_cast, ",") .. ") (spec 0 ticks, grade B, tol exact)")
        do
            -- the tornado rises (npc_anim on its spawn tick) and sinks (npc_anim 9005, npc_free the tick after)
            local sp_seqs, de_seqs, de_ticks = {}, {}, {}
            local n_tor, n_gone_with_realm = 0, 0
            local tor_diag = {}
            local n_own_leave, own_text = 0, {}
            for _, mz2 in ipairs(maze) do
                for _, sp in ipairs(spawns) do
                    if sp.level == 3 and sp.tick > mz2.proc and sp.tick < mz2.react and sp.type ~= nil then
                        n_tor = n_tor + 1
                        local _, tar = t.ticklog.rows({ kind = "npc_anim", slot = sp.slot })
                        local _, tfr = t.ticklog.rows({ kind = "npc_free", slot = sp.slot })
                        local on_spawn, dtick, dseq = 0, nil, nil
                        for _, a in ipairs(tar) do
                            if a.tick == sp.tick and on_spawn == 0 then on_spawn = a.seq end
                            if a.seq == 9005 and a.tick > sp.tick and a.tick <= mz2.react + 1 and dtick == nil then
                                dtick, dseq = a.tick, a.seq
                            end
                        end
                        sp_seqs[#sp_seqs + 1] = on_spawn
                        local ftick = nil
                        for _, f in ipairs(tfr) do
                            if f.tick > sp.tick and f.tick <= mz2.react + 2 and ftick == nil then ftick = f.tick end
                        end
                        local raw_a, raw_f = {}, {}
                        for _, a in ipairs(tar) do if a.tick >= sp.tick then raw_a[#raw_a + 1] = a.seq .. "@" .. a.tick .. "#" .. a.serial end end
                        for _, f in ipairs(tfr) do if f.tick >= sp.tick then raw_f[#raw_f + 1] = f.tick .. "#" .. f.serial end end
                        tor_diag[#tor_diag + 1] = "spawn " .. sp.tick .. " anims " .. table.concat(raw_a, ",") .. " frees " .. table.concat(raw_f, ",") .. " react " .. mz2.react
                        if dtick ~= nil and ftick ~= nil and mz2.stepback ~= nil and dtick >= mz2.stepback and dtick <= mz2.stepback + 6 then
                            de_seqs[#de_seqs + 1] = dseq
                            de_ticks[#de_ticks + 1] = ftick - dtick
                        elseif dtick ~= nil and ftick ~= nil then
                            de_seqs[#de_seqs + 1] = dseq
                            n_own_leave = n_own_leave + 1
                            own_text[#own_text + 1] = "9005@" .. dtick .. " free@" .. ftick
                        else
                            n_gone_with_realm = n_gone_with_realm + 1
                        end
                    end
                end
            end
            t.expect("spec.sotetseg.av.tornado.spawn_seq", #sp_seqs > 0 and "ok" or "none",
                "measured " .. table.concat(sp_seqs, ",") .. " count, npc_anim seq on each of " .. n_tor .. " tornadoes' npc_spawn tick (0 = none); " .. table.concat(tor_diag, "; ") .. " (spec 9004 count, grade D, tol exact)")
            t.expect("spec.sotetseg.av.tornado.despawn_seq", #de_seqs > 0 and "ok" or "none",
                "measured " .. table.concat(de_seqs, ",") .. " count, npc_anim 9005 on each tornado that left before the maze ended; " .. n_gone_with_realm .. " went with the realm without it (spec 9005 count, grade D, tol exact)")
            t.expect("spec.sotetseg.tornado_despawn_ticks", #de_ticks > 0 and "ok" or "none",
                "measured " .. table.concat(de_ticks, ",") .. " ticks, from the 9005 row to the npc_free row of each tornado that sank when the runner stepped back onto row 3 (" .. n_own_leave .. " tornado(es) that left by their own step, not counted: " .. table.concat(own_text, ", ") .. ") (spec 1 ticks, grade D, tol exact)")
        end
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
        t.expect("technique.off_on_3", (off_ok) and "ok" or "fail", #offs .. " mazes left with a step resolving on cycle tick 3; re-activation came " .. table.concat(offs, ",") .. " tick(s) later")
        local ahead = 0
        for _, mz in ipairs(maze) do ahead = ahead + (mz.tor_before or 0) end
        t.expect("technique.tornado_outrun", (tor_rows[1] ~= nil and ahead == 0) and "ok" or "fail",
            "the tornado spawned at tick " .. tostring(tor_rows[1] and tor_rows[1].tick) .. " when the runner stepped onto row 4, and the runner, one tile a tick ahead of it, took " .. ahead .. " tornado splats on the way to the path's end of " .. #maze .. " mazes")
        t.expect("technique.walked_only_lit_tiles", (maze_visits_off_path <= 5 and maze_ticks_on_grid > 20) and "ok" or "fail",
            maze_ticks_on_grid .. " player_tile rows in the shadow realm, " .. maze_visits_off_path .. " of them on a tile the loc_set rows never lit (the deliberate wrong-tile step stood 4 ticks)")

        -- rag: the wrong-tile excursion of maze 1
        local rag_gaps = {}
        local rag_samples = {}               -- { hitpoints before the splat, damage }
        local prev_hpb, prev_dmg, prev_tick = nil, nil, nil
        for i, r in ipairs(rag_rows) do
            local rg = r.maze.rag
            local hpb = nil
            -- the client's hitpoints lag a tick: the first splat's hitpoints are the reading before it,
            -- the next splat of the same stay follows from it
            if prev_tick ~= nil and r.tick == prev_tick + 1 then
                hpb = prev_hpb - prev_dmg
                -- the realm chip (1-3, every 7 ticks) comes before the rag within a tick
                for ci, ct in ipairs(chip_ticks) do
                    if ct > prev_tick and ct <= r.tick then hpb = hpb - chip_dmg[ci] end
                end
            elseif rg ~= nil and rg.tkB < r.tick then
                hpb = rg.hpB
                for ci, ct in ipairs(chip_ticks) do
                    if ct > rg.tkB and ct <= r.tick then hpb = hpb - chip_dmg[ci] end
                end
            end
            prev_hpb, prev_dmg, prev_tick = hpb, r.damage, r.tick
            if hpb ~= nil then rag_samples[#rag_samples + 1] = { hpb, r.damage } end
            if i > 1 then rag_gaps[#rag_gaps + 1] = r.tick - rag_rows[i - 1].tick end
        end
        local rag_dmgs, rag_hps = {}, {}
        for i, sm in ipairs(rag_samples) do rag_dmgs[i] = sm[2] rag_hps[i] = sm[1] end
        -- the solve: damage = f + floor(p * hitpoints). For every whole f the share p must satisfy
        -- (damage - f) / hp <= p < (damage - f + 1) / hp at every sample; keep each f with a non-empty interval
        local fits, fit_f_lo, fit_f_hi, fit_p_lo, fit_p_hi = {}, nil, nil, 2000, 0
        for f = 0, 40 do
            local lo, hi = 0, 1000
            for _, sm in ipairs(rag_samples) do
                local part = sm[2] - f
                if part < 0 then lo, hi = 1000, 0 break end
                if part * 1000 / sm[1] > lo then lo = part * 1000 / sm[1] end
                if (part + 1) * 1000 / sm[1] < hi then hi = (part + 1) * 1000 / sm[1] end
            end
            if #rag_samples > 0 and lo < hi then
                fits[#fits + 1] = string.format("f=%d p=%.1f-%.1f", f, lo, hi)
                fit_f_lo = fit_f_lo or f
                fit_f_hi = f
                if lo < fit_p_lo then fit_p_lo = lo end
                if hi > fit_p_hi then fit_p_hi = hi end
            end
        end
        local solve_text = #rag_samples .. " rag splats " .. table.concat(rag_dmgs, "/") .. " at hitpoints " .. table.concat(rag_hps, "/") ..
            "; damage = f + floor(p*hitpoints) solved for every whole f, fitting: " .. (#fits > 0 and table.concat(fits, "; ") or "none") .. ";"
        if #rag_samples >= 2 and fit_f_lo ~= nil then
            local fval = fit_f_lo == fit_f_hi and tostring(fit_f_lo) or (fit_f_lo .. "-" .. fit_f_hi)
            local pval = string.format("%.1f-%.1f", fit_p_lo, fit_p_hi)
            t.expect("spec.sotetseg.rag_flat_entry", "ok",
                "measured " .. fval .. " hp, " .. solve_text .. " the flat part is not assumed (spec 11 hp, grade D, tol bracket<=6)")
            t.expect("spec.sotetseg.rag_percent", "ok",
                "measured " .. pval .. " permille, " .. solve_text .. " the share is not assumed (spec 66.7 permille, grade C, tol bracket<=60)")
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
            do
                local _, wl = t.msg.last(30)
                local wave_raw = nil
                for _, m in ipairs(wl or {}) do
                    if string.find(m.text, "complete!", 1, true) and string.find(m.text, "Wave", 1, true) then wave_raw = m.text end
                end
                local wave_txt = wave_raw and string.gsub(string.gsub(wave_raw, "<br>", " "), "<[^>]*>", "") or "none"
                t.check("room.wave_line", wave_raw ~= nil and string.find(wave_txt, "(Entry Mode) complete!", 1, true) ~= nil and string.find(wave_txt, "Duration: %d+:%d+") ~= nil,
                    "chat line after his death: " .. wave_txt)
            end
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
        ------------------------------------------------------------------
        -- 6. what the room shows and plays (sotetseg.av.*): every figure is read from tick-log rows on the tick of the event that owns it
        ------------------------------------------------------------------
        do
            local _
            local AV, EV = {}, {}
            local byk, idx, fld, firstseen = {}, {}, {}, {}
            _, AV.snd = t.ticklog.rows({ kind = "sound" })
            t.ticks(1)
            _, AV.mus = t.ticklog.rows({ kind = "music" })
            _, AV.jin = t.ticklog.rows({ kind = "jingle" })
            _, AV.pan = t.ticklog.rows({ kind = "player_anim" })
            _, AV.pfx = t.ticklog.rows({ kind = "player_spotanim" })
            _, AV.mfx = t.ticklog.rows({ kind = "map_spotanim" })
            t.ticks(1)
            _, AV.loc = t.ticklog.rows({ kind = "loc_set" })
            t.ticks(1)
            for _, k in ipairs({ "bshort", "blong", "bossanim", "synth", "area", "npcsnd", "mus", "jin", "pan", "pfx", "mfx", "loc3", "loc0" }) do
                byk[k] = {}
            end
            fld.bshort, fld.blong, fld.bossanim = "spotanim", "spotanim", "seq"
            fld.synth, fld.area, fld.npcsnd = "sound", "sound", "sound"
            fld.mus, fld.jin, fld.pan, fld.pfx, fld.mfx = "track", "jingle", "seq", "spotanim", "spotanim"
            fld.loc3, fld.loc0 = "loc", "loc"
            -- his projectiles: the ones launched from inside his footprint (the player's own darts start at the player)
            for _, r in ipairs(projs) do
                if r.src_level == 0 and r.src_x >= bx - 1 and r.src_x <= bx + 5 and r.src_z >= bz - 1 and r.src_z <= bz + 5 then
                    if r.end_cycle - r.start_cycle > 300 then
                        byk.blong[#byk.blong + 1] = r
                    else
                        byk.bshort[#byk.bshort + 1] = r
                    end
                end
            end
            local _, fresh_anims = t.ticklog.rows({ kind = "npc_anim", slot = wslot })
            byk.bossanim = fresh_anims
            local silent_area, silent_synth = 0, 0
            for _, r in ipairs(AV.snd) do
                if r.source == "synth" then
                    if r.loops >= 1 then byk.synth[#byk.synth + 1] = r else silent_synth = silent_synth + 1 end
                elseif r.source == "area" then
                    if r.loops >= 1 then byk.area[#byk.area + 1] = r else silent_area = silent_area + 1 end
                elseif r.source == "npc" and r.npc_slot == wslot then
                    byk.npcsnd[#byk.npcsnd + 1] = r
                end
            end
            for _, r in ipairs(AV.mus) do
                if r.source == "script" then byk.mus[#byk.mus + 1] = r end
            end
            byk.jin, byk.pan, byk.pfx, byk.mfx = AV.jin, AV.pan, AV.pfx, AV.mfx
            -- the arena floor per tick: how many level 0 loc_set rows, and which ids
            local cnt0, ids0 = {}, {}
            for _, r in ipairs(AV.loc) do
                if r.level == 3 then
                    byk.loc3[#byk.loc3 + 1] = r
                else
                    byk.loc0[#byk.loc0 + 1] = r
                    cnt0[r.tick] = (cnt0[r.tick] or 0) + 1
                    ids0[r.tick] = ids0[r.tick] or {}
                    ids0[r.tick][r.loc] = (ids0[r.tick][r.loc] or 0) + 1
                end
            end
            for k, rows in pairs(byk) do
                idx[k], firstseen[k] = {}, {}
                for _, r in ipairs(rows) do
                    idx[k][r.tick] = idx[k][r.tick] or {}
                    idx[k][r.tick][#idx[k][r.tick] + 1] = r
                    local id = r[fld[k]]
                    if firstseen[k][id] == nil or r.tick < firstseen[k][id] then firstseen[k][id] = r.tick end
                end
            end
            t.ticks(1)
            -- the events
            EV.ball_ticks, EV.death_ticks, EV.land_ticks, EV.melee_pre = {}, {}, {}, {}
            for _, r in ipairs(byk.bshort) do
                EV.ball_ticks[#EV.ball_ticks + 1] = r.tick
                local fl = math.floor((r.end_cycle - r.start_cycle) / 30)
                local found = nil
                for _, h in ipairs(hits) do
                    if h.tick >= r.tick + fl and h.tick <= r.tick + fl + 2 and found == nil then found = h.tick end
                end
                if found ~= nil then EV.land_ticks[#EV.land_ticks + 1] = found end
            end
            EV.death_near, EV.death_far = {}, {}
            for _, r in ipairs(byk.blong) do
                EV.death_ticks[#EV.death_ticks + 1] = r.tick
                local pp = pos[r.tick]
                local dd = -1
                if pp ~= nil and pp.level == 0 then dd = math.max(math.abs(pp.x - (bx + 2)), math.abs(pp.z - (bz + 2))) end
                if dd >= 0 and dd <= 10 then
                    EV.death_near[#EV.death_near + 1] = r.tick
                else
                    EV.death_far[#EV.death_far + 1] = r.tick .. "@" .. dd
                end
            end
            for _, h in ipairs(hits) do
                local p = pos[h.tick - 1]
                if h.npc_slot == wslot and melee_splat_ticks[h.tick] and p ~= nil and p.level == 0 and idx.bossanim[h.tick - 1] ~= nil then
                    local dx, dz = 0, 0
                    if p.x < bx then dx = bx - p.x elseif p.x > bx + 4 then dx = p.x - (bx + 4) end
                    if p.z < bz then dz = bz - p.z elseif p.z > bz + 4 then dz = p.z - (bz + 4) end
                    if math.max(dx, dz) <= 1 then EV.melee_pre[#EV.melee_pre + 1] = h.tick - 1 end
                end
            end
            EV.proc_ticks, EV.react_ticks, EV.tor_spawn_ticks, EV.tor_hit_ticks, EV.rag_ticks, EV.hitn_ticks = {}, {}, {}, {}, {}, {}
            EV.tor_types, EV.tor_anims, EV.portal_ids = {}, {}, {}
            for k, mz in ipairs(maze) do
                EV.proc_ticks[#EV.proc_ticks + 1] = mz.proc
                EV.react_ticks[#EV.react_ticks + 1] = mz.react
                if mz.tor_hit ~= nil then EV.tor_hit_ticks[#EV.tor_hit_ticks + 1] = mz.tor_hit.tick end
                if mz.tor_anim ~= nil then EV.tor_anims[#EV.tor_anims + 1] = tostring(mz.tor_anim) end
                if mz.portal_id ~= nil then EV.portal_ids[#EV.portal_ids + 1] = tostring(mz.portal_id) end
                for _, sp in ipairs(spawns) do
                    if sp.level == 3 and sp.tick > mz.proc and sp.tick < mz.react then
                        EV.tor_spawn_ticks[#EV.tor_spawn_ticks + 1] = sp.tick
                        EV.tor_types[#EV.tor_types + 1] = tostring(sp.type)
                    end
                end
            end
            local rag_seen = {}
            for _, r in ipairs(rag_rows) do
                if not rag_seen[r.tick] then
                    rag_seen[r.tick] = true
                    EV.rag_ticks[#EV.rag_ticks + 1] = r.tick
                end
            end
            for _, h in ipairs(hitn) do EV.hitn_ticks[#EV.hitn_ticks + 1] = h.tick end
            EV.fight_start = { mark_tick }
            local E = {}
            -- name, row class, events, window lo, hi, first-occurrence filter, spec, grade, tol, what it measures
            E[#E + 1] = { "ball.seq", "bossanim", EV.ball_ticks, 0, 0, false, "8139", "C", "exact", "seq of the boss's npc_anim row on the launch tick of each of his ordinary (short-flight) projectiles" }
            E[#E + 1] = { "ball.proj", "bshort", EV.ball_ticks, 0, 0, false, "1606", "C", "exact", "spotanim of the projectile launched from his footprint on each ordinary cast tick" }
            E[#E + 1] = { "ball.cast_sound", "synth", EV.ball_ticks, 0, 0, false, "3539", "D", "exact", "script sounds (synth, loops >= 1) common to every ordinary cast tick" }
            E[#E + 1] = { "ball.hit_sound", "synth", EV.land_ticks, -1, 0, false, "3996", "D", "exact", "script sounds common to the window hit_player tick-1..tick of the splat each ordinary ball landed with (prayed or not)" }
            E[#E + 1] = { "melee.seq", "bossanim", EV.melee_pre, 0, 0, false, "8138", "C", "exact", "seq of the boss's npc_anim row the tick before each of his splats while the player stood at most one tile from his footprint" }
            E[#E + 1] = { "death_ball.seq", "bossanim", EV.death_ticks, 0, 0, false, "8139", "C", "exact", "seq of the boss's npc_anim row on the launch tick of each long-flight (death ball) projectile" }
            E[#E + 1] = { "death_ball.proj", "blong", EV.death_ticks, 0, 0, false, "1604", "C", "exact", "spotanim of the long-flight projectile launched from his footprint" }
            E[#E + 1] = { "death_ball.cast_sound", "area", EV.death_near, 0, 0, true, "3994", "D", "exact", "area sounds (loops >= 1) on each death-ball cast tick with the raider within its radius of 10 tiles of his centre, first heard at the first of them" }
            -- a death ball still in flight when a maze opens never lands on the raider (he is in the realm)
            do
            local death_land, death_lost = {}, {}
            for _, dt in ipairs(EV.death_ticks) do
                local lost = false
                for _, pt in ipairs(EV.proc_ticks) do
                    if pt >= dt and pt <= dt + 20 then lost = true end
                end
                if lost then death_lost[#death_lost + 1] = dt else death_land[#death_land + 1] = dt end
            end
            E[#E + 1] = { "death_ball.impact_gfx", "pfx", death_land, 12, 20, false, "1605", "D", "exact", "player_spotanim rows on the raider in the window cast+12..cast+20 common to every death ball that landed (" .. #death_lost .. " death ball(s) cast at ticks " .. table.concat(death_lost, ",") .. " left out: a maze opened before they could land)" }
            E[#E + 1] = { "death_ball.impact_sound", "synth", death_land, 12, 20, true, "3947", "D", "exact", "script sounds in the window cast+12..cast+20 common to every death ball that landed, first heard in the first one (" .. #death_lost .. " cast at ticks " .. table.concat(death_lost, ",") .. " left out: a maze opened before they could land)" }
            end
            local proc_clean, proc_cast = {}, {}
            for _, pt in ipairs(EV.proc_ticks) do
                local cast_here = false
                for _, bt in ipairs(EV.ball_ticks) do if bt == pt then cast_here = true end end
                for _, bt in ipairs(EV.death_ticks) do if bt == pt then cast_here = true end end
                if cast_here then proc_cast[#proc_cast + 1] = pt else proc_clean[#proc_clean + 1] = pt end
            end
            E[#E + 1] = { "maze.boss_seq", "bossanim", proc_clean, 0, 0, false, "0", "C", "exact", "seq of the boss's npc_anim row on each maze proc tick (the npc_retype tick) that is not also the tick of one of his ball casts; " .. #proc_cast .. " proc tick(s) left out because a ball was cast on the same tick (ticks " .. table.concat(proc_cast, ",") .. "), where the log holds the cast's seq and no maze seq" }
            E[#E + 1] = { "maze.proc_sound", "synth", EV.proc_ticks, 0, 0, true, "3963", "D", "exact", "script sounds on each proc tick, first heard at the first proc" }
            E[#E + 1] = { "maze.player_anim", "pan", EV.proc_ticks, 0, 0, true, "1816", "C", "exact", "player_anim rows on each proc tick, first played at the first proc" }
            E[#E + 1] = { "maze.lit_path", "loc3", EV.proc_ticks, 1, 8, false, "33035", "C", "exact", "loc_set rows on level 3 (the realm) in the window proc+1..proc+8 common to every maze" }
            E[#E + 1] = { "rag.gfx", "mfx", EV.rag_ticks, 0, 0, false, "505", "C", "exact", "map_spotanim rows on the tick of each rag splat" }
            E[#E + 1] = { "rag.sound", "synth", EV.rag_ticks, 0, 0, true, "3985", "D", "exact", "script sounds on the tick of each rag splat, first heard at the first rag" }
            E[#E + 1] = { "drain.gfx", "pfx", chip_ticks, 0, 0, false, "1608", "D", "exact", "player_spotanim rows on the raider on the tick of each realm chip splat" }
            E[#E + 1] = { "tornado.spawn_sound", "area", EV.tor_spawn_ticks, 0, 0, true, "4001", "D", "exact", "area sounds (loops >= 1) on each tornado's npc_spawn tick, first heard at the first spawn" }
            E[#E + 1] = { "tornado.hit_sound", "synth", EV.tor_hit_ticks, 0, 0, true, "3233", "D", "exact", "script sounds on the tick of each tornado splat, first heard at the first" }
            E[#E + 1] = { "hit.defend_sound", "npcsnd", EV.hitn_ticks, 0, 0, false, "4019", "D", "exact", "npc-source sound rows of his own slot on the tick of each hit_npc row on him" }
            E[#E + 1] = { "death.seq", "bossanim", { death_tick }, 1, 4, false, "8140", "D", "exact", "seq of the boss's npc_anim row in the window death+1..death+4 (the death tick itself can hold a ball cast started before the killing blow)" }
            E[#E + 1] = { "death.sound", "synth", { death_tick }, 0, 8, true, "3286", "D", "exact", "script sounds in the window death..death+8, first heard there" }
            E[#E + 1] = { "death.jingle", "jin", { death_tick }, 0, 10, false, "250", "D", "exact", "jingle rows in the window death..death+10" }
            E[#E + 1] = { "death.chest_loc", "loc0", { death_tick }, 0, 10, false, "32758", "D", "exact", "loc_set rows on level 0 in the window death..death+10" }
            E[#E + 1] = { "music.fight", "mus", EV.fight_start, -3, 3, false, "581", "D", "+-1", "script music rows within 3 ticks of the barrier click (room start mark)" }
            local OUT = {}
            for _, e in ipairs(E) do
                local common, per, nev = nil, {}, 0
                for _, tk in ipairs(e[3]) do
                    local here, hn = {}, 0
                    for d = e[4], e[5] do
                        for _, r in ipairs(idx[e[2]][tk + d] or {}) do
                            here[r[fld[e[2]]]] = true
                            hn = hn + 1
                        end
                    end
                    if common == nil then
                        common = here
                    else
                        for id, _ in pairs(common) do
                            if not here[id] then common[id] = nil end
                        end
                    end
                    per[#per + 1] = hn
                    nev = nev + 1
                end
                local vals = {}
                for id, _ in pairs(common or {}) do
                    local fine = true
                    if e[6] then
                        local ft = firstseen[e[2]][id]
                        fine = ft ~= nil and ft >= e[3][1] + e[4] and ft <= e[3][1] + e[5]
                    end
                    if fine then vals[#vals + 1] = tostring(id) end
                end
                table.sort(vals)
                local shown = {}
                for i = 1, math.min(#e[3], 8) do shown[i] = e[3][i] end
                local pshown = {}
                for i = 1, math.min(#per, 8) do pshown[i] = per[i] end
                OUT[#OUT + 1] = { name = e[1], vals = vals, spec = e[7], grade = e[8], tol = e[9],
                    free = e[10] .. ": " .. nev .. " events (first ticks " .. table.concat(shown, ",") .. "), rows in the window per event " .. table.concat(pshown, ",") .. (#per > 8 and "..." or "") .. (e[6] and "; first-occurrence filter on" or "") }
            end
            do
                local o0, o1, o2 = 0, 0, 0
                local sid = tonumber(OUT[4].vals[1])
                for _, lt in ipairs(EV.land_ticks) do
                    local here = false
                    for _, r in ipairs(idx.synth[lt] or {}) do if r.sound == sid then here = true end end
                    local before = false
                    for _, r in ipairs(idx.synth[lt - 1] or {}) do if r.sound == sid then before = true end end
                    if here then o0 = o0 + 1 elseif before then o1 = o1 + 1 else o2 = o2 + 1 end
                end
                OUT[4].free = OUT[4].free .. "; sound on the splat tick: " .. o0 .. ", one tick before it: " .. o1 .. ", neither: " .. o2
            end
            local by_name = {}
            for _, o in ipairs(OUT) do by_name[o.name] = o end
            -- the death ball's area sound: a cast beyond its radius of 10 tiles of the raider must send no row
            do
                local o = by_name["death_ball.cast_sound"]
                local far_rows = 0
                for _, r in ipairs(byk.blong) do
                    if idx.area[r.tick] ~= nil then
                        local pp = pos[r.tick]
                        if pp == nil or pp.level ~= 0 or math.max(math.abs(pp.x - (bx + 2)), math.abs(pp.z - (bz + 2))) > 10 then far_rows = far_rows + 1 end
                    end
                end
                o.free = o.free .. "; casts out of earshot (tick@distance, -1 = in the realm): " .. (#EV.death_far > 0 and table.concat(EV.death_far, ",") or "none") .. " sent " .. far_rows .. " rows; loops-0 area rows in the log: " .. silent_area
                if silent_area > 0 or far_rows > 0 then o.bad = true end
            end
            do
                local o = by_name["tornado.spawn_sound"]
                o.free = o.free .. "; loops-0 area rows in the log: " .. silent_area
                if silent_area > 0 then o.bad = true end
                o = by_name["hit.defend_sound"]
                local missing = 0
                for _, tk in ipairs(EV.hitn_ticks) do if idx.npcsnd[tk] == nil then missing = missing + 1 end end
                o.free = o.free .. "; hit ticks with no sound row: " .. missing
                if missing > 0 then o.bad = true end
            end
            -- the melee seq's own frame sound is not a row: the carrier is the npc_anim row, and no script sound doubles it
            do
                local n_anim, n_snd = 0, 0
                for _, a in ipairs(anims) do if a.seq == 8138 then n_anim = n_anim + 1 end end
                for _, r in ipairs(AV.snd) do if r.sound == 3540 then n_snd = n_snd + 1 end end
                OUT[#OUT + 1] = { name = "melee.frame_sound", vals = { (n_anim > 0 and n_snd == 0) and "3540" or "0" }, spec = "3540", grade = "A", tol = "exact",
                    free = "the frame sound of seq 8138 (tools/raid_gate/seq_frame_sounds.py 8138: frame 4, sound 3540) rides the npc_anim row: " .. n_anim .. " npc_anim rows of seq 8138 on his slot and " .. n_snd .. " script sound rows of 3540 in the log (a row would be the sound twice)" }
            end
            -- the floor: groups of 210 level 0 loc_set rows on one tick
            do
                local plain_ids, dark_ids, plain_ticks, dark_ticks = {}, {}, {}, {}
                local is_proc = {}
                for _, pt in ipairs(EV.proc_ticks) do is_proc[pt] = true end
                local gt = {}
                for tk, n in pairs(cnt0) do gt[#gt + 1] = tk end
                table.sort(gt)
                for _, tk in ipairs(gt) do
                    if cnt0[tk] == 210 then
                        local gid = nil
                        for id, _ in pairs(ids0[tk]) do gid = id end
                        if is_proc[tk] then
                            dark_ids[tostring(gid)] = true
                            dark_ticks[#dark_ticks + 1] = tk
                        else
                            plain_ids[tostring(gid)] = true
                            plain_ticks[#plain_ticks + 1] = tk
                        end
                    end
                end
                local pv, dv = {}, {}
                for id, _ in pairs(plain_ids) do pv[#pv + 1] = id end
                for id, _ in pairs(dark_ids) do dv[#dv + 1] = id end
                table.sort(pv)
                table.sort(dv)
                OUT[#OUT + 1] = { name = "maze.floor_plain", vals = pv, spec = "33033", grade = "C", tol = "exact",
                    free = "ids of the 210-row level 0 loc_set groups on ticks that are not a proc, at ticks " .. table.concat(plain_ticks, ",") .. " (room entry, then each re-activation + 1)" }
                OUT[#OUT + 1] = { name = "maze.floor_dark", vals = dv, spec = "33034", grade = "D", tol = "exact",
                    free = "ids of the 210-row level 0 loc_set groups on the proc ticks " .. table.concat(dark_ticks, ",") }
                -- the arena during a maze: one lit tile, the runner's current one
                local lit_ids, max_lit, steps, lit_ticks = {}, 0, 0, {}
                for k, mz in ipairs(maze) do
                    for tk = mz.proc + 1, mz.react do
                        if cnt0[tk] ~= nil and cnt0[tk] ~= 210 then
                            local lit = 0
                            for id, n in pairs(ids0[tk]) do
                                if not dark_ids[tostring(id)] then
                                    lit = lit + n
                                    lit_ids[tostring(id)] = true
                                end
                            end
                            if lit > 0 then
                                steps = steps + 1
                                if lit > max_lit then max_lit = lit end
                                if #lit_ticks < 8 then lit_ticks[#lit_ticks + 1] = tk end
                            end
                        end
                    end
                end
                local lv = {}
                for id, _ in pairs(lit_ids) do lv[#lv + 1] = id end
                table.sort(lv)
                OUT[#OUT + 1] = { name = "maze.arena_progress", vals = lv, spec = "33035", grade = "C", tol = "exact", bad = (max_lit ~= 1 or steps < 10),
                    free = "ids on level 0 of the loc_set rows that are not the dark floor id, between each proc and re-activation: " .. steps .. " lit-tile ticks, at most " .. max_lit .. " lit tile per tick (first ticks " .. table.concat(lit_ticks, ",") .. ")" }
            end
            OUT[#OUT + 1] = { name = "maze.exit_portal", vals = EV.portal_ids, spec = "33037", grade = "C", tol = "exact",
                free = "id of the loc t.world.loc_near found standing at the path's end in each of " .. #maze .. " realms" }
            OUT[#OUT + 1] = { name = "walk_idle.seq", vals = { tostring(boss_ready_anim), tostring(boss_walk_anim) }, spec = "8137,8136", grade = "A", tol = "exact", bad = (boss_pose_kind ~= "ready" or boss_pose_anim ~= boss_ready_anim),
                free = "ready_anim then walk_anim of the boss's npc row before the barrier; pose_kind " .. tostring(boss_pose_kind) .. " drew pose_anim " .. tostring(boss_pose_anim) }
            OUT[#OUT + 1] = { name = "death_ball.proj_anim", vals = { tostring(dball_proj_seq) }, spec = "8144", grade = "A", tol = "exact",
                free = "seq of the first long-flight projectile row (spotanim " .. tostring(DEATH_BALL) .. ") once bound, read from t.world.projectiles" }
            OUT[#OUT + 1] = { name = "tornado.seq", vals = { tostring(tor_ready_anim), tostring(tor_walk_anim) }, spec = "8100", grade = "A", tol = "exact",
                free = "ready_anim then walk_anim of the first tornado's npc row (pose_kind " .. tostring(tor_pose_kind) .. ")" }
            OUT[#OUT + 1] = { name = "tornado.npc", vals = EV.tor_types, spec = "8389,10866,10869", grade = "A", tol = "exact",
                free = "type of each npc_spawn row on level 3 between a proc and its re-activation (" .. #EV.tor_types .. " spawns; the Entry record is the second of the three)" }
            do
                local mraw = death_msg_raw
                local mtxt = mraw and string.gsub(mraw, "<[^>]*>", "") or "none"
                local mcol = mraw and string.match(mraw, "<col=(%x+)>") or "none"
                OUT[#OUT + 1] = { name = "death_ball.message", text = true, vals = { mtxt }, spec = "A large ball of energy is shot your way...", grade = "A", tol = "exact",
                    free = "the line t.msg.last returned on the tick his first death ball launched, colour tag " .. tostring(mcol) .. " (raw: " .. tostring(mraw) .. ")" }
            end
            do
                local mv = {}
                local mt = {}
                for _, r in ipairs(AV.mus) do
                    if r.source == "script" and r.tick < mark_tick - 2 then
                        mv[#mv + 1] = tostring(r.track)
                        mt[#mt + 1] = r.tick
                    end
                end
                OUT[#OUT + 1] = { name = "music.room", vals = mv, spec = "584", grade = "D", tol = "exact",
                    free = "script music rows before the barrier click (landing and walk to the barrier), at ticks " .. table.concat(mt, ",") }
            end
            for _, o in ipairs(OUT) do
                local ok = #o.vals > 0 and not o.bad
                if o.spec == "0" then ok = (#o.vals == 0) and not o.bad end
                local specset = {}
                if o.text then
                    specset[o.spec] = true
                else
                    for v in string.gmatch(o.spec, "[^,]+") do specset[v] = true end
                end
                local tolN = tonumber(string.match(o.tol, "%+%-(%d+)"))
                for _, v in ipairs(o.vals) do
                    if tolN ~= nil then
                        local near = false
                        for sv, _ in pairs(specset) do
                            if tonumber(v) ~= nil and math.abs(tonumber(v) - tonumber(sv)) <= tolN then near = true end
                        end
                        if not near then ok = false end
                    elseif not specset[v] then
                        ok = false
                    end
                end
                local unit = o.text and "text" or "count"
                local measured = #o.vals > 0 and (o.text and o.vals[1] or table.concat(o.vals, ",")) or "0"
                if o.text then
                    t.expect("spec.sotetseg.av." .. o.name, ok and "ok" or "refused", "measured " .. measured .. "; " .. o.free .. " (spec " .. o.spec .. " text, grade " .. o.grade .. ", tol " .. o.tol .. ")")
                else
                    t.expect("spec.sotetseg.av." .. o.name, ok and "ok" or "refused", "measured " .. measured .. " " .. unit .. ", " .. o.free .. " (spec " .. o.spec .. " " .. unit .. ", grade " .. o.grade .. ", tol " .. o.tol .. ")")
                end
            end
        end
        t.finish(0)
    end,
}
