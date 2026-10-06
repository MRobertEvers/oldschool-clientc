-- quest-driver / raid_play_tob_sotetseg: the Sotetseg plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files).
-- Written by raid seam30 play_tob_sotetseg: the Entry solo plan, from the
-- sources line by line.  PLAY_NOTES.md "Sotetseg" is its strategy table; each
-- decision below names its source.
--   E   docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext
--       (==Sotetseg== ===Solo strategy===, lines 190-195)
--   W   .../sources/wiki_Theatre_of_Blood_Strategies.wikitext (==Sotetseg==, 777-811)
--   ET  docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md section 5
--   S   our server, OSRS-Content/.../minigame_tob/scripts/tob_sotetseg.rs2 and
--       configs/tob.constant (what the room does, read so the plan is not a guess)
--   K   test/raids/tob_sotetseg.lua (the kept Entry room) and its kept run
--       build/quest_gate/tob_sotetseg (ledger + ticklog.tsv)

QD.raid._play_plan("tob_sotetseg", {
    room = "sotetseg",
    boss = { entry = "tob_sotetseg_combat_story", normal = "tob_sotetseg_combat", hard = "tob_sotetseg_combat_hard" },
    -- ET 5.1: melee seq 8138, ball seq 8139 (the death ball too), the maze's
    -- portal seq 8142; projectiles 1606 magic (red), 1607 ranged (grey), 1604
    -- the death ball.  E:191 "The red one can be completely blocked with
    -- Protect from Magic, while the grey one ... with Protect from Missiles".
    seq_melee = 8138, seq_ball = 8139, seq_portal = 8142,
    proj_magic = 1606, proj_ranged = 1607, proj_death = 1604,
    -- the shadow-realm path: S tob_sote_light_path changes every path tile of
    -- the underworld grid from tob_sotetseg_darktile to tob_sotetseg_lighttile
    -- (the red path a person sees, W:25 "presented with a randomly generated
    -- path"); the grid is 14 wide and 15 high (ET 5.2), the runner enters on
    -- its south row and leaves off its north edge.
    path_loc = "tob_sotetseg_lighttile", maze_rows = 15, under_level = 3,
    -- ET 5.3 "every 4 ticks if nobody is on the grid, despawn the maze": the
    -- check runs on server ticks 0 mod 4 (K spec maze_cycle 4, re-activation
    -- ticks mod 4 = 0,0, cycle phase global), so the step off the grid is sent
    -- on a tick 2 mod 4 and resolves on 3 ("off on 3", ET 5.3).
    maze_cycle = 4, off_send_phase = 2,
    -- ET 5.3: "on first step onto ROW 4 spawn the tornado ... it then follows
    -- the path"; W:27 "The maze runner should stop on the third row and wait"
    -- (for teammates); solo the wait on row 3 (index 2) is where the plan
    -- times its run so it reaches the path's end on the tick it may leave.
    tornado_row = 3,
    modes = {
        -- Entry, S tob.constant: melee 1..20 (^tob_sote_melee_max_entry), 1..10
        -- through Protect from Melee (^tob_sote_melee_prayed_max_entry), a ball
        -- 1..22 unprayed (^tob_sote_projectile_max_entry; K ball_max_entry 22),
        -- the death ball 15 solo (E:191 "When soloing in Entry Mode, this
        -- projectile will deal 15 damage"; K death_ball_hit_entry_solo 15),
        -- the maze chip 1..3 every 7 ticks (W:25).
        entry = { melee = 20, melee_prayed = 10, ball = 22, death = 15, chip = 3 },
        -- Normal/Hard (W:11 "up to 45 damage (22 if prayed against)", W:13
        -- "up to 50"); the death ball is party-scaled (W:18), a solo Normal
        -- raider is not this plan's case.
        normal = { melee = 45, melee_prayed = 22, ball = 50, death = 121, chip = 3 },
        hard = { melee = 45, melee_prayed = 22, ball = 50, death = 121, chip = 3 },
    },
    -- The library's SEND lights `walk_prayers` and `down_prayers` from the
    -- intent's `want`.  The protection prayers exclude each other and a press
    -- is a toggle (nylocas fixer, seam30: an "off" for the old one after the
    -- new one's "on" lights the old one again), so the plan keeps ONE
    -- protection in walk_prayers, the one it wants this tick (decide writes
    -- slot 1), and the server puts the other out.
    walk_prayers = { "protectfrommelee" },
    -- E:191 "it is best to attack Sotetseg with Melee": the melee boost while
    -- the scythe swings (the kept room's phase 3 wears the scythe adjacent).
    down_prayers = { "piety" },
    decide = "_play_sotetseg_decide",
})

-- the footprint distance a melee is decided on: S tob_sote_attack
-- `npc_range(coord) <= 1` (K melee_range: 7 of 7 swings at 1 tile).
function QD.raid._play_sotetseg_range(b, x, z)
    local n = b.size or 1
    local dx = math.max(0, b.x - x, x - (b.x + n - 1))
    local dz = math.max(0, b.z - z, z - (b.z + n - 1))
    return math.max(dx, dz)
end

-- THE SOTETSEG PLAN'S DECIDE (PLAY_NOTES.md "Sotetseg").
--   The fight: "attack Sotetseg with Melee, praying Protect from Melee and
--   switching to the other two protection prayers when you see their
--   respective projectile" (E:191).  The melee cannot be reacted to (S: its
--   prayer is read in the swing tick's own player phase), so Protect from
--   Melee is up on every tick the raider stands in his melee range; a ball
--   is read when it LANDS (the owner's ruling; S tob_sote_impact runs at the
--   flight's end, K ball_prayer_read_tick), so the prayer for its colour goes
--   up the tick it is seen and stays until it has landed.  Out of his range
--   he only throws balls (S: melee needs npc_range <= 1), so Protect from
--   Magic is up there.  The death ball cannot be prayed (E:191) and is eaten
--   through (W:20 "eat immediately after Sotetseg's second attack
--   animation": the supplies skill sees its 15 coming).
--   The maze (W:23-29, E:193): run the lit path tile by tile, never a dark
--   tile, wait on row 3 so the run ends on the tick the raider may leave,
--   leave the grid north with the step resolving on cycle tick 3.
function QD.raid._play_sotetseg_decide(st, v)
    local P, N = st.plan, st.numbers
    local intent = { want = {}, walk = nil, attack = false }
    st.sote = st.sote or { mazes = {}, balls = 0, death_balls = 0, press_log = {}, read_magic = {}, pray_sent = nil,
        hold = -1, ranged_hold = -1, death_land = -1, magic_ticks = 0, melee_ticks = 0 }
    local S = st.sote
    -- THE SHADOW REALM: the raider is on the underworld's level
    if v.me.level == P.under_level then
        return QD.raid._play_sotetseg_maze(st, v, intent)
    end
    if S.mz ~= nil and S.mz.back == nil then
        -- back in the arena (the re-activation teleport, K maze.returned)
        S.mz.back = v.tick
        st.teleport_until = v.tick + 2
        S.mz = nil
    end
    -- what the raider read lit, before the plan's own press guard below
    -- (the harness's technique.ball_prayer_raised_in_flight reads it)
    S.read_magic[v.tick] = v.lit.protectfrommagic == true
    local b = v.boss
    if b == nil then
        -- his combat form is gone from the portal to the re-activation (S
        -- tob_sote_start_maze retypes him to the noncombat form on the proc
        -- tick, before the client can draw seq 8142 on the combat row): the
        -- library would read three ticks of it as his death, so the plan keeps
        -- that count at zero (his death is the tick log's npc_death row, which
        -- the library reads first) and covers the realm teleport three ticks on
        if st.log then
            st.boss_gone = 0
            st.teleport_until = v.tick + 6
        end
        return intent
    end
    -- the portal animation: the maze is coming (W:23 "a bright white light";
    -- S tob_sote_start_maze npc_anim 8142 is the first thing of it); the
    -- player is stunned and moved to the realm three ticks later
    if b.seq_id == P.seq_portal and S.portal_seen ~= b.seq_tick then
        S.portal_seen = b.seq_tick
        S.portal_tick = v.tick
        st.teleport_until = v.tick + 12
    end
    -- WHAT IS IN THE AIR (what a person sees: the projectiles coming at them)
    local magic_in_air, ranged_in_air, death_land = false, false, nil
    local pr, projs = QD.world.projectiles(30)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            local land = v.tick + math.ceil((p.cycles_left or 0) / 30)
            if p.spotanim_id == P.proj_magic then
                magic_in_air = true
                if land > S.hold then S.hold = land end
            elseif p.spotanim_id == P.proj_ranged then
                ranged_in_air = true
                if land > S.ranged_hold then S.ranged_hold = land end
            elseif p.spotanim_id == P.proj_death then
                if death_land == nil or land < death_land then death_land = land end
                if S.death_seen ~= b.seq_tick then
                    S.death_seen = b.seq_tick
                    S.death_balls = S.death_balls + 1
                end
            end
        end
    end
    local range = QD.raid._play_sotetseg_range(b, v.me.x, v.me.z)
    local adjacent = range <= 1
    -- THE PROTECTION for the NEXT tick (a press is in force for the next npc
    -- phase, DRIVER_NOTES): a ball in the air or not yet landed by one tick
    -- (its impact queue runs on the landing tick) wins; else melee in range,
    -- magic out of it.
    local protect = "protectfrommagic"
    if magic_in_air or v.tick <= S.hold then
        protect = "protectfrommagic"
    elseif ranged_in_air or v.tick <= S.ranged_hold then
        protect = "protectfrommissiles"
    elseif adjacent then
        protect = "protectfrommelee"
    end
    if protect == "protectfrommagic" then S.magic_ticks = S.magic_ticks + 1 end
    if protect == "protectfrommelee" then S.melee_ticks = S.melee_ticks + 1 end
    P.walk_prayers[1] = protect
    intent.want[protect] = true
    -- a press reads lit a tick or two after it is sent; a second press is a
    -- toggle (nylocas fixer, seam30): until the read catches up the plan
    -- treats its own press as lit
    if S.pray_sent ~= nil and S.pray_sent.name == protect and v.tick - S.pray_sent.tick <= 2 then
        v.lit[protect] = true
    end
    if v.lit[protect] ~= true and v.prayer > 0 then
        S.pray_sent = { name = protect, tick = v.tick }
        S.press_log[#S.press_log + 1] = { tick = v.tick, name = protect }
    end
    -- Piety for the whole fight outside the realm (E:191 melee; the kept
    -- room's phase 3): kept lit through the approach too, so it is never
    -- toggled off and on again between swings
    intent.want.piety = true
    if v.lit.piety ~= true then
        if S.piety_sent ~= nil and v.tick - S.piety_sent <= 2 then
            v.lit.piety = true
        elseif v.prayer > 0 then
            S.piety_sent = v.tick
        end
    end
    -- THE ATTACK: "attack Sotetseg with Melee" (E:191); the press paths the
    -- raider into his range and the scythe swings on its own after it
    intent.attack = true
    -- THE SUPPLIES: the most that can land before the next chance to eat
    -- (the library's rule: eat by the largest hit before the next chance).
    -- One attack every 5 ticks (ET 5.1 attack rate 5).  In range: a melee
    -- through Protect from Melee (1..10 Entry); a ball through its prayer is
    -- 0 (survey 2026-10-05 seam30: 0 unprayed balls in 5 names).  The death
    -- ball's 15 when it lands inside the horizon (W:20 tick-eat it).  While
    -- the protections are disabled (an unprayed ball: "disable protection
    -- prayers", W:13; seen as a refused prayer press) a ball and a melee
    -- unprayed.  The first plan carried that unprayed ball on every tick and
    -- ate at 25-43 hitpoints against a largest tick of 15-17 (survey1,
    -- raid_report food).
    if st.refusals > (S.refusals_seen or 0) then
        S.refusals_seen = st.refusals
        S.disabled_until = v.tick + 6
    end
    local disabled = S.disabled_until ~= nil and v.tick <= S.disabled_until
    local function threat(h)
        local attacks = math.ceil(h / 5)
        local per = adjacent and N.melee_prayed or 0
        if disabled then per = N.ball + (adjacent and N.melee or 0) end
        local total = attacks * per
        if death_land ~= nil and death_land - v.tick <= h then total = total + N.death end
        return total
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    intent.attack = QD.raid._play_attack(st, v, intent.attack)
    return intent
end

-- THE MAZE (W:25-29, E:193, ET 5.3-5.4).  The path is read from the tiles a
-- person sees lit (the client's tob_sotetseg_lighttile locs on the realm's
-- level); the walk goes one straight run at a time, each to the path's next
-- corner, so no route can cut a corner over a dark tile ("an L that goes
-- diagonal first steps off the path", ET 5.4).
function QD.raid._play_sotetseg_maze(st, v, intent)
    local P, N, S = st.plan, st.numbers, st.sote
    st.boss_gone = 0
    st.teleport_until = v.tick + 2
    -- he deals no damage while the maze is on (ET 5.3); Protect from Magic
    -- stays up for the balls that come one tick after the re-activation
    -- (K post_maze_first_attack 1), Piety goes out (want leaves it off)
    P.walk_prayers[1] = "protectfrommagic"
    intent.want.protectfrommagic = true
    if v.lit.protectfrommagic ~= true and S.pray_sent ~= nil and S.pray_sent.name == "protectfrommagic" and v.tick - S.pray_sent.tick <= 2 then
        v.lit.protectfrommagic = true
    elseif v.lit.protectfrommagic ~= true and v.prayer > 0 then
        S.pray_sent = { name = "protectfrommagic", tick = v.tick }
        S.press_log[#S.press_log + 1] = { tick = v.tick, name = "protectfrommagic" }
    end
    if v.lit.piety == true and S.piety_off ~= nil and v.tick - S.piety_off <= 2 then
        v.lit.piety = false
    elseif v.lit.piety == true then
        S.piety_off = v.tick
    end
    local mz = S.mz
    if mz == nil then
        mz = { n = #S.mazes + 1, land = v.tick, sx = v.me.x, sz = v.me.z, path = {}, path_n = 0,
            order = nil, idx = 1, walks = 0, waits = 0, off_sent = nil, portal_clicks = 0 }
        S.mazes[#S.mazes + 1] = mz
        S.mz = mz
    end
    -- the lit path, read until it is there (S lights it on the runner's first
    -- tick in the realm: K maze.lit)
    if mz.order == nil then
        local lr, _, rows = QD.world.loc_copies(P.path_loc, 40)
        if lr == "ok" and rows ~= nil then
            for _, r in ipairs(rows) do
                if r.level == P.under_level and mz.path[r.x * 100000 + r.z] == nil then
                    mz.path[r.x * 100000 + r.z] = true
                    mz.path_n = mz.path_n + 1
                end
            end
        end
        if mz.path_n >= P.maze_rows and mz.path[v.me.x * 100000 + v.me.z] then
            -- the order a person walks it: north when the path goes north,
            -- else along the row (S tob_sote_path_has: an even row is lit on
            -- its seed column alone, an odd row from one seed to the next)
            local order, seen = { { v.me.x, v.me.z } }, {}
            local cx, cz = v.me.x, v.me.z
            seen[cx * 100000 + cz] = true
            for _ = 1, 80 do
                local nx, nz = nil, nil
                for _, d in ipairs({ { 0, 1 }, { -1, 0 }, { 1, 0 } }) do
                    local k = (cx + d[1]) * 100000 + (cz + d[2])
                    if nx == nil and mz.path[k] and not seen[k] then nx, nz = cx + d[1], cz + d[2] end
                end
                if nx == nil then break end
                cx, cz = nx, nz
                seen[cx * 100000 + cz] = true
                order[#order + 1] = { cx, cz }
            end
            mz.order = order
            mz.ex, mz.ez = cx, cz
        end
    end
    -- the chip (1..3 every 7 ticks, W:25) is the only damage on a clean run
    local function threat(h) return N.chip * math.ceil(h / 7) + N.chip end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    if mz.order == nil then
        return intent
    end
    -- where the raider is on the path
    local here = nil
    for i, o in ipairs(mz.order) do
        if o[1] == v.me.x and o[2] == v.me.z then here = i end
    end
    if here == nil then
        -- off the path (the step off the end resolved, or a wrong tile):
        -- nothing to walk; the re-activation takes the raider back
        mz.off_path = (mz.off_path or 0) + 1
        return intent
    end
    mz.idx = here
    local n = #mz.order
    local moved = st.last_me ~= nil and (st.last_me.x ~= v.me.x or st.last_me.z ~= v.me.z)
    if here == n then
        -- THE PATH'S END: off the grid north, sent on a tick 2 mod 4 so it
        -- resolves on 3 ("off on 3", ET 5.3)
        if v.tick % P.maze_cycle == P.off_send_phase and (mz.off_sent == nil or v.tick - mz.off_sent >= P.maze_cycle) then
            intent.walk = { x = v.me.x, z = v.me.z + 1 }
            mz.off_sent = v.tick
            mz.off_sends = (mz.off_sends or 0) + 1
            if mz.off_sends >= 3 and mz.portal_clicks < 1 then
                -- the step was refused twice: the portal (E:193 "click on the
                -- portal to return")
                mz.portal_clicks = mz.portal_clicks + 1
                QD.player.click_loc("tob_sotetseg_darkrealm_exit", 1)
            end
        else
            mz.waits = mz.waits + 1
        end
        return intent
    end
    -- the next corner: the farthest tile of the straight run from here
    local function corner(from)
        local dx = mz.order[from + 1][1] - mz.order[from][1]
        local dz = mz.order[from + 1][2] - mz.order[from][2]
        local j = from + 1
        while j < n and mz.order[j + 1][1] - mz.order[j][1] == dx and mz.order[j + 1][2] - mz.order[j][2] == dz do
            j = j + 1
        end
        return j
    end
    -- W:27 "stop on the third row and wait": the tornado spawns on the first
    -- step onto row 4 (index 3, ET 5.3); before that step the plan waits until
    -- the rest of the run (one tile a tick) ends on a tick 2 mod 4, so the
    -- raider leaves the end tile at once with the tornado behind it
    local next_row = mz.order[here + 1][2] - mz.sz
    local row_here = v.me.z - mz.sz
    if row_here == P.tornado_row - 1 and next_row == P.tornado_row and mz.timed == nil then
        local left = n - here
        if (v.tick + left) % P.maze_cycle ~= P.off_send_phase then
            mz.waits = mz.waits + 1
            return intent
        end
        mz.timed = v.tick
    end
    local j = corner(here)
    local tx, tz = mz.order[j][1], mz.order[j][2]
    -- before the tornado row, walk only up to the row-3 tile the wait is on
    if mz.timed == nil then
        for k = here + 1, j do
            if mz.order[k][2] - mz.sz >= P.tornado_row then
                j = k - 1
                break
            end
        end
        if j <= here then j = here + 1 end
        tx, tz = mz.order[j][1], mz.order[j][2]
    end
    -- S tob_sote_send_party p_stun 5 from the proc, the realm 3 after it: a
    -- step on the first two ticks there is dropped (K maze.stall, maze_first_move 5)
    if v.tick < mz.land + 2 then
        return intent
    end
    local same = st.walk_target ~= nil and st.walk_target.x == tx and st.walk_target.z == tz
    if not same or not moved then
        intent.walk = { x = tx, z = tz }
        mz.walks = mz.walks + 1
    end
    return intent
end
