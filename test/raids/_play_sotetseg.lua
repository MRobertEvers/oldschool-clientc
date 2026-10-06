-- _play_sotetseg: Sotetseg played through the PLAY LIBRARY (t.raid.play,
-- script/plugins/quest_driver/raid_play.lua) and the room's plan
-- (raid_play_tob_sotetseg.lua; docs/minigames/raid_loop/PLAY_NOTES.md
-- "Sotetseg").  An underscore harness, not a kept room (raid seam30
-- play_tob_sotetseg): solo Entry with test/raids/tob_sotetseg.lua's own
-- bring-alongs and entry, then ONE call, then the tick log.  The kept room's
-- technique rows (technique.ball_prayer_raised_in_flight,
-- technique.no_melee_at_range, technique.prayed_balls_blocked,
-- technique.off_on_3, technique.tornado_outrun,
-- technique.walked_only_lit_tiles) and room-complete rows (fight.over,
-- room.wave_line, exit.barrier, chest.found, chest.click, chest.bandages) are
-- copied UNCHANGED from tob_sotetseg.lua :181-182, :982, :1042-1043,
-- :1192-1193, :1477-1483, :1587-1613; the reads that feed them are
-- tob_sotetseg.lua's ANALYSIS (:986-1045, :1096-1166, :1266-1382), cut to
-- what they use, with the fight's own record (the lit path the play read, its
-- prayer presses, the step off the grid) from the play.  Not copied:
-- technique.ball_prayer_dropped_in_flight -- the kept room DROPS the prayer
-- under a ball on purpose to measure the disable window; a play never does.
return {
    id = "_play_sotetseg",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    -- tob_sotetseg.lua's own bring-alongs, unchanged
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel ranged 99", "::setlevel magic 99",
        "::give dragon_dart 800", "::fullscythe",
        "::setlevel agility 99", "::give bow_of_faerdhinen 1",
        "::give elder_maul 1",
        "::give bandos_chestplate 1", "::give primordial_boots 1", "::give infernal_cape 1",
        "::give ferocious_gloves 1", "::give ultor_ring 1",
        "::give shark 13",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=entry party=1")
        local BOSS = "tob_sotetseg_combat_story"
        local MELEE, BALL = 8138, 8139            -- attack seqs (ENCOUNTER_TIMING 5.1)
        local PORTAL = 8142                       -- maze proc seq
        local DEATH_BALL, PLAIN_BALL = 1604, 1606 -- projectile spotanims

        -- (1) the Entry room, as tob_sotetseg.lua :43-97 does it (its ::tobboss
        -- readout is a spec read, not part of the entry, and is left out)
        local wslot, bx, bz, mark_tick, mark_serial
        do
            local lr_pre, lg_pre = t.ticklog.start()
            local er, ed = t.raid.enter("tob", "sotetseg", { mode = "entry" })
            t.check("enter", er == "ok", tostring(ed))
            local sr, st = t.raid.state()
            t.check("state", sr == "ok" and st.mode == "entry" and st.started == false, tostring(st and st.line))
            t.check("ticklog", lr_pre == "ok", tostring(lg_pre))
            local nr, nrow = t.npc.by_symbol(BOSS)
            t.check("boss.row", nr == "ok", "client slot " .. tostring(nrow and nrow.slot) .. " at " .. tostring(nrow and nrow.x) .. "," .. tostring(nrow and nrow.z))
            local wr
            wr, wslot = t.ticklog.slot(nrow)
            t.check("boss.slot", wr == "ok", "world slot " .. tostring(wslot))
            bx, bz = nrow.x, nrow.z
            -- the plan's loadout: the scythe and the melee kit the kept room wears
            -- for its scythe phase (tob_sotetseg.lua :309-314, :373), on before the
            -- barrier ("it is best to attack Sotetseg with Melee", Entry page :191)
            t.exec("equip.scythe", t.player.equip, "scythe_of_vitur")
            t.exec("equip.chestplate", t.player.equip, "bandos_chestplate")
            t.exec("equip.boots", t.player.equip, "primordial_boots")
            t.exec("equip.cape", t.player.equip, "infernal_cape")
            t.exec("equip.gloves", t.player.equip, "ferocious_gloves")
            t.exec("equip.ring", t.player.equip, "ultor_ring")
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
        end

        -- (2) THE FIGHT: the library and the room's plan, nothing else
        local result, detail, rec = t.raid.play("tob_sotetseg", { mode = "entry", weapon = "scythe_of_vitur", max_ticks = 1400 })
        t.check("play.fight", result == "ok", tostring(detail))
        local S = rec.sote or { mazes = {}, press_log = {}, read_magic = {} }
        local death_tick = rec.death_tick
        local fguard = (rec.end_tick or rec.start_tick) - rec.start_tick
        local mazes_done = #S.mazes
        local sharks_eaten, potions_drunk, vuln_casts = 0, 0, 0
        for _, e in ipairs(rec.eats) do if e.item == "shark" then sharks_eaten = sharks_eaten + 1 end end
        for _, d in ipairs(rec.drinks) do if string.find(d.item, "restore", 1, true) then potions_drunk = potions_drunk + 1 end end
        t.expect("fight.over", (death_tick ~= nil) and "ok" or "fail", "boss npc_death at tick " .. tostring(death_tick) .. " after " .. fguard .. " loop ticks, " .. mazes_done .. " mazes, " .. sharks_eaten .. " sharks, " .. potions_drunk .. " restores, " .. vuln_casts .. " vulnerability casts")

        -- (3) the tick log (tob_sotetseg.lua :986-1045)
        t.ticks(1)
        local _, anims = t.ticklog.rows({ kind = "npc_anim", slot = wslot })
        local _, projs = t.ticklog.rows({ kind = "projectile" })
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        local _, rtall = t.ticklog.rows({ kind = "npc_retype", slot = wslot })
        local _, ptl = t.ticklog.rows({ kind = "player_tile" })
        local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
        local pos = {}
        for _, r in ipairs(ptl) do pos[r.tick] = r end
        local death_proj, plain = {}, {}
        for _, r in ipairs(projs) do
            if r.spotanim == DEATH_BALL then
                death_proj[r.tick] = r
            elseif r.spotanim == PLAIN_BALL then
                plain[#plain + 1] = r
            end
        end
        local attacks = {}
        for _, a in ipairs(anims) do
            if a.seq == MELEE or a.seq == BALL then
                local kind = "melee"
                if a.seq == BALL then
                    if death_proj[a.tick] ~= nil then kind = "death" else kind = "ball" end
                end
                attacks[#attacks + 1] = { tick = a.tick, kind = kind }
            end
        end
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

        -- every splat from him: death ball, melee, or an unprayed ball (tob_sotetseg.lua :1096-1166)
        local eat_ticks = {}
        for _, e in ipairs(rec.eats) do eat_ticks[#eat_ticks + 1] = e.tick end
        for _, d in ipairs(rec.drinks) do eat_ticks[#eat_ticks + 1] = d.tick end
        local xb_launches = {}
        -- the play has Protect from Magic up from the room start (the kept room's
        -- prayer goes back up after its deliberate drop at `ok_issue`)
        local ok_issue = mark_tick
        local ball_late = nil
        local boss_hits = {}
        for _, h in ipairs(hits) do
            if h.npc_slot == wslot and h.damage > 0 then boss_hits[#boss_hits + 1] = h end
        end
        local melee_used, death_used = {}, {}
        melee_used.on_tick, melee_used.shared = {}, 0
        for _, h in ipairs(boss_hits) do melee_used.on_tick[h.tick] = (melee_used.on_tick[h.tick] or 0) + 1 end
        local melee_dmg, death_hits = {}, {}
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
                melee_used[best] = true
                melee_dmg[#melee_dmg + 1] = h.damage
            elseif best_kind == "death" then
                death_used[best] = true
                death_hits[#death_hits + 1] = h.damage
            else
                ball_dmg[#ball_dmg + 1] = h.damage
                local in_xb = false
                for _, xl in ipairs(xb_launches) do
                    if h.tick >= xl and h.tick <= xl + 10 then in_xb = true end
                end
                if ok_issue ~= nil and h.tick > ok_issue and not in_xb then ball_after = ball_after + 1; ball_late = (ball_late or "") .. " t" .. h.tick .. ":" .. h.damage end
            end
        end
        local blocks = 0
        for _, h in ipairs(hits) do
            if h.hitsplat == 26 and h.damage == 0 and h.npc_slot == -1 and h.tick > mark_tick and pos[h.tick] ~= nil and pos[h.tick].level == 0 then
                blocks = blocks + 1
            end
        end
        t.expect("technique.prayed_balls_blocked", (blocks > 0 and ball_after == 0) and "ok" or "fail",
            blocks .. " ball splats blocked to zero by Protect from Magic, " .. ball_after .. " ball splats that hurt after the prayer was back up at tick " .. tostring(ok_issue) .. (ball_late or ""))

        -- the ball the play saw leave with Protect from Magic OFF (it stood in his
        -- melee range under Protect from Melee) and raised the prayer for in
        -- flight: the kept room's trial 1 (tob_sotetseg.lua :125-180), read
        -- from the play's own record (what it read lit at the throw, the tick
        -- it pressed) and the log (the landing splat)
        local rp_lines, rp_verdicts = {}, {}
        for _, r in ipairs(plain) do
            if r.tick > mark_tick and rp_verdicts[1] == nil and S.read_magic[r.tick] == false then
                local land = r.tick + math.floor(r.end_cycle / 30)
                local switched = nil
                for _, p in ipairs(S.press_log) do
                    if switched == nil and p.name == "protectfrommagic" and p.tick >= r.tick and p.tick < land then switched = p.tick end
                end
                local verdict, hit = "none", nil
                for _, h in ipairs(hits) do
                    if hit == nil and h.tick == land and h.hitsplat == 26 and h.damage == 0 then verdict, hit = "prayed", h end
                    if hit == nil and h.tick == land + 1 and h.npc_slot == wslot and h.damage > 0 then verdict, hit = "unprayed", h end
                end
                if hit and switched ~= nil then
                    rp_verdicts[1] = verdict
                    rp_lines[#rp_lines + 1] = "throw " .. r.tick .. " (prayer false, switched true at tick " .. switched .. "), landing " .. land .. " -> " .. verdict .. " (splat tick " .. hit.tick .. ", damage " .. hit.damage .. ")"
                end
            end
        end
        rp_verdicts[1] = rp_verdicts[1] or "none"
        t.ball_read_text = table.concat(rp_lines, " | ")
        t.check("technique.ball_prayer_raised_in_flight", rp_verdicts[1] == "prayed",
            "Protect from Magic off when the ball left, on in flight: " .. rp_verdicts[1] .. "; " .. t.ball_read_text)

        -- (4) the mazes (tob_sotetseg.lua :1266-1382, cut to the technique rows):
        -- the proc and the re-activation are his retype rows, the lit path is
        -- the one the play read off the realm's floor, the step off the grid is
        -- the first tile north of the path's end in the log
        local maze = {}
        for i = 1, #rtall, 2 do
            local k = #maze + 1
            local pm = S.mazes[k] or {}
            local mz = { proc = rtall[i].tick, react = rtall[i + 1] and rtall[i + 1].tick or nil, path = {}, sz = pm.sz or 0 }
            for key, _ in pairs(pm.path or {}) do
                mz.path[math.floor(key / 100000) .. "," .. (key % 100000)] = true
            end
            if mz.react ~= nil and pm.ez ~= nil then
                for tk = mz.proc, mz.react do
                    local p = pos[tk]
                    if mz.off_tick == nil and p and p.level == 3 and p.z == pm.ez + 1 then mz.off_tick = tk end
                end
            end
            mz.tor_before = 0
            for _, h in ipairs(hits) do
                if h.damage >= 30 and h.tick > mz.proc + 3 and h.tick <= (mz.react or mz.proc + 200) then mz.tor_before = mz.tor_before + 1 end
            end
            maze[k] = mz
        end
        local offs, tor_rows = {}, {}
        local maze_visits_off_path, maze_ticks_on_grid = 0, 0
        for k, mz in ipairs(maze) do
            local P, R = mz.proc, mz.react or mz.proc
            for tk = P, R do
                local p = pos[tk]
                if p and p.level == 3 then
                    maze_ticks_on_grid = maze_ticks_on_grid + 1
                    if mz.path[p.x .. "," .. p.z] == nil and p.z - mz.sz < 15 then maze_visits_off_path = maze_visits_off_path + 1 end
                end
            end
            if mz.off_tick and mz.off_tick % 4 == 3 then offs[#offs + 1] = R - mz.off_tick end
            for _, s in ipairs(spawns) do
                if s.tick > P and s.tick < R and s.type ~= nil then
                    local p = pos[s.tick - 1]
                    if p ~= nil and p.level == 3 and tor_rows[k] == nil and s.level == 3 then
                        tor_rows[k] = { row = (p.z - mz.sz) + 1, slot = s.slot, tick = s.tick }
                    end
                end
            end
        end
        local off_ok = #offs > 0
        for _, v in ipairs(offs) do if v ~= 1 then off_ok = false end end
        t.expect("technique.off_on_3", (off_ok) and "ok" or "fail", #offs .. " mazes left with a step resolving on cycle tick 3; re-activation came " .. table.concat(offs, ",") .. " tick(s) later")
        local ahead = 0
        for _, mz in ipairs(maze) do ahead = ahead + (mz.tor_before or 0) end
        t.expect("technique.tornado_outrun", (tor_rows[1] ~= nil and ahead == 0) and "ok" or "fail",
            "the tornado spawned at tick " .. tostring(tor_rows[1] and tor_rows[1].tick) .. " when the runner stepped onto row 4, and the runner, one tile a tick ahead of it, took " .. ahead .. " tornado splats on the way to the path's end of " .. #maze .. " mazes")
        t.expect("technique.walked_only_lit_tiles", (maze_visits_off_path <= 5 and maze_ticks_on_grid > 20) and "ok" or "fail",
            maze_ticks_on_grid .. " player_tile rows in the shadow realm, " .. maze_visits_off_path .. " of them on a tile the loc_set rows never lit (the deliberate wrong-tile step stood 4 ticks)")

        -- (5) the exit, copied unchanged from tob_sotetseg.lua :1578-1614
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

        -- (6) THE MEASURE (reported against the kept tob_sotetseg run)
        local taken = 0
        for _, h in ipairs(hits) do
            if h.tick > mark_tick and (death_tick == nil or h.tick <= death_tick) then taken = taken + h.damage end
        end
        local hist = { 0, 0, 0, 0 }
        for _, n in pairs(rec.inputs) do
            if n > 0 then hist[math.min(n, 4)] = hist[math.min(n, 4)] + 1 end
        end
        local mz_text = {}
        for k, mz in ipairs(S.mazes) do
            mz_text[#mz_text + 1] = "maze " .. k .. ": landed " .. tostring(mz.land) .. ", path " .. tostring(mz.path_n) .. " tiles (" .. tostring(mz.order and #mz.order) .. " in order), " .. tostring(mz.walks) .. " walks, " .. tostring(mz.waits) .. " waits, off sent " .. tostring(mz.off_sent)
        end
        t.check("play.measure", true, string.format("room %s ticks (mark %s, death %s); damage taken %d (melee %d in %d, unprayed balls %d in %d, death balls %d in %d); food %d, drinks %d; swings %d; balls blocked %d; protect ticks melee %d magic %d; inputs per tick: 1 on %d, 2 on %d, 3 on %d, 4+ on %d; %s",
            tostring(death_tick and (death_tick - mark_tick)), tostring(mark_tick), tostring(death_tick), taken,
            (function() local s = 0 for _, d in ipairs(melee_dmg) do s = s + d end return s end)(), #melee_dmg,
            (function() local s = 0 for _, d in ipairs(ball_dmg) do s = s + d end return s end)(), #ball_dmg,
            (function() local s = 0 for _, d in ipairs(death_hits) do s = s + d end return s end)(), #death_hits,
            #rec.eats, #rec.drinks, #rec.swings, blocks, S.melee_ticks or 0, S.magic_ticks or 0,
            hist[1], hist[2], hist[3], hist[4], table.concat(mz_text, "; ")))
        t.finish(0)
    end,
}
