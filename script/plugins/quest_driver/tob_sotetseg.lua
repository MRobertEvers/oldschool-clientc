-- quest-driver / tob_sotetseg: SOTETSEG, Normal trio.
--
--   t.raid.sotetseg_solve(opts) -> result, detail, record
--
-- Called by every seat at the room's entry (the corridor south of the
-- barrier). The leader starts the room at once (he does nothing before the
-- start) and a member crosses once it sees the leader inside. Then the fight
-- on the shared loop (tob.lua), MEASURE -> DECIDE -> ACT a tick, in explicit
-- phases. The plan is ROOM_SOLVERS.md 4.4 / 4.4.1, the spec
-- solver_specs/sotetseg.md. Local tiles are from the room's origin, his
-- south-west tile minus (13,38).
--
--   his clock    an attack every 5 from start + 6: a ball (always magic, the
--                prayer read at LANDING), a melee on a target within 1 (50%),
--                or after 10 balls the death ball.
--   the ball     lands floor((56 + 8d) / 30) after its launch (d from his
--                (15,40)); on landing a grey (Missiles) and a red (Magic)
--                ricochet to the other two, floor((41 + 8d) / 30) on, a tick
--                more for a recipient with a lower pid than the victim.
--   death ball   lands 15 after its launch on whoever stands within 1 of the
--                target's tile, 1..121 split between them.
--   the maze     at 2/3 and 1/3 hp: everyone stunned 5, teleported on P+3;
--                the lowest pid runs the lit path in the shadow realm
--                (plane 3), the others wait off the arena grid; a wrong tile
--                rags everyone within 1, a tornado walks the path behind.
--
-- THE KIT: the Learner/Void melee set on every seat (the tentacle, Piety),
-- each seat beside one of his faces (west, east, north): his ranged defence
-- is 150, so the blowpipe misses; a target within 1 risks his melee (1..45
-- through Protect from Magic), which the wiki's teams take and eat through.
-- ==========================================================================

QD.SOTE = {
    SIZE = 5, SW = { x = 13, z = 38 }, SRC = { x = 15, z = 40 },
    -- THE MELEE STATIONS, beside his faces (his footprint x13..17 z38..42),
    -- one a seat: west, east, north-centre. The wiki's trio stands "east,
    -- west and north-west respectively" (wiki_Theatre_of_Blood_Strategies
    -- .wikitext, Sotetseg) to lengthen the flights; on 48 seeds the north
    -- face's west end (13,43) was 45 green against north-centre's 48 (sweeps
    -- so11 / so10: two styles landing on one seat in one tick, 136 hp a raider
    -- against 127), so seat 3 keeps 3 from both others. Melee, not the
    -- blowpipe: his ranged defence is 150 against 70 stab/slash (the cache's
    -- and the wiki's), and the probe's blowpipe missed 86% (so1/sa).
    STATION = { [1] = { x = 12, z = 40 }, [2] = { x = 18, z = 40 }, [3] = { x = 15, z = 43 } },
    GRID = { x0 = 9, z0 = 22, x1 = 22, z1 = 36 },         -- the arena's maze grid, level 0
    REALM = { x0 = 26, z0 = 23, x1 = 39, z1 = 37 },        -- the shadow realm's, level 3
    WAIT = { x = 15, z = 20 },                             -- the waiters, off the grid
    -- THE MAZE AS TEAMS RUN IT (Blert, 208 Normal trio mazes, the scratchpad
    -- maze pass of 2026-10-09; Plank2g: "If you're up top ... click on the red
    -- tiles that get highlighted", "the maze runner needs to stop on the third
    -- row"): the runner holds on row 2 (0-based) ROW2_HOLD ticks; the team
    -- steps on behind it and keeps FOLLOW_LAG rows back, on tiles that have
    -- glowed only (0 of 428 Blert followers ever stood off the trail)
    ROW2_HOLD = 2, FOLLOW_LAG = 2,
    -- THE WAY OUT: the realm's portal, loc 33037 tob_sotetseg_darkrealm_exit,
    -- 8 x 2 on realm local (29,42) (cache map m52_67; content
    -- [oploc1,tob_sotetseg_darkrealm_exit]). The maze ends only once a runner
    -- has ENTERED it (content ~tob_sote_realm_open; wiki_Sotetseg.wikitext:97
    -- "enter the portal at the end to return to the arena"). Blert's runners
    -- (118 Normal, 88 Hard, recorded from inside the realm) op it from the last
    -- row: z37 -> z39 -> z41 at a run, a tick held beside it, a tick in it,
    -- then beside him. PORTAL_RETRY: re-sent after that many ticks unmoved
    -- short of it (a held op clears a pending interaction, tob2_exec.lua E5);
    -- PORTAL_EAT: the walk's eat floor (the realm's chip is 1..3 a 7 ticks).
    PORTAL = { x0 = 29, z0 = 42, w = 8, l = 2 }, PORTAL_RETRY = 2, PORTAL_EAT = 25,
    DEATH_FLIGHT = 15,
    H = 10, BEAM = 32,
}

