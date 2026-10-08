-- tob_sotetseg_normal: Sotetseg, Normal Mode, a party of THREE (three driven clients in one world).
-- ROLES (named in the sources; the leader holds the world's tick log, so every spec row is read on it):
--   "In a trio encounter, players will stand to the east, west and north-west respectively"
--     (wiki_Theatre_of_Blood_Strategies.wikitext:791); the trio is the guide's "the mage, the melee and the ranger"
--     (transcripts/yt_4i4lv-srJkw.md:91: "The mage should spec on the first and second phase. The melee should spec on the second
--     and third phase. And the ranger should spec on the first and third phase."), the spec being the Dragon warhammer
--     (yt_KF9y2GYTJ-A.md:149 "your Dragon warhammers one means immediately on Entry two is after the first maze and three is after
--     the second maze"; wiki:782 "1 - upon starting the fight, 2 - upon clearing the first maze, 3 - upon clearing the second maze").
--   p1 the leader is the RANGER (east: twisted bow, the maze runner: the content sends the first raider in slot order),
--   p2 the MELEE raider (west, beside him: scythe), p3 the MAGE (north-west: Tumeken's shadow).
--   Every raider prays Protect from Magic from the barrier and switches for a grey ricochet ("pray against these two", yt_4i4lv-srJkw.md:93),
--   eats and brews when low (yt_4i4lv-srJkw.md:93 "eat up if you drop below 45 HP"), and gathers on the death ball's target ("you should
--   all group up at the center tile in front of the boss to share this damage", yt_KF9y2GYTJ-A.md:151; yt_4i4lv-srJkw.md:93).
--   The maze: "One person will be able to see a path through the maze ... Everyone else just needs to follow the path"
--   (yt_4i4lv-srJkw.md:97): the runner is the leader; the others wait where the content throws them ("The remaining players will be
--   forcibly teleported to the other end of the arena", wiki:799) and walk back when he wakes.
-- A party of three fixes him at 750 permille of the five-man pool: 3000 (sotetseg.hp_normal).
local role = (QD_PARTY and QD_PARTY.role) or 1
local kit = {
    -- an empty backpack so the kit below fits (a raider holding no br_ brew is handed eight free doses on entry)
    "::clearinv",
    -- the prayer level for Protect from Magic and Protect from Missiles
    "::setlevel prayer 99",
}
if role == 1 then
    -- the ranger's best ranged set and twisted bow (the content's own ::maxrange), with the levels to wield the warhammer
    kit[#kit + 1] = "::maxrange"
    kit[#kit + 1] = "::setlevel attack 99"
    kit[#kit + 1] = "::setlevel strength 99"
elseif role == 2 then
    -- the melee raider's best melee set and the charged scythe of vitur (::maxmelee)
    kit[#kit + 1] = "::maxmelee"
else
    -- the mage's best magic set and Tumeken's shadow (::maxmage), with the levels to wield the warhammer
    kit[#kit + 1] = "::maxmage"
    kit[#kit + 1] = "::setlevel attack 99"
end
-- the special attack roles' weapon (yt_KF9y2GYTJ-A.md:1023), food, Saradomin brews and prayer restores (yt_4i4lv-srJkw.md:101 "at least six brews, four restores")
kit[#kit + 1] = "::give dragon_warhammer 1"
kit[#kit + 1] = "::give anglerfish 20"
kit[#kit + 1] = "::give br_4dosepotionofsaradomin 3"
kit[#kit + 1] = "::give br_4dose2restore 3"
if role == 1 then
    -- the ranger's potion (the tob_verzik.lua P2 recipe)
    kit[#kit + 1] = "::give br_4doserangerspotion 1"
end

return {
    id = "tob_sotetseg_normal",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 200000,
    setup = kit,

    run = function(t)
        local role = t.party.role()
        local BOSS, BOSS_IDLE = "tob_sotetseg_combat", "tob_sotetseg_noncombat"
        local MELEE, BALL, PORTAL = 8138, 8139, 8142
        local DEATH_BALL, PLAIN_BALL, GREY_BALL = 1604, 1606, 1607
        local MAIN = { "twisted_bow", "scythe_of_vitur", "tumekens_shadow" }
        -- the sources' special attack order: ranger 1 and 3, melee 2 and 3, mage 1 and 2
        local SPEC_PHASES = { { [1] = true, [3] = true }, { [2] = true, [3] = true }, { [1] = true, [2] = true } }
        local FOOD = "anglerfish"
        local boss_world_slot, pool, def_base, att_now, wslot, bx, bz, mark_tick, mark_serial, room_start
        local boss_ready_anim, boss_walk_anim, boss_pose_kind, boss_pose_anim
        ------------------------------------------------------------------
        -- 1. the room: landing, the log, his record
        ------------------------------------------------------------------
        if role == 1 then
            t.check("spec.scope", true, "mode=normal party=3")
            -- the log is on before the landing: the room-entry rows (the arena floor, the room's music) are part of what the room shows
            local lr_pre, lg_pre = t.ticklog.start()
            t.check("ticklog", lr_pre == "ok", tostring(lg_pre))
        end
        local er, ed = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        t.check("enter", er == "ok", "p" .. role .. " " .. tostring(ed))
        if role == 1 then
            local sr, st = t.raid.state()
            t.check("state", sr == "ok" and st.mode == "normal" and st.started == false, tostring(st and st.line))
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
            def_base = tonumber(string.match(bline, "def=%d+ of (%d+)"))
            att_now = tonumber(string.match(bline, "att=(%d+) of"))
            t.check("boss.record", pool ~= nil and def_base ~= nil, bline)
        end
        local nr, nrow = t.npc.by_symbol(BOSS)
        t.check("boss.row", nr == "ok", "p" .. role .. " client slot " .. tostring(nrow and nrow.slot) .. " at " .. tostring(nrow and nrow.x) .. "," .. tostring(nrow and nrow.z))
        bx, bz = nrow.x, nrow.z
        if role == 1 then
            local wr
            wr, wslot = t.ticklog.slot(nrow)
            t.check("boss.slot", wr == "ok", "world slot " .. tostring(wslot))
            local _, bst0 = t.npc.state(BOSS)
            boss_ready_anim, boss_walk_anim = bst0 and bst0.ready_anim, bst0 and bst0.walk_anim
            boss_pose_kind, boss_pose_anim = bst0 and bst0.pose_kind, bst0 and bst0.pose_anim
            local floor_r0, floor_d0 = t.world.loc_near("tob_sotetseg_plaintile", 40)
            t.check("arena.floor_at_entry", floor_r0 == "ok", "the arena floor before the barrier: " .. tostring(floor_r0) .. " loc " .. tostring(floor_d0 and floor_d0.id))
        end
        -- Protect from Magic from the barrier ("to start the room pray magic", yt_KF9y2GYTJ-A.md:151)
        local pmr, pmd = t.prayer.set("protectfrommagic", true)
        t.check("prayer.magic", pmr == "ok", "p" .. role .. " " .. tostring(pmd))
        -- the offensive prayer beside the protection: Rigour for the ranger, Piety for the melee raider, Augury for the mage (the strategy videos pray these in every room)
        local off_name = ({ "rigour", "piety", "augury" })[role]
        local offr, offd = t.prayer.set(off_name, true)
        t.check("prayer.offence", offr == "ok", "p" .. role .. " " .. off_name .. " " .. tostring(offd))
        if role == 1 then
            -- the bow on rapid (a shot every four ticks) and the ranger's potion, as tob_maiden_normal.lua does
            t.ui.tab("combat")
            t.ticks(2)
            local sw_result, sw = t.ui.widget("combat_interface:style_slot_1")
            if sw_result == "ok" then t.ui.invoke(sw, 1) end
            t.ticks(1)
            local sv_result, sv = t.var.varp("varp43_com_mode")
            t.check("style.rapid", sw_result == "ok" and sv == 1, "p1 combat tab style slot 1 pressed for the bow: widget " .. tostring(sw_result) .. ", style " .. tostring(sv))
            local dr, dd = t.player.drink("br_4doserangerspotion")
            t.check("potion.rangers", dr == "ok", "p1 " .. tostring(dd))
        end
        t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
        ------------------------------------------------------------------
        -- 2. the barrier by the player's own click: the leader crosses and starts the room, the members step over after
        ------------------------------------------------------------------
        local eat_ticks, sharks_eaten = {}, 0
        if role == 1 then
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
        t.expect("party.barrier.started", t.party.barrier("started", 400))
        if role ~= 1 then
            -- the leader crossed and the fight is running: for a member the barrier is a gate that steps it over
            local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.cross", xr == "ok", "p" .. role .. " " .. tostring(xd))
        end
        ------------------------------------------------------------------
        -- 3. the fight: every raider runs the same loop, branched on its role
        ------------------------------------------------------------------
        -- stations in tiles from his footprint (south-west corner bx,bz, 5x5): the trio stands east, west and north-west (wiki:791)
        local STATION = { { bx + 8, bz - 6 }, { bx + 2, bz - 1 }, { bx - 4, bz - 6 } }
        local home_x, home_z = STATION[role][1], STATION[role][2]
        local _, at0 = t.world.tile()
        t.check("station", true, "p" .. role .. " crossed at " .. at0.x .. "," .. at0.z .. " and walks to the station " .. home_x .. "," .. home_z .. " (his footprint at " .. bx .. "," .. bz .. ")")
        local mazes_done, phase = 0, 1
        local spec_done = {}
        local boss_seen, gone_for = false, 0
        local pass, last_eat_pass, last_attack_pass = 0, -9, -9
        local gather_x, gather_z = nil, nil
        local death_tick = nil
        local maze = {}
        local cursor_retype = mark_serial
        local def_reads, def_last = {}, nil
        local notes = { eats = 0, brews = 0, restores = 0, gathers = 0, swaps = 0, switch_log = {}, grey_seen = 0, hp_trace = {}, spec_log = {}, pass_ticks = {}, pray_trace = {} }
        local detail_fight = "not finished"
        while pass < 900 do
            pass = pass + 1
            local skip_wait = false
            if role == 1 then notes.pass_ticks[#notes.pass_ticks + 1] = select(2, t.tick()) end
            local cr1, crow1 = t.npc.nearest(BOSS, 70)
            local ir1 = t.npc.nearest(BOSS_IDLE, 70)
            if cr1 == "ok" or ir1 == "ok" then
                boss_seen, gone_for = true, 0
            else
                gone_for = gone_for + 1
            end
            if boss_seen and gone_for >= 3 then
                detail_fight = "his bodies are gone after " .. pass .. " passes"
                break
            end
            if ir1 == "ok" then
                ------------------------------------------------------------------
                -- the maze: the runner (the leader) walks it, the others wait where they were thrown
                ------------------------------------------------------------------
                if role == 1 then
                    local _, rt = t.ticklog.rows({ kind = "npc_retype", since = cursor_retype, slot = wslot })
                    local proc = rt[1]
                    if proc == nil then
                        t.blocked("maze " .. (mazes_done + 1) .. ": his retype row is not in the log although the idle form stands")
                        return
                    end
                    cursor_retype = proc.serial
                    local mi = mazes_done + 1
                    local P = proc.tick
                    local mz = { n = mi, proc = P, serial = proc.serial, path = {}, path_n = 0, def_pre = def_last }
                    maze[mi] = mz
                    local _, bhp0 = t.skill.read("hitpoints")
                    t.check("maze" .. mi .. ".proc", true, "retype row on his slot at tick " .. P .. "; hitpoints " .. tostring(bhp0.level) .. ", food " .. tostring(select(2, t.inv.count(FOOD))))
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
                    if lv ~= 3 then
                        t.blocked("content_bug: maze " .. mi .. " did not send the leader (slot order first) into the realm: it stands on level " .. tostring(lv))
                        return
                    end
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
                    local _, ppm = t.skill.read("prayer")
                    if type(ppm) == "table" and ppm.level ~= nil and ppm.level < 70 then
                        t.player.drink({ "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" })
                        notes.restores = notes.restores + 1
                    end
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
                            t.player.inv_op(FOOD, 1)
                            t.ticks(3)
                        end
                        -- maze 1, row 2: one deliberate wrong tile for four ticks (rag)
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
                                -- full hitpoints first (so the splats of the stay are taken at well separated hitpoints), then two ticks so the client's reading is current
                                local _, hfs = t.skill.read("hitpoints")
                                if hfs.level < 80 then
                                    t.player.inv_op(FOOD, 1)
                                    t.ticks(3)
                                end
                                t.ticks(2)
                                local _, hpB = t.skill.read("hitpoints")
                                local tkB = select(2, t.tick())
                                local xr, xd = t.player.step_tick(wx, wz)
                                t.ticks(4)
                                local br2, bd2 = t.player.step_tick(cx, cz)
                                mz.rag = { wx = wx, wz = wz, hpB = hpB.level, tkB = tkB, out = xr, back = br2 }
                                t.check("maze" .. mi .. ".rag_step", xr == "ok" and br2 == "ok",
                                    "wrong tile " .. wx .. "," .. wz .. " off the path at row 2: " .. tostring(xd) .. "; back: " .. tostring(bd2))
                                local _, hfe = t.skill.read("hitpoints")
                                if hfe.level < 60 then
                                    t.player.inv_op(FOOD, 1)
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
                            if steps == 1 then
                                mz.first_move = tonumber(string.match(tostring(rd), "resolved at tick (%d+)"))
                            end
                        end
                        local _, lvq = t.world.level()
                        if lvq ~= 3 then break end
                    end
                    mz.end_x, mz.end_z, mz.walked = cx, cz, steps
                    t.check("maze" .. mi .. ".walked", cz - sz >= 13,
                        steps .. " steps along the lit path from " .. sx .. "," .. sz .. " to " .. cx .. "," .. cz .. ", row " .. (cz - sz + 1) .. " of " .. mz.grid_h .. "; first step resolved at tick " .. tostring(mz.first_move))
                    -- the portal at the path's north end, read from the world while standing in the realm
                    local por, pord = t.world.loc_near("tob_sotetseg_darkrealm_exit", 40)
                    if por == "ok" then
                        mz.portal_id = pord.id
                    end
                    t.check("maze" .. mi .. ".portal_loc", por == "ok", "world read at the path's end: " .. tostring(por) .. " " .. tostring(pord and pord.id))
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
                    -- his hitpoints and Defence after the maze, read from the content's own readout
                    t.cheat("::tobboss")
                    t.ticks(2)
                    local _, hm = t.msg.last(4)
                    for _, m in ipairs(hm) do
                        local v = string.match(m.text, "tobboss record=%a+ room_mode=%d+ hp=(%d+)")
                        if v then
                            mz.hp_after = tonumber(v)
                            mz.def_after = tonumber(string.match(m.text, "def=(%d+) of"))
                            break
                        end
                    end
                    local _, hn2 = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                    local dealt = 0
                    for _, h in ipairs(hn2) do
                        if h.tick <= P then dealt = dealt + h.damage end
                    end
                    mz.expected = pool - dealt
                    t.check("maze" .. mi .. ".hp_kept", mz.hp_after ~= nil and math.abs(mz.hp_after - (pool - dealt)) <= 60,
                        "boss hitpoints read " .. tostring(mz.hp_after) .. " after the maze, the pool " .. pool .. " less " .. dealt .. " dealt by the proc tick " .. P .. " is " .. (pool - dealt) .. " (the raiders keep hitting until the proc, so a gap of the splats since is expected)")
                else
                    local waited = 0
                    while waited < 120 and t.npc.nearest(BOSS_IDLE, 70) == "ok" do
                        -- nothing of his lands in a maze: eat back to full before he wakes
                        local _, hpw = t.skill.read("hitpoints")
                        local _, nfw = t.inv.count(FOOD)
                        local _, ppw = t.skill.read("prayer")
                        if hpw.level < 85 and nfw and nfw > 0 and waited % 3 == 2 then
                            t.player.eat(FOOD)
                        elseif type(ppw) == "table" and ppw.level ~= nil and ppw.level < 70 and waited % 3 == 0 then
                            -- the offensive prayer and the protection drain a point every two ticks: restore before he wakes
                            t.player.drink({ "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" })
                            notes.restores = notes.restores + 1
                        else
                            t.ticks(1)
                        end
                        waited = waited + 1
                    end
                    t.check("maze" .. (mazes_done + 1) .. ".waited", waited < 120, "p" .. role .. " waited " .. waited .. " ticks for him to wake")
                end
                mazes_done = mazes_done + 1
                phase = math.min(mazes_done + 1, 3)
            elseif cr1 == "ok" then
                local _, me = t.world.tile()
                local acted = false
                ------------------------------------------------------------------
                -- the ricochets first (they are in the air two ticks): a grey one aimed at this tile wants Protect from Missiles, a red one
                -- (and every ball) Protect from Magic ("Put on your prayer earlier than you think", yt_4i4lv-srJkw.md:93)
                ------------------------------------------------------------------
                local _, pj = t.world.projectiles(40)
                local grey_at_me, red_at_me, dball, hot = false, false, nil, false
                for _, r in ipairs(pj or {}) do
                    if r.dst_x == me.x and r.dst_z == me.z then
                        if r.spotanim_id == GREY_BALL then grey_at_me = true end
                        if r.spotanim_id == PLAIN_BALL then red_at_me = true end
                    end
                    if r.spotanim_id == DEATH_BALL then dball = r end
                    -- a ball about to land (the primary) throws two ricochets the tick it lands: hold the chores and watch
                    if (r.spotanim_id == PLAIN_BALL or r.spotanim_id == GREY_BALL) and r.cycles_left ~= nil and r.cycles_left <= 100 then hot = true end
                end
                local _, _, pset = t.prayer.read()
                local on_magic = pset ~= nil and pset.protectfrommagic == true
                local on_missiles = pset ~= nil and pset.protectfrommissiles == true
                if role == 1 and pass <= 90 then
                    local flags = ""
                    if on_magic then flags = flags .. "M" end
                    if on_missiles then flags = flags .. "G" end
                    if grey_at_me then flags = flags .. "g" end
                    if red_at_me then flags = flags .. "r" end
                    notes.pray_trace[#notes.pray_trace + 1] = select(2, t.tick()) .. flags
                end
                -- two threats at once (a grey ricochet and a red ball): answer the one that lands first, then the other
                local want, first_c = "protectfrommagic", nil
                for _, r in ipairs(pj or {}) do
                    if r.dst_x == me.x and r.dst_z == me.z and (r.spotanim_id == GREY_BALL or r.spotanim_id == PLAIN_BALL) and r.cycles_left ~= nil then
                        if first_c == nil or r.cycles_left < first_c or (r.cycles_left == first_c and r.spotanim_id == PLAIN_BALL) then
                            first_c = r.cycles_left
                            want = (r.spotanim_id == GREY_BALL) and "protectfrommissiles" or "protectfrommagic"
                        end
                    end
                end
                -- the press follows the prayer actually lit (an unprayed ball switches both protections off for five ticks)
                if (want == "protectfrommissiles" and not on_missiles) or (want == "protectfrommagic" and not on_magic) then
                    local qr = t.prayer.set(want, true)
                    if #notes.switch_log < 40 then notes.switch_log[#notes.switch_log + 1] = pass .. ":" .. want:sub(12, 14) .. ":" .. tostring(qr):sub(1, 3) end
                    -- a refused press (the five-tick lock) costs no tick: the pass then waits one
                    acted = (qr == "ok")
                end
                if grey_at_me then notes.grey_seen = notes.grey_seen + 1 end
                ------------------------------------------------------------------
                -- chores in the quiet window: food, restores, the special attack, the attack press
                ------------------------------------------------------------------
                local _, hpr = t.skill.read("hitpoints")
                local hp = hpr.level
                notes.hp_trace[#notes.hp_trace + 1] = hp
                if not acted then
                    if (hp < 75 or (dball ~= nil and hp < 95)) and pass - last_eat_pass >= 2 then
                        local _, nfood = t.inv.count(FOOD)
                        local _, nbrew = t.inv.count("br_4dosepotionofsaradomin")
                        if hp < 45 and nbrew and nbrew > 0 then
                            t.player.drink({ "br_4dosepotionofsaradomin", "br_3dosepotionofsaradomin", "br_2dosepotionofsaradomin", "br_1dosepotionofsaradomin" })
                            notes.brews = notes.brews + 1
                        elseif nfood and nfood > 0 then
                            t.player.eat(FOOD)
                            notes.eats = notes.eats + 1
                        end
                        eat_ticks[#eat_ticks + 1] = pass
                        last_eat_pass = pass
                        acted = true
                    elseif pass % 3 == 0 then
                        local _, pp = t.skill.read("prayer")
                        if type(pp) == "table" and pp.level ~= nil and pp.level < 40 then
                            t.player.drink({ "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" })
                            notes.restores = notes.restores + 1
                            acted = true
                        end
                    end
                end
                if not acted and not hot and not grey_at_me and role == 1 and pass % 12 == 6 then
                    -- his Defence from the content's own readout (the spec rows' witness for the floor and the restore)
                    t.cheat("::tobboss")
                    local _, dm = t.msg.last(3)
                    for _, m in ipairs(dm) do
                        local v = string.match(m.text, "tobboss record=.* def=(%d+) of")
                        if v then
                            def_last = tonumber(v)
                            def_reads[#def_reads + 1] = { pass, def_last }
                            break
                        end
                    end
                end
                -- the walk to the station is taken one tick at a time: a blocking walk would miss the ricochet that decides the next five ticks
                if not acted and dball == nil and gather_x == nil and math.max(math.abs(me.x - home_x), math.abs(me.z - home_z)) > 1 then
                    t.player.walk_to(home_x, home_z, 1)
                    acted = true
                end
                ------------------------------------------------------------------
                -- the death ball: everybody stands on its target's tile (3x3 share)
                ------------------------------------------------------------------
                if dball ~= nil then
                    if gather_x ~= dball.dst_x or gather_z ~= dball.dst_z then
                        gather_x, gather_z = dball.dst_x, dball.dst_z
                        notes.gathers = notes.gathers + 1
                    end
                    if not acted and math.max(math.abs(me.x - gather_x), math.abs(me.z - gather_z)) > 0 then
                        t.player.walk_to(gather_x, gather_z, 1)
                        acted = true
                    end
                elseif gather_x ~= nil then
                    gather_x, gather_z = nil, nil
                    last_attack_pass = -9
                end
                ------------------------------------------------------------------
                -- the special attack of this phase (the warhammer), then the main weapon again
                ------------------------------------------------------------------
                if not acted and not hot and dball == nil and not grey_at_me and SPEC_PHASES[role][phase] and not spec_done[phase] and pass >= 3 and hp >= 80 then
                    spec_done[phase] = true
                    t.player.equip("dragon_warhammer", { quick = true })
                    local wr2, wid2 = t.ui.widget("orbs:specbutton")
                    local _, en = t.var.varp("varp300_sa_energy")
                    local ir2 = "no_widget"
                    if wr2 == "ok" and type(en) == "number" and en >= 500 then ir2 = t.ui.invoke(wid2, 1) end
                    t.ticks(1)
                    t.player.attack(BOSS, 2, 20)
                    t.ticks(2)
                    t.player.equip(MAIN[role], { quick = true })
                    -- the warhammer is a melee weapon: the ranger and the mage walk back to their station at once (nobody but the melee raider stands beside him)
                    if role ~= 2 then t.player.walk_to(home_x, home_z, 8) end
                    notes.swaps = notes.swaps + 1
                    notes.spec_log[#notes.spec_log + 1] = "phase " .. phase .. " energy " .. tostring(en) .. " button " .. tostring(wr2) .. " pressed " .. tostring(ir2)
                    last_attack_pass = -9
                    acted = true
                end
                if not acted and not hot and pass - last_attack_pass >= 8 then
                    t.player.attack(BOSS, 2, 1)
                    last_attack_pass = pass
                    acted = true
                end
                if pass % 50 == 0 then
                    t.expect("fight.trace" .. pass, "ok", "p" .. role .. " pass " .. pass .. " hp " .. hp .. " at " .. me.x .. "," .. me.z .. "; prayer switches " .. table.concat(notes.switch_log, " ") .. "; greys aimed at me seen on " .. notes.grey_seen .. " passes; eats " .. notes.eats .. " brews " .. notes.brews .. (role == 1 and ("; prayer trace " .. table.concat(notes.pray_trace, ",")) or ""))
                end
                skip_wait = acted
            end
            if not skip_wait then t.ticks(1) end
        end
        t.check("fight.over", true, "p" .. role .. " " .. detail_fight .. "; eats " .. notes.eats .. ", brews " .. notes.brews .. ", restores " .. notes.restores .. ", gathers " .. notes.gathers .. ", mazes " .. mazes_done .. "; specs " .. table.concat(notes.spec_log, " | "))
        t.expect("party.barrier.dead", t.party.barrier("dead", 900))
        t.finish(0)
    end,
}
