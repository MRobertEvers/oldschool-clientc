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
-- raid seam33 play_tob_sotetseg_normal: with `--party 3` (QD_PARTY) the
-- harness plays NORMAL with three raiders (`seed_survey.py _play_sotetseg
-- --party 3`, `party_repeat.py _play_sotetseg --runs 3`): the trio's kit and
-- run are `trio_kit` / `trio_run` below; solo, everything is as before.
local role = (QD_PARTY and QD_PARTY.role) or 1
local size = (QD_PARTY and QD_PARTY.size) or 1
local trio_kit, trio_run = nil, nil
-- The trio's kit: ::tobkit (seam52 melee_damage_per_swing: ::maxmelee with
-- the recorded raiders' radiant oathplate body and legs and the amulet of
-- rancour -- Blert equipmentDeltas, build/blert/sotetseg, oathplate on 81 of
-- 87 -- cheat_max_gear.rs2; the scythe of vitur; "it is best to attack
-- Sotetseg with Melee", Entry page :191, and the trio guide's melee set),
-- anglerfish ("make sure that you eat your angler", yt_KF9y2GYTJ-A.md:114),
-- restores and brews ("eat up if you drop below 45 HP ... brew up",
-- yt_4i4lv-srJkw.md:93-95), a super combat; Agility 99 for the run energy
-- the maze and the death-ball gathers spend (the Bloat trio's finding,
-- PLAY_NOTES "Bloat, Normal trio").
trio_kit = {
    "::clearinv", "::tobkit", "::setlevel prayer 99", "::setlevel agility 99",
    "::give anglerfish 16", "::give br_4dose2restore 3", "::give 4dose2combat 1",
    "::give br_4dosepotionofsaradomin 4",
    -- raid seam42: the elder maul the Blert trios spec once a phase
    -- (sotetseg_normal_3.json weapons ELDER_MAUL; the plan's THE ELDER MAUL)
    "::give elder_maul 1",
    -- raid seam55: the twisted bow the Blert trios open the room with, one
    -- arrow each on the walk in (sotetseg_normal_3.json weapons, start:
    -- TWISTED_BOW in 14, 12 and 13 of 19 rooms, count 1; the plan's THE BOW
    -- OPENER), its dragon arrows (cheat_max_gear.rs2 ::maxrange's quiver)
    -- and the Ranged it is wielded with
    "::setlevel ranged 99", "::give twisted_bow 1", "::give dragon_arrow 100",
    -- owner_rooms4: a stamina dose (the owner's "are all players running?"; the
    -- plan's run keep drinks it only if run goes off)
    "::give 4dosestamina 1",
}

trio_run = function(t)
    local mode = "normal"
    local BOSS = "tob_sotetseg_combat"
    local MELEE, BALL = 8138, 8139
    local DEATH_BALL, RED, GREY = 1604, 1606, 1607
    t.check("spec.scope", true, "mode=" .. mode .. " party=" .. size .. " p" .. role)
    if role == 1 then t.ticklog.start() end
    local er, ed = t.raid.enter("tob", "sotetseg", { mode = mode })
    t.check("enter", er == "ok", "p" .. role .. " " .. tostring(ed))
    local fight = nil
    if role == 1 then
        local sr, rs = t.raid.state()
        t.check("state", sr == "ok" and rs.room == "sotetseg" and rs.mode == mode and rs.started == false,
            type(rs) == "table" and tostring(rs.line) or tostring(rs))
        local fr, ftile, ftext = t.raid.start_tile()
        t.check("start_tile", fr == "ok", tostring(ftext))
        fight = ftile
    end
    local pr0 = t.player.inv_op("4dose2combat", 1, { quick = true })
    t.ticks(1)
    local ar2, att2 = t.skill.read("attack")
    t.check("play.potion", ar2 == "ok" and att2.level > 99, "p" .. role .. " super combat before the barrier: attack " .. tostring(att2 and att2.level) .. " (" .. tostring(pr0) .. ")")
    -- raid seam55: the arrows into the quiver (the bow opener's), and the
    -- scythe's style by its NAME (ui.style; slot 1 "Chop" is stab)
    local qr, qd = t.player.equip("dragon_arrow")
    t.check("kit.arrows", qr == "ok", "p" .. role .. " " .. tostring(qd))
    local yr, yd = t.ui.style("Reap")
    t.check("kit.style", yr == "ok", "p" .. role .. " " .. tostring(yd))
    -- "to start the room pray magic and piety" (yt_KF9y2GYTJ-A.md:151)
    t.exec("prayer.magic", t.prayer.set, "protectfrommagic", true)
    if role == 1 then t.player.walk_to(fight.x, fight.z - 2, 20) end
    t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
    if role == 1 then
        local cr, cd = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", cr == "ok", tostring(cd))
        local pr, pd = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.begin", pr == "ok", tostring(pd))
        t.ticklog.mark("room start")
        t.expect("party.barrier.started", t.party.barrier("started", 900))
    else
        t.expect("party.barrier.started", t.party.barrier("started", 900))
        local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.cross", xr == "ok", "p" .. role .. " " .. tostring(xd))
    end

    -- THE FIGHT: the library and the room's plan, nothing else
    local result, detail, rec = t.raid.play("tob_sotetseg", { mode = mode, weapon = "scythe_of_vitur", max_ticks = 2400 })
    local S = rec.sote or { mazes = {}, follows = {} }
    t.check("note.run", true, "p" .. role .. " run: varp173 read 0 on " .. tostring(rec.run_offs or 0) .. " ticks, orb presses "
        .. tostring(rec.run_presses or 0) .. ", stamina doses " .. tostring(rec.staminas or 0))
    local function maze_text()
        local parts = {}
        for k, mz in ipairs(S.mazes or {}) do
            parts[#parts + 1] = "ran maze " .. k .. ": landed t" .. tostring(mz.land) .. ", path " .. tostring(mz.order and #mz.order)
                .. " tiles, end t" .. tostring(mz.end_at) .. ", " .. tostring(mz.walks) .. " walks, back t" .. tostring(mz.back)
        end
        for k, fw in ipairs(S.follows or {}) do
            parts[#parts + 1] = "read maze " .. k .. ": from t" .. tostring(fw.seen) .. ", " .. #fw.samples .. " glows (" .. fw.gaps
                .. " gaps, " .. tostring(fw.ambiguous or 0) .. " of them two-way), path " .. fw.path_n .. " tiles complete t" .. tostring(fw.complete) .. (fw.bad and " BAD SHAPE" or "")
                .. ", walk t" .. tostring(fw.start_walk) .. ", off north t" .. tostring(fw.off_north) .. ", " .. fw.on_grid
                .. " ticks on the grid (" .. fw.off_path .. " off the path)"
                -- raid seam52: the read side, measured: ticks the read was
                -- not taken while the glow was live, the unsent guesses
                .. ", read skips " .. tostring(fw.read_skips or 0) .. ", reads with two lit " .. tostring(fw.multi_lit or 0) .. ", frame polls " .. tostring(fw.polls or 0) .. ", early runs " .. tostring(fw.early_steps or 0) .. ", direct walks " .. tostring(fw.direct_walks or 0)
                .. ", held at a two-way gap " .. tostring(fw.held or 0) .. " ticks, guessed " .. tostring(fw.guess_walked or 0)
                .. (fw.stayed_off and ", stayed off the grid" or "")
        end
        return #parts > 0 and table.concat(parts, "; ") or "no maze"
    end
    t.check("play.fight", result == "ok", "p" .. role .. " " .. tostring(detail))
    t.check("play.mazes", true, "p" .. role .. " " .. maze_text() .. "; death balls seen " .. tostring(S.death_balls)
        .. ", gathers " .. tostring(S.gathers) .. " (" .. tostring(S.gather_ticks) .. " ticks), seat ticks " .. tostring(S.seat_ticks)
        .. ", seats given up " .. tostring(S.seats_given_up or 0) .. ", ticks a ball flew at me " .. tostring(S.aimed)
            .. ", bow " .. tostring(S.bow_log) .. ", corner shifts " .. tostring(S.seat_shifts or 0)
        .. "; elder maul " .. ((S.em and #S.em.log > 0) and table.concat(S.em.log, ", ") or "none")
        -- raid seam51: his attack clock as this raider read it, and the boosts
        .. "; his attacks seen " .. tostring(S.attacks_seen or 0) .. ", melee prayer on his due tick " .. tostring(S.melee_due or 0)
        .. " ticks, super combat sips " .. tostring(S.reboosts or 0))
    if role ~= 1 then
        t.expect("party.barrier.done", t.party.barrier("done", 9000))
        t.finish(0)
        return
    end

    -- THE TICK LOG (leader): what the room did, per raider (pids as the log counts them)
    t.ticks(1)
    local mark_tick = nil
    local _, mark_rows = t.ticklog.rows({ kind = "mark" })
    for i = 1, #mark_rows do
        if mark_rows[i].label == "room start" then mark_tick = mark_rows[i].tick end
    end
    mark_tick = mark_tick or rec.start_tick
    local wslot = rec.boss_slot
    local death_tick = rec.death_tick
    t.expect("fight.over", (death_tick ~= nil) and "ok" or "fail", "boss npc_death at tick " .. tostring(death_tick)
        .. " (room ticks " .. tostring(death_tick and (death_tick - mark_tick)) .. "), slot " .. tostring(wslot) .. "; " .. tostring(detail))
    local end_tick = death_tick or rec.end_tick or mark_tick
    local _, hits = t.ticklog.rows({ kind = "hit_player" })
    t.ticks(1)
    local _, projs = t.ticklog.rows({ kind = "projectile" })
    local _, rts = t.ticklog.rows({ kind = "npc_retype", slot = wslot })
    t.ticks(1)
    local _, ptl = t.ticklog.rows({ kind = "player_tile" })
    local _, anims = t.ticklog.rows({ kind = "npc_anim", slot = wslot })
    -- damage per raider, and what dealt it
    local taken, melee_n, pids = {}, 0, {}
    for _, h in ipairs(hits) do
        if h.tick > mark_tick and h.tick <= end_tick then
            taken[h.pid] = (taken[h.pid] or 0) + h.damage
            pids[h.pid] = true
        end
    end
    for _, a in ipairs(anims) do
        if a.seq == MELEE and a.tick > mark_tick then melee_n = melee_n + 1 end
    end
    -- the mazes: his two retypes per maze (proc, re-activation); the raider in
    -- the realm (level 3) is the one the room chose
    local mazes = {}
    for i = 1, #rts, 2 do
        mazes[#mazes + 1] = { proc = rts[i].tick, react = rts[i + 1] and rts[i + 1].tick or nil, runners = {}, arena_grid = {}, blasts = 0 }
    end
    for _, r in ipairs(ptl) do
        for _, mz in ipairs(mazes) do
            if r.tick > mz.proc and r.tick <= (mz.react or end_tick) then
                if r.level == 3 then mz.runners[r.pid] = (mz.runners[r.pid] or 0) + 1 end
                local ox, oz = math.floor(r.x / 64) * 64, math.floor(r.z / 64) * 64
                local lx, lz = r.x - ox - 9, r.z - oz - 22
                if r.level == 0 and lx >= 0 and lx < 14 and lz >= 0 and lz < 15 then
                    mz.arena_grid[r.pid] = (mz.arena_grid[r.pid] or 0) + 1
                end
            end
        end
    end
    for _, h in ipairs(hits) do
        for _, mz in ipairs(mazes) do
            if h.tick > mz.proc + 3 and h.tick <= (mz.react or end_tick) and h.damage > 3 then mz.blasts = mz.blasts + 1 end
        end
    end
    local maze_lines, one_runner, no_blast, walked = {}, #mazes >= 1, true, true
    for k, mz in ipairs(mazes) do
        local rs, gs = {}, {}
        local nr = 0
        for pid, c in pairs(mz.runners) do nr = nr + 1 rs[#rs + 1] = "p" .. pid .. " " .. c end
        for pid, c in pairs(mz.arena_grid) do gs[#gs + 1] = "p" .. pid .. " " .. c end
        table.sort(rs) table.sort(gs)
        if nr ~= 1 then one_runner = false end
        if mz.blasts > 0 then no_blast = false end
        if #gs < 1 then walked = false end
        maze_lines[#maze_lines + 1] = "maze " .. k .. ": t" .. mz.proc .. "-t" .. tostring(mz.react) .. " (" .. tostring(mz.react and (mz.react - mz.proc))
            .. " ticks), runner " .. table.concat(rs, ",") .. ", arena grid ticks " .. table.concat(gs, ",") .. ", hits >3 in it " .. mz.blasts
    end
    t.expect("technique.trio.maze_runner_one", one_runner and "ok" or "fail", #mazes .. " mazes; " .. table.concat(maze_lines, "; "))
    t.expect("technique.trio.maze_no_blast", (#mazes >= 1 and no_blast) and "ok" or "fail",
        "no hit over 3 (a wrong tile's blast, 15 + 6.67% hp; the tornado's 35-45) on anyone while a maze was on: " .. table.concat(maze_lines, "; "))
    t.check("technique.trio.maze_followed", #mazes >= 1 and walked, "the raiders thrown to the far end walked the arena's grid behind the glow: " .. table.concat(maze_lines, "; "))
    -- the death balls: each landing's splats, by raider (W:794 split; all three on
    -- the front tile, yt_4i4lv-srJkw.md:95)
    local balls, shared_all, ball_lines = 0, true, {}
    for _, r in ipairs(projs) do
        if r.spotanim == DEATH_BALL and r.tick > mark_tick and r.tick <= end_tick then
            balls = balls + 1
            local land = r.tick + math.floor((r.end_cycle or 0) / 30)
            -- the share is one splat of one size on each raider within a tile
            -- of the target, on one tick (S tob_sote_ball_impact: total /
            -- sharing to each): the largest group of equal splats from him
            -- from the landing to two after (a raider earlier in the tick's
            -- order shows its splat a tick later: _play_sotetseg t206/t207)
            local got, n = {}, 0
            local by = {}
            for _, h in ipairs(hits) do
                if h.tick >= land and h.tick <= land + 2 and h.npc_slot == wslot and h.damage > 0 then
                    by[h.damage] = by[h.damage] or {}
                    by[h.damage][h.pid] = h.damage
                end
            end
            for _, group in pairs(by) do
                local c = 0
                for _ in pairs(group) do c = c + 1 end
                if c > n then got, n = group, c end
            end
            local parts = {}
            for pid, d in pairs(got) do parts[#parts + 1] = "p" .. pid .. " " .. d end
            table.sort(parts)
            -- "incoming attacks made by Sotetseg, including the big red ball,
            -- will not deal damage" while a maze is on (W:799; S
            -- tob_sote_maze_nulls_hit): a ball landing inside one is not judged
            local nulled = false
            for _, mz in ipairs(mazes) do
                if land >= mz.proc and land <= (mz.react or end_tick) then nulled = true end
            end
            if nulled then
                balls = balls - 1
                parts[#parts + 1] = "nulled by the maze"
            elseif land <= end_tick then
                -- the technique: every raider within a tile of one raider's
                -- tile at the landing (S tob_sote_ball_impact hunts radius 1
                -- around the target) -- the splats themselves can be 0 for all
                -- but one (1 + random(121) split three ways, through a prayer)
                local at = {}
                for _, pt in ipairs(ptl) do
                    if pt.tick == land - 1 and pt.level == 0 then at[#at + 1] = pt end
                end
                local stacked = false
                for _, c in ipairs(at) do
                    local all = true
                    for _, o in ipairs(at) do
                        if math.abs(o.x - c.x) > 1 or math.abs(o.z - c.z) > 1 then all = false end
                    end
                    if all then stacked = true end
                end
                stacked = stacked and #at == size
                parts[#parts + 1] = (stacked and "all " .. #at .. " within a tile at t" .. (land - 1)) or ("NOT stacked at t" .. (land - 1))
                if not stacked then shared_all = false end
            end
            ball_lines[#ball_lines + 1] = "t" .. r.tick .. " land t" .. land .. ": " .. n .. " raiders (" .. table.concat(parts, ",") .. ")"
        end
    end
    -- raid seam50: a room whose only death ball landed inside a maze (nulled,
    -- W:799) has nothing to judge: the row is absent on that seed, never a
    -- FAIL on nothing (seam49 s2 _play_sotetseg: t190 land t205, nulled) nor
    -- a PASS on nothing; play.measure carries the line
    if balls >= 1 then
        t.expect("technique.trio.death_ball_shared", shared_all and "ok" or "fail",
            balls .. " death balls, the party stacked within a tile at every landing (the splats: the largest equal group, landing to +2): " .. table.concat(ball_lines, "; "))
    end
    -- the balls: blocked splats (hitsplat 26, 0) against ones that hurt
    local blocked, hurt = 0, 0
    for _, h in ipairs(hits) do
        if h.tick > mark_tick and h.tick <= end_tick then
            if h.hitsplat == 26 and h.damage == 0 and h.npc_slot == -1 then blocked = blocked + 1 end
        end
    end
    -- the room's end: the real chat line (tob_sotetseg.lua :1578-1590's read)
    if death_tick ~= nil then
        t.ticks(10)
        local _, wl = t.msg.last(30)
        local wave_raw = nil
        for _, m in ipairs(wl or {}) do
            if string.find(m.text, "complete!", 1, true) and string.find(m.text, "Wave", 1, true) then wave_raw = m.text end
        end
        local wave_txt = wave_raw and string.gsub(string.gsub(wave_raw, "<br>", " "), "<[^>]*>", "") or "none"
        t.check("room.wave_line", wave_raw ~= nil and string.find(wave_txt, "(Normal Mode) complete!", 1, true) ~= nil,
            "chat line after his death: " .. wave_txt)
    end
    local per = {}
    for pid, _ in pairs(pids) do per[#per + 1] = "p" .. pid .. " " .. (taken[pid] or 0) end
    table.sort(per)
    local hist = { 0, 0, 0, 0 }
    for _, n in pairs(rec.inputs) do
        if n > 0 then hist[math.min(n, 4)] = hist[math.min(n, 4)] + 1 end
    end
    -- raid seam50 play_tob_sotetseg_whole: THE REFERENCE ROWS.  The room
    -- against the 20 recorded death-free Normal trio rooms on Blert
    -- (docs/minigames/theater_of_blood/sources/blert_api/reference/
    -- sotetseg_normal_3.json): outcome.room_ticks 212.5 [164-262];
    -- outcome.hp_lost per raider melee1 92.5 [27-155], melee2 108 [3-225],
    -- melee3 105 [83-135] (the roles are the recorder's labels, so a raider
    -- is held to their union [3-225]); outcome.deaths 0 [0-0]; the maze, proc
    -- to his combat form back, 28 [14-47] over 26 mazes (blert_api/
    -- sote_maze.csv reactivate_tick - proc_tick).
    -- OWNER RULING 2026-10-07 (owner_rooms4, via the coordinator): "If sotetseg
    -- is completing but just a bit slower, count it as good, don't worry about
    -- meeting blert times."  So for SOTETSEG ONLY the room's green is that it
    -- completes (his death row, no raider dead: the deaths row below); the
    -- reference's [164-262] is printed beside it, not asserted.  The other rooms'
    -- time bounds stand.
    local room_ticks = death_tick and (death_tick - mark_tick) or nil
    t.check("blert.room_ticks", room_ticks ~= nil,
        "room " .. tostring(room_ticks) .. " ticks from the mark to his death (completion is the bound, owner 2026-10-07); the reference's 20 rooms: 212.5 [164-262]"
        .. ((room_ticks ~= nil and room_ticks > 262) and " -- slower than every recorded room" or ""))
    local hp_ok = #per >= 1
    for pid, _ in pairs(pids) do
        if (taken[pid] or 0) > 225 then hp_ok = false end
    end
    t.check("blert.hp_lost", hp_ok, "hitpoints lost a raider " .. table.concat(per, ", ") .. "; the reference: melee1 92.5 [27-155], melee2 108 [3-225], melee3 105 [83-135]")
    local maze_ok, maze_d = #mazes >= 1, {}
    for _, mz in ipairs(mazes) do
        local d = mz.react and (mz.react - mz.proc) or nil
        maze_d[#maze_d + 1] = tostring(d)
        if d == nil or d < 14 or d > 47 then maze_ok = false end
    end
    t.check("blert.maze_ticks", maze_ok, "mazes proc to back " .. table.concat(maze_d, ", ") .. " ticks; the reference's 26 mazes: 28 [14-47]")
    t.check("play.measure", true, string.format("room %s ticks (mark %s, death %s); damage taken %s; his melee swings %d; "
        .. "balls blocked %d; death balls %d; mazes %d; leader eats %d, drinks %d, swings %d; inputs 1/2/3/4+ %d/%d/%d/%d",
        tostring(death_tick and (death_tick - mark_tick)), tostring(mark_tick), tostring(death_tick), table.concat(per, ", "),
        melee_n, blocked, balls, #mazes, #rec.eats, #rec.drinks, #rec.swings, hist[1], hist[2], hist[3], hist[4]))
    t.expect("party.barrier.done", t.party.barrier("done", 9000))
    t.finish(0)
end
return {
    id = "_play_sotetseg",
    fixture = "fresh_lumbridge.ini",
    max_frames = (size > 1) and 150000 or 90000,
    -- tob_sotetseg.lua's own bring-alongs, unchanged (solo); the trio's below
    setup = (size > 1) and trio_kit or {
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
        if size > 1 then
            return trio_run(t)
        end
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