local function cheb(ax, az, bx, bz) return QD.raid._tob_cheb(ax, az, bx, bz) end

-- A projectile row's identity across ticks: spotanim, target, source tile and
-- launch tick (F.tick - elapsed ticks; QD.raid._sote_measure).
function QD.raid._sote_proj_key(p, pid)
    local launch = api_drive.tick() - (p.duration - p.cycles_left) // 30
    return p.spotanim_id .. ":" .. pid .. ":" .. p.src_x .. "," .. p.src_z .. ":" .. launch
end
function QD.raid._sote_ids()
    local ids = QD.raid._tob_common_ids("sotetseg_solve")
    local room = QD.raid._tob_symbols("sotetseg_solve", {
        { "boss", "npc", "tob_sotetseg_combat" }, { "boss_maze", "npc", "tob_sotetseg_noncombat" },
        { "tornado", "npc", "tob_sotetseg_creeper" },
        { "ball", "spotanim", "tob_sotetseg_maging" }, { "grey", "spotanim", "tob_sotetseg_ranging" },
        { "death", "spotanim", "tob_sotetseg_sharedattack" },
        { "lit", "loc", "tob_sotetseg_lighttile" }, { "death_seq", "seq", "tob_sotetseg_death" },
        { "seq_melee", "seq", "tob_sotetseg_attack_melee" }, { "seq_ball", "seq", "tob_sotetseg_attack_ranged" },
        { "melee_helm", "obj", "game_pest_melee_helm" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "torture", "obj", "zenyte_amulet_enchanted" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "defender", "obj", "dragon_parryingdagger" }, { "hammer", "obj", "dragon_warhammer" },
    })
    for k, v in pairs(room) do ids[k] = v end
    ids.melee_set = { ids.melee_helm, ids.tentacle, ids.torture, ids.fire_cape, ids.defender }
    return ids
end

-- ================================================================== MEASURE

