-- seam22 tob_normal_trio_findings (1): Sotetseg Normal, a party of three, the first maze.
-- The leader is sent into the realm (slot order); the two members land on the arena's far
-- tile (^tob_sote_fight 15,20 = column 6, row -2 of the arena grid) and walk onto the grid:
-- p2 straight north to row 4 (one tile per step), p3 to row 1 three columns west and stands.
-- The leader reads the world's tick log: the arena tornado's npc_spawn (8389), the rag
-- explosions and every hit on the members (pid 1, pid 2) since the landing.
return {
    id = "s22_sote_trio",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 40000,
    setup = {
        "::clearinv",
        "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::give anglerfish 10",
    },
    run = function(t)
        local role = t.party.role()
        if role == 1 then
            local lr, lg = t.ticklog.start()
            t.check("ticklog", lr == "ok", tostring(lg))
        end
        local er, ed = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        t.check("enter", er == "ok", "p" .. role .. " " .. tostring(ed))
        t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
        if role == 1 then
            local fr, fight = t.raid.start_tile()
            t.player.walk_to(fight.x, fight.z - 2, 20)
            local cr, cd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.click", cr == "ok", tostring(cd))
            local pr, pd = t.chat.play({ "options", "choose:Yes, begin the fight." })
            t.check("barrier.begin", pr == "ok", tostring(pd))
        end
        t.expect("party.barrier.started", t.party.barrier("started", 400))
        if role ~= 1 then
            local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.cross", xr == "ok", "p" .. role .. " " .. tostring(xd))
        end
        t.prayer.set("protectfrommagic", true)
        t.expect("party.barrier.crossed", t.party.barrier("crossed", 200))
        local land_tick = nil
        if role == 1 then
            t.cheat("::tobmazearm")
            local g = 0
            while g < 60 do
                t.ticks(1)
                g = g + 1
                local _, wt = t.world.tile()
                if wt and wt.level == 3 then break end
            end
            local _, wt = t.world.tile()
            land_tick = select(2, t.tick())
            t.check("runner.landed", wt and wt.level == 3, "p1 on level " .. tostring(wt and wt.level) .. " at " .. tostring(wt and wt.x) .. "," .. tostring(wt and wt.z) .. " tick " .. tostring(land_tick))
        end
        t.expect("party.barrier.landed", t.party.barrier("landed", 200))
        local _, here = t.world.tile()
        t.check("tile.landed", true, "p" .. role .. " at " .. here.x .. "," .. here.z .. " level " .. here.level .. " tick " .. tostring(select(2, t.tick())))
        if role ~= 1 then
            t.party.allow_death("scratch s22_sote_trio: the arena rules hitting a member are the measurement")
            t.ticks(3)
            local lr, lt = t.world.loc_near("tob_sotetseg_lighttile", 30)
            t.check("p" .. role .. ".mirror", lr == "ok", "the runner's mirrored tile: " .. tostring(lr) .. " " .. tostring(lt and lt.tile_x) .. "," .. tostring(lt and lt.tile_z))
            if lr == "ok" then
                if role == 3 then
                    -- the path's start tile, row 0 (a path tile: no rag); stands there
                    local wr, wd = t.player.walk_to(lt.tile_x, lt.tile_z, 15)
                    t.check("p3.walk_start", wr == "ok", tostring(wd))
                else
                    -- three tiles up the start column: the FOURTH row (zero-based row 3)
                    t.ticks(3)
                    local wr, wd = t.player.walk_to(lt.tile_x, lt.tile_z + 3, 15)
                    t.check("p2.walk_row4", wr == "ok", tostring(wd))
                end
            end
        end
        t.ticks(10)
        local _, fin = t.world.tile()
        t.check("tile.final", true, "p" .. role .. " at " .. fin.x .. "," .. fin.z .. " level " .. fin.level .. " tick " .. tostring(select(2, t.tick())))
        t.expect("party.barrier.walked", t.party.barrier("walked", 200))
        if role == 1 then
            local since = land_tick - 4
            local _, sp = t.ticklog.rows({ kind = "npc_spawn", since = since })
            local torn = {}
            for _, r in ipairs(sp or {}) do
                if r.type == 8389 or r.type == 10866 or r.type == 10869 then
                    torn[#torn + 1] = "type " .. r.type .. " tick " .. r.tick .. " at " .. tostring(r.x) .. "," .. tostring(r.z)
                end
            end
            t.check("arena.tornado_spawn", #torn > 0, #torn .. " tornado spawn(s) since tick " .. since .. ": " .. table.concat(torn, "; "))
            local _, fr2 = t.ticklog.rows({ kind = "npc_free", since = since })
            local frees = {}
            for _, r in ipairs(fr2 or {}) do frees[#frees + 1] = tostring(r.slot) .. "@" .. r.tick end
            t.check("npc_free.list", true, table.concat(frees, " "))
            local _, ms = t.ticklog.rows({ kind = "map_spotanim", since = since })
            local mrow = {}
            for _, r in ipairs(ms or {}) do mrow[#mrow + 1] = r.spotanim .. "@" .. r.tick .. "(" .. tostring(r.x) .. "," .. tostring(r.z) .. ")" end
            t.check("map_spotanim.list", true, #mrow .. ": " .. table.concat(mrow, " "))
            local _, hp = t.ticklog.rows({ kind = "hit_player", since = since })
            local by = { [0] = {}, [1] = {}, [2] = {} }
            for _, r in ipairs(hp or {}) do
                local b = by[r.pid] or {}
                b[#b + 1] = r.damage .. "@" .. r.tick .. "/t" .. tostring(r.npc_type)
                by[r.pid] = b
            end
            t.check("hits.p1_pid0", true, table.concat(by[0], " "))
            t.check("hits.member_pid1", #by[1] > 0, table.concat(by[1], " "))
            t.check("hits.member_pid2", true, table.concat(by[2], " "))
            t.cheat("::tobmazestate")
            t.ticks(2)
            local _, ml = t.msg.last(4)
            local line = ""
            for _, m in ipairs(ml or {}) do
                if string.find(m.text, "tobmazestate", 1, true) then line = m.text end
            end
            t.check("mazestate", true, line)
        end
        t.expect("party.barrier.done", t.party.barrier("done", 200))
    end,
}