function QD.raid._sote_measure(S, F)
    local V, ids = S.V, S.ids
    local tr, tile = api_drive.player_tile()
    assert(tr == "ok", "sotetseg_solve: no tile")
    F.level = tile.level or 0
    F.boss, F.tornadoes = nil, {}
    for _, row in ipairs(F.npcs) do
        if row.npc_id == ids.boss or row.npc_id == ids.boss_maze then F.boss = row end
        if row.npc_id == ids.tornado then F.tornadoes[#F.tornadoes + 1] = row end
    end
    if F.boss and F.level == 0 then
        S.base = { x = F.boss.x - V.SW.x, z = F.boss.z - V.SW.z }
    end
    F.maze = F.boss ~= nil and F.boss.npc_id == ids.boss_maze
    if F.level == 3 then F.maze = true end
    -- THE PROJECTILES, each keyed on its spotanim, target and first sight;
    -- its landing from the CONTENT's flight from first sight (lesson: the live
    -- client reads a projectile further into its flight than scriptrun)
    local r, projs = api_drive.projectiles(0)
    if r ~= "ok" then projs = {} end
    local live = {}
    for _, p in ipairs(projs) do
        local pid = (p.target ~= nil and p.target < 0) and (-p.target - 1) or nil
        if pid and (p.spotanim_id == ids.ball or p.spotanim_id == ids.grey or p.spotanim_id == ids.death) then
            -- keyed with its LAUNCH tick too (both lanes count a row's elapsed
            -- flight in whole ticks, so duration - cycles_left is 30 x the
            -- ticks since launch): a second grey off the same victim tile at
            -- me, thrown while the first is still in the air, shared every
            -- other field and was never seen (tob2 g8 q39 t260: a 6-tick grey
            -- of t255 and a 3-tick one of t260, both off pid 5's tile; the
            -- second landed unprayed, 42 + 17 + 16)
            local key = QD.raid._sote_proj_key(p, pid)
            live[key] = true
            -- NEW when it was not in flight LAST tick: two greys from one
            -- victim tile to one raider share every field, and keyed for good
            -- the second was never seen (seed so1/sa t109: the grey at t103
            -- had the key, Missiles never went up, the block cascaded)
            if not S.proj_live[key] then
                local P = { kind = "ball", pid = pid, first = F.tick }
                -- the flight as the server sent it (duration, fixed for the
                -- projectile's life): the impact queue is duration // 30
                -- ticks. A distance read off the tiles is the target's tile
                -- after it moved, not at launch (seed so7/tj t82: a ball at 6
                -- read as 4, a tick early), and a +/-1 window merged a grey on
                -- T with a red on T+1 into one Magic press (so7/sv t239)
                local flight = p.duration // 30
                if p.spotanim_id == ids.death then
                    P.kind = "death"
                    P.land = F.tick + V.DEATH_FLIGHT
                    S.deaths = S.deaths + 1
                elseif S.base and p.src_x == S.base.x + V.SRC.x and p.src_z == S.base.z + V.SRC.z then
                    P.kind = "ball"
                    P.land = F.tick + flight
                else
                    -- a ricochet, off the ball that landed on its victim this
                    -- tick: its impact is queued on the recipient, so one the
                    -- server processed before the victim this tick (a lower
                    -- pid) runs it a tick later (6,031 ricochets on sweep so7:
                    -- every lower-pid recipient +1, every higher +0)
                    P.kind = (p.spotanim_id == ids.grey) and "grey" or "red"
                    P.land = F.tick + flight
                    local victim = nil
                    for _, Q in ipairs(S.incoming) do
                        if Q.kind == "ball" and Q.land == F.tick then victim = Q.pid end
                    end
                    if victim == nil then
                        P.land2 = P.land + 1
                    elseif pid < victim then
                        P.land = P.land + 1
                    end
                end
                S.incoming[#S.incoming + 1] = P
            end
        end
    end
    S.proj_live = live
    local keep = {}
    for _, P in ipairs(S.incoming) do
        if (P.land2 or P.land) >= F.tick then keep[#keep + 1] = P end
    end
    S.incoming = keep
    -- MY PROTECTION BLOCK: protection gone without a press of mine (none of
    -- the three lit where one was) is an unprayed hit landing now, blocking
    -- until now + 5; one landing while blocked extends it
    local prot = F.lit["protectfrommagic"] or F.lit["protectfrommissiles"] or F.lit["protectfrommelee"]
    if S.prot_was and not prot then
        S.block_until = F.tick + 5
        QD.raid._tob_trace(S, F.tick, "protection blocked to t" .. S.block_until)
    end
    -- (a landing ON the lifting tick is read before that tick's press: it
    -- re-blocks too)
    if S.block_until and F.tick <= S.block_until and F.tick > S.block_until - 5 then
        for _, P in ipairs(S.incoming) do
            if P.pid == F.pid and P.kind ~= "death" and P.land == F.tick and P.land2 == nil then
                S.block_until = F.tick + 5
            end
        end
    end
    S.prot_was = prot
    -- HIS CLOCK: an attack seen (his attack seq, keyed on its seq_tick) puts
    -- the next slot 5 on, 10 after a death ball (its 1604 launched that
    -- tick); out of a maze, he attacks on the tick after he turns back
    if F.boss and F.boss.npc_id == ids.boss and (F.boss.seq_id == ids.seq_melee or F.boss.seq_id == ids.seq_ball)
        and F.boss.seq_tick ~= S.attack_seq_tick then
        S.attack_seq_tick = F.boss.seq_tick
        local death = false
        for _, P in ipairs(S.incoming) do
            if P.kind == "death" and P.first == F.tick then death = true end
        end
        S.slot_next = F.tick + (death and 10 or 5)
    end
    if F.boss and F.boss.npc_id == ids.boss_maze then S.in_maze = true end
    if S.in_maze and F.boss and F.boss.npc_id == ids.boss then
        S.in_maze = false
        S.slot_next = F.tick + 1
    end
    if S.slot_next == nil and S.fight_start then S.slot_next = S.fight_start + 6 end

    -- THE ARENA'S TRAIL (the team up top, level 0): one tile glows, the
    -- runner's; every tile that has glowed this maze is remembered, in order
    if F.level == 0 and F.maze and S.base then
        local lr, rows = api_drive.locs(0)
        if lr == "ok" then
            for _, row in ipairs(rows) do
                if row.level == 0 and (row.loc_id == ids.lit or row.resolved_loc_id == ids.lit) then
                    local c, r = row.x - S.base.x - QD.SOTE.GRID.x0, row.z - S.base.z - QD.SOTE.GRID.z0
                    local key = c .. "," .. r
                    if c >= 0 and r >= 0 and c <= 13 and r <= 14 and not S.trail_seen[key] then
                        -- the tile the runner ran THROUGH from the last glow:
                        -- a two-tile move takes its long axis first (a 2x2
                        -- move its diagonal), the engine's pathing and the
                        -- planner's; a follower clicking glow to glow takes
                        -- the same route (Plank2g: "click on the red tiles")
                        local prev = S.trail[#S.trail]
                        if prev then
                            local dx, dz = c - prev.c, r - prev.r
                            local ax, az = math.abs(dx), math.abs(dz)
                            if math.max(ax, az) == 2 then
                                local mx, mz
                                if ax == 2 and az == 2 then mx, mz = dx // 2, dz // 2
                                elseif ax == 2 then mx, mz = dx // 2, 0
                                else mx, mz = 0, dz // 2 end
                                S.trail_seen[(prev.c + mx) .. "," .. (prev.r + mz)] = true
                            end
                        end
                        S.trail_seen[key] = true
                        S.trail[#S.trail + 1] = { c = c, r = r }
                    end
                end
            end
        end
    end
    if not F.maze then S.trail, S.trail_seen = {}, {} end
    -- THE PATH (the runner, plane 3): the lit locs, seen on P+4
    F.path = nil
    if F.level == 3 then
        local lr, rows = api_drive.locs(0)
        if lr == "ok" then
            F.path = {}
            for _, row in ipairs(rows) do
                if row.level == 3 and (row.loc_id == ids.lit or row.resolved_loc_id == ids.lit) then
                    F.path[row.x .. "," .. row.z] = { x = row.x, z = row.z }
                end
            end
        end
    end
end

-- ================================================================== PRAYER

-- ===================================================================== GEAR

-- =================================================================== PHASES

function QD.raid._sote_phase(S, F)
    local phase
    -- DONE only on his death: the death animation seen, or his row gone five
    -- ticks running on level 0 (seed so3/sc: the runner, back from the realm,
    -- saw no row for a tick, called it his death and stood the rest of the
    -- room out - the death ball split two ways and killed both)
    if F.boss and F.boss.seq_id == S.ids.death_seq then S.dying = true end
    if F.boss == nil and F.level == 0 and S.seen_boss then
        S.gone = (S.gone or 0) + 1
    else
        S.gone = 0
    end
    if S.seen_boss and F.boss == nil and F.level == 0 and (S.dying or S.gone >= 5) then
        phase = "done"
    elseif F.level == 3 then
        phase = "maze_runner"
    elseif F.maze then
        phase = "maze_waiter"
    else
        phase = "fight"
        for _, P in ipairs(S.incoming) do
            if P.kind == "death" and P.land >= F.tick then phase = "death_ball" end
        end
    end
    if phase ~= S.phase then
        QD.raid._tob_trace(S, F.tick, "phase " .. phase)
        S.phase_log[#S.phase_log + 1] = phase .. "@" .. (F.tick - (S.fight_start or F.tick))
        if phase == "maze_waiter" or phase == "maze_runner" then
            if not S.maze_at or F.tick - S.maze_at > 40 then
                S.mazes = S.mazes + 1
                S.maze_at = F.tick
            end
        end
        S.phase = phase
    end
    return phase
end

-- ===================================================================== STEP

-- HIS FIGHT, every seat: my station beside him and the target; out of his
-- melee on his slots; no ricochet onto my own landing. -> target, station
function QD.raid._sote_fight_zones(S, F, add, phase)
    local V, ids = S.V, S.ids
    local B = S.base
    local target, station = nil, nil
    if phase == "fight" or phase == "death_ball" then
        local st = V.STATION[S.role] or V.STATION[1]
        station = { x = B.x + st.x, z = B.z + st.z }
        if F.boss and F.boss.npc_id == ids.boss then target = F.boss end
        -- OUT OF HIS MELEE ON HIS SLOTS: a target that ended the tick before
        -- within 1 of him is meleed 1 in 2 (1..45; 184 a raider a room on 48
        -- seeds, sweep sg1), so on every slot T every seat ends T-1 2+ from
        -- him and he can only throw the ball Protect from Magic blocks
        if S.slot_next then
            for k = 0, 2 do
                local T = S.slot_next + 5 * k
                if T - 1 >= F.tick + 1 and T - 1 <= F.tick + V.H then
                    add.zone("slot", { x = B.x + V.SW.x, z = B.z + V.SW.z, size = V.SIZE, lo = 0, hi = 1,
                        t0 = T - 1, t1 = T - 1, tier = "lethal", cost = 45 })
                end
            end
        end
    end
    if phase == "fight" or phase == "death_ball" then
        -- NO RICOCHET ONTO MY OWN LANDING: a ball landing on a teammate V at
        -- L' throws a ricochet of a random style at me, landing L' + (41 +
        -- 8d) // 30 (+1 when my pid is below V's: the impact queue is mine,
        -- run before V's turn - so7's 6,031 ricochets), d my distance from V
        -- when V's queue runs. If that is the tick something of mine already
        -- lands, one of the two may be the other style, and an unprayable hit
        -- blocks prayer for 5 ticks - the cascade that killed seat 3 walking
        -- in stacked (seed so19/uj t85). Plank2g: "pay attention to who Soda
        -- Seg shoots his balls at and then you can watch those balls split
        -- off". So I stay out of the distance band that lands it then. At the
        -- stations every ricochet is 2 ticks and this never fires.
        for _, Pm in ipairs(S.incoming) do
            if Pm.pid == F.pid and Pm.kind ~= "death" and Pm.land >= F.tick + 2 then
                for _, Pv in ipairs(S.incoming) do
                    if Pv.kind == "ball" and Pv.pid ~= F.pid and Pv.land < Pm.land then
                        local order = (F.pid < Pv.pid) and 1 or 0
                        local ft = Pm.land - Pv.land - order
                        local tm = (F.pid < Pv.pid) and Pv.land or (Pv.land - 1)
                        if ft >= 1 and tm >= F.tick + 1 then
                            local dmin = math.max(0, -((41 - 30 * ft) // 8))
                            local dmax = (30 * (ft + 1) - 1 - 41) // 8
                            local V_ = nil
                            for _, rd in ipairs(F.raiders) do if rd.pid == Pv.pid then V_ = rd end end
                            if V_ and dmax >= dmin then
                                add.zone("ricochet-onto-mine", { x = V_.x, z = V_.z, size = 1, lo = dmin, hi = dmax,
                                    t0 = tm, t1 = tm, tier = "lethal", cost = 25 })
                            end
                        end
                    end
                end
            end
        end
    end
    return target, station
end

-- THE MAZE, UP TOP: follow the trail the runner's glow lays. -> my station
function QD.raid._sote_waiter(S, F, add)
    local V = S.V
    local B = S.base
    local station = nil
    -- BACK THROUGH THE PORTAL, the maze not over yet (his re-activation is
    -- 0..3 ticks after my return, Blert 104 of 118; Hard Mode's runners after
    -- the first wait out the others): beside him already, so my station, off
    -- the grid - not the waiters' tile south of it across the whole grid
    if S.ran_maze == S.mazes then return QD.raid._sote_back(S, F, add) end
    do
        -- FOLLOW THE TRAIL: on the grid only tiles that have glowed, the goal
        -- the last of them FOLLOW_LAG rows behind the glow; before the runner
        -- is that far in, beside the grid's south edge under its first tile.
        -- The arena tornado (spawned when one of us reaches row 3) walks the
        -- path a tile a tick behind; its tile and its neighbours are lethal.
        local G = V.GRID
        local head = S.trail[#S.trail]
        -- OFF ON THE LAST ROW: once the glow is on the last two rows the team
        -- leaves north, running THROUGH row 14 - a tile run through is not
        -- judged, only the one a tick ends on ("you can consistently step off
        -- the maze from anywhere on the second last row no matter where the
        -- path leads", wiki Guide/Advanced ToB "Off on 3"; the content's rag is
        -- the end-of-tick tile's). The runner's own exit ran through row 14
        -- too, so no glow ever names a row-14 tile (seed so14/sb: the team
        -- stood on row 13 for 70 ticks and held the maze open).
        local leaving = head ~= nil and head.r >= G.z1 - G.z0 - 1
        for x = 0, G.x1 - G.x0 do
            for z = 0, G.z1 - G.z0 do
                if not S.trail_seen[x .. "," .. z] then
                    if leaving and z == G.z1 - G.z0 then
                        add.zone("off-trail-end", { x = B.x + G.x0 + x, z = B.z + G.z0 + z, size = 1, lo = 0, hi = 0,
                            t0 = F.tick + 1, t1 = F.tick + V.H, tier = "lethal", cost = 50 })
                    else
                        add.forbid("off-trail", { x = B.x + G.x0 + x, z = B.z + G.z0 + z, t0 = F.tick, t1 = F.tick + V.H,
                            tier = "lethal", cost = 50 })
                    end
                end
            end
        end
        local goal = nil
        if head then
            for i = #S.trail, 1, -1 do
                if S.trail[i].r <= head.r - V.FOLLOW_LAG then goal = S.trail[i] break end
            end
        end
        if leaving then
            -- the runner is on the last two rows: off the grid north, over
            -- the trail, so the 4-tick check finds the arena empty
            local st = V.STATION[S.role] or V.STATION[1]
            station = { x = B.x + st.x, z = B.z + st.z }
        elseif goal then
            station = { x = B.x + G.x0 + goal.c, z = B.z + G.z0 + goal.r }
        elseif head then
            station = { x = B.x + G.x0 + S.trail[1].c, z = B.z + G.z0 - 1 }
        else
            station = { x = B.x + V.WAIT.x, z = B.z + V.WAIT.z }
        end
        for _, tn in ipairs(F.tornadoes) do
            if tn.level == nil or tn.level == 0 then
                add.zone("tornado", { x = tn.x - 1, z = tn.z - 1, size = (tn.size or 3) + 2, lo = 0, hi = 0,
                    t0 = F.tick + 1, t1 = F.tick + 3, tier = "lethal", cost = 50 })
            end
        end
    end
    return station
end

-- BACK IN THE ARENA, the maze still on: the arena grid forbidden, my fight
-- station. -> my station
function QD.raid._sote_back(S, F, add)
    local V = S.V
    local B = S.base
    local G = V.GRID
    for x = G.x0, G.x1 do
        for z = G.z0, G.z1 do
            add.forbid("grid", { x = B.x + x, z = B.z + z, t0 = F.tick, t1 = F.tick + V.H, tier = "lethal", cost = 50 })
        end
    end
    local st = V.STATION[S.role] or V.STATION[1]
    return { x = B.x + st.x, z = B.z + st.z }
end

-- The portal's row in the loc listing (the copy nearest me), or nil.
function QD.raid._sote_portal_row(S, F)
    if S.ids.portal == nil then
        local r, id = api_drive.symbol("loc", "tob_sotetseg_darkrealm_exit")
        assert(r == "ok", S.who .. ": no loc named tob_sotetseg_darkrealm_exit")
        S.ids.portal = id
    end
    local lr, rows = api_drive.locs(40)
    if lr ~= "ok" then return nil end
    local best, bd = nil, nil
    for _, row in ipairs(rows) do
        if row.loc_id == S.ids.portal or row.resolved_loc_id == S.ids.portal then
            local d = cheb(F.me.x, F.me.z, row.x, row.z)
            if bd == nil or d < bd then best, bd = row, d end
        end
    end
    return best
end

-- Is my end-of-tick tile the realm grid's last row or north of it (the walk
-- to the portal is mine to make)?
function QD.raid._sote_at_exit(S, F)
    local R = S.V.REALM
    local bz = F.me.z - F.me.z % 64
    return F.level == 3 and F.me.z - bz >= R.z1
end

-- THE MAZE, IN THE REALM: the lit path, the hold on row 2. -> my station
function QD.raid._sote_runner(S, F, add)
    local V = S.V
    local station = nil
    S.ran_maze = S.mazes
    do
        -- THE RUNNER: every realm grid cell off the lit path forbidden, the
        -- tornado and its neighbours a lethal zone, the exit row pulled to
        local bx, bz = F.me.x - F.me.x % 64, F.me.z - F.me.z % 64
        local path = F.path or {}
        local n = 0
        for _ in pairs(path) do n = n + 1 end
        if n > 0 and not S.path_logged then
            S.path_logged = true
            S.path_log[#S.path_log + 1] = n
            QD.raid._tob_trace(S, F.tick, "the path: " .. n .. " lit tiles")
        end
        if n > 0 then
            for x = V.REALM.x0, V.REALM.x1 do
                for z = V.REALM.z0, V.REALM.z1 do
                    if not path[(bx + x) .. "," .. (bz + z)] then
                        add.forbid("off-path", { x = bx + x, z = bz + z, t0 = F.tick, t1 = F.tick + V.H,
                            tier = "lethal", cost = 50 })
                    end
                end
            end
            -- THE TOP ROW'S PATH TILE (an even row: one tile), not off the
            -- grid: the step off is the portal's own walk (QD.raid._sote_portal)
            station = { x = bx + 32, z = bz + V.REALM.z1 + 1 }
            for _, t in pairs(path) do
                if t.z == bz + V.REALM.z1 then station = { x = t.x, z = t.z } end
            end
            -- HOLD ON ROW 2 (0-based) ROW2_HOLD ticks for the team to get
            -- on behind (the wiki: "The maze runner should stop on the third
            -- row and wait"); the realm's tornado rises past row 3, not before
            -- and FIRST ONTO ROW 0: the runner lands a tile south of the
            -- grid, and the team's trail starts at the first glow (Blert:
            -- row 0 in 208 of 208 mazes)
            local row = F.me.z - bz - V.REALM.z0
            if row == 2 then S.row2_ticks = (S.row2_ticks or 0) + 1 elseif row < 2 then S.row2_ticks = 0 end
            local stop = nil
            if row < 0 then stop = 1 elseif row <= 2 and (S.row2_ticks or 0) < V.ROW2_HOLD then stop = 3 end
            if stop then
                for x = V.REALM.x0, V.REALM.x1 do
                    add.forbid("hold", { x = bx + x, z = bz + V.REALM.z0 + stop, t0 = F.tick + 1, t1 = F.tick + 1,
                        tier = "lethal", cost = 50 })
                end
            end
        end
        for _, tn in ipairs(F.tornadoes) do
            add.zone("tornado", { x = tn.x - 1, z = tn.z - 1, size = (tn.size or 3) + 2, lo = 0, hi = 0,
                t0 = F.tick + 1, t1 = F.tick + 3, tier = "lethal", cost = 50 })
        end
    end
    return station
end

-- ===================================================================== LOOP
