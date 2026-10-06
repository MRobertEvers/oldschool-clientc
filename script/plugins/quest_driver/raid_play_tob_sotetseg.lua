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
    -- raid seam33 play_tob_sotetseg_normal: the trio's numbers (read only by
    -- QD.raid._play_sotetseg_trio; the solo plan never reads them).
    -- His form while a maze is on (S tob_sote_start_maze retypes him to the
    -- mode's noncombat record; tob_sote_end_maze back): a raider who sees it
    -- knows the maze is on and he is not dead.
    idle = { entry = "tob_sotetseg_noncombat_story", normal = "tob_sotetseg_noncombat", hard = "tob_sotetseg_noncombat_hard" },
    -- the arena's copy of the grid, from the room's south-west corner (S
    -- tob.constant ^tob_sote_maze_lx 9, ^tob_sote_maze_lz 22; ^tob_sote_maze_w
    -- 14): the tile the runner stands on is lit there (S tob_sote_mirror; W:801
    -- "players in the real world will see the tile the player in the Shadow
    -- Realm is standing on with a red glow")
    over_lx = 9, over_lz = 22, maze_cols = 14,
    -- the runner holds the path's last tile this many ticks before the step
    -- off, so the raiders reading it are on the grid before the 4-tick check
    -- can end the maze (S tob_sote_grid_occupied counts both grids).  The
    -- seam's choice: no source gives a number.  raid seam50: 2, the live
    -- followers are on the grid a run behind the runner, not waiting for the
    -- whole path (Blert: the runner back beside him a tick after the
    -- followers' last row, 16ff015b +28).
    party_end_hold = 2,
    -- the death ball's gather starts this many ticks before it lands (the
    -- longest walk from a seat to the front tile is 5 tiles: 3 ticks running,
    -- one for the press, one to spare).  The seam's choice.  raid seam50: 6,
    -- the east seat (5,4) is 8 tiles round his south-east corner.
    gather_lead = 6,
    -- raid seam42: a follower walks only to a tile at least this many lit
    -- tiles behind the newest glow (the runner runs two tiles a tick; two
    -- tiles is one tick of his run).  The seam's choice.
    follow_lag = 2,
    -- raid seam50: the follower walks the lit route as it is lit, as the
    -- Blert trios do (QD.raid._play_sotetseg_follow, its live block).
    follow_live = true,
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
    if (st.party or 1) > 1 then
        -- raid seam33: the Normal trio has its own decide (below); the solo
        -- Entry plan is unchanged
        return QD.raid._play_sotetseg_trio(st, v)
    end
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
        -- raid seam33: in a party the runner holds the last tile until the
        -- raiders reading the glow are on the arena's grid (the maze ends on a
        -- 4-tick check that finds BOTH grids empty: S tob_sote_grid_occupied),
        -- so they walk the path behind it (yt_4i4lv-srJkw.md:97 "Everyone
        -- else just needs to follow the path")
        mz.end_at = mz.end_at or v.tick
        local held = (st.party or 1) > 1 and v.tick < mz.end_at + P.party_end_hold
        -- THE PATH'S END: off the grid north, sent on a tick 2 mod 4 so it
        -- resolves on 3 ("off on 3", ET 5.3)
        if not held and v.tick % P.maze_cycle == P.off_send_phase and (mz.off_sent == nil or v.tick - mz.off_sent >= P.maze_cycle) then
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
    -- raid seam33: no wait in a party: "This tornado will not appear for the
    -- maze runner (unless they are the only player in the encounter)" (W:803;
    -- S tob_sote_runner_tick checks the tornado only for a party of one or
    -- Hard), and the raiders reading the glow walk once the path is whole
    if (st.party or 1) > 1 and mz.timed == nil then
        mz.timed = v.tick
    end
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

-- ==========================================================================
-- THE NORMAL TRIO (raid seam33 play_tob_sotetseg_normal; PLAY_NOTES.md
-- "Sotetseg, Normal trio").  Every raider runs this; the room picks the maze
-- runner (S tob_sote_send_party: the first raider its hunt finds), so the
-- plan does not choose one: whoever lands in the realm runs the path
-- (QD.raid._play_sotetseg_maze, the Entry runner with the two party lines),
-- the other two read the glow and walk it (QD.raid._play_sotetseg_follow).
--   W:791 "In a trio encounter, players will stand to the east, west and
--         north-west respectively" (spread "to increase the projectile's
--         travel time so they can be reacted to in time")
--   W:789 a ball "up to 50 damage and disable protection prayers, but ...
--         fully blocked if prayed against correctly"; W:787 the melee "up to
--         45 damage (22 if prayed against)"
--   W:794 the death ball "121+ damage ... This damage can be split among
--         other players"; yt_4i4lv-srJkw.md:95 "learners should all gather
--         together on the tile directly in front of Sotoseg. This splits the
--         damage evenly between you"; yt_KF9y2GYTJ-A.md:151 "group up at the
--         center tile in front of the boss"
--   yt_KF9y2GYTJ-A.md:151 "to start the room pray magic and piety and attack
--         the boss"; the Entry plan's switch (melee in his range, the ball's
--         colour while one flies at the raider) is kept: a ball is read at
--         IMPACT (the owner's ruling, S tob_sote_impact)
-- ==========================================================================

-- the seat for a role, from his south-west tile and size (W:791 east, west,
-- north-west).  Each seat shares an edge with his footprint (the scythe's
-- reach), and no two seats or his centre are nearer than 3 tiles, so every
-- ball and ricochet flies at least 2 ticks (S tob_sote_cast / tob_sote_ricochet:
-- duration = delay + 36 + 8 per tile, 30 cycles a tick; a 1-tile ricochet is 1).
-- raid seam50 play_tob_sotetseg_whole: the Blert trios' tiles.  Their
-- positions relative to his south-west tile (sotetseg_normal_3.json
-- positions, 20 rooms, ticks spent) are the four face corners, one per
-- raider: (-1,0) the west face's south end, (4,-1) the south face's east end,
-- (5,4) the east face's north end, (0,5) the north face's west end (melee2:
-- (4,-1) 25-27% of its ticks in every phase; melee1: (5,4), (-1,0), (0,5),
-- (4,-1) 12-20% each).  Ours takes the three nearest the maze's north exit
-- (the grid ends a row south of his south face): every raider is a step or
-- two from its seat when he wakes, as theirs are (first attack a tick after).
function QD.raid._play_sotetseg_seat(st, b)
    local n = b.size or 5
    local role = st.role or 1
    if role == 2 then return b.x - 1, b.z end               -- (-1,0): west face, south end
    if role == 3 then return b.x + n - 1, b.z - 1 end       -- (4,-1): south face, east end
    return b.x + n, b.z + n - 1                             -- (5,4): east face, north end
end

function QD.raid._play_sotetseg_trio(st, v)
    local P, N = st.plan, st.numbers
    local intent = { want = {}, walk = nil, attack = false }
    st.sote = st.sote or { mazes = {}, balls = 0, death_balls = 0, press_log = {}, read_magic = {}, pray_sent = nil,
        hold = -1, ranged_hold = -1, death_land = -1, magic_ticks = 0, melee_ticks = 0,
        follows = {}, gathers = 0, gather_ticks = 0, seat_ticks = 0, aimed = 0 }
    local S = st.sote
    if v.me.level == P.under_level then
        -- the runner (the room chose this raider)
        S.in_maze = true
        if S.mz == nil then S.runs = (S.runs or 0) + 1 end
        return QD.raid._play_sotetseg_maze(st, v, intent)
    end
    if S.mz ~= nil and S.mz.back == nil then
        S.mz.back = v.tick
        st.teleport_until = v.tick + 2
        S.mz = nil
    end
    S.read_magic[v.tick] = v.lit.protectfrommagic == true
    local b = v.boss
    if b == nil then
        -- his combat form is gone: a maze (his idle form stands there) or his
        -- death.  The maze is not his death; the leader reads his death from
        -- the tick log's npc_death row, a member from the idle form being gone
        -- as well (three ticks: the library's own rule)
        local ir = QD.npc.state(P.idle[st.mode])
        if ir == "ok" or st.log then
            st.boss_gone = 0
            -- the throw to the far end three ticks after the proc (W:799)
            st.teleport_until = v.tick + 6
        end
        if ir == "ok" then
            S.in_maze = true
            return QD.raid._play_sotetseg_follow(st, v, intent)
        end
        return intent
    end
    if S.fw ~= nil then
        S.fw.done = S.fw.done or v.tick
        S.fw = nil
    end
    -- raid seam42: the fight's phase (0 before the first maze, +1 each time
    -- his combat form is back after one), the elder maul's key
    if S.in_maze then
        S.in_maze = nil
        S.phase = (S.phase or 0) + 1
        -- raid seam51: the tick the phase began (the maul's opener key)
        S.phase_start = v.tick
    end
    local function protect_press(protect)
        P.walk_prayers[1] = protect
        intent.want[protect] = true
        if S.pray_sent ~= nil and S.pray_sent.name == protect and v.tick - S.pray_sent.tick <= 2 then
            v.lit[protect] = true
        end
        if v.lit[protect] ~= true and v.prayer > 0 then
            S.pray_sent = { name = protect, tick = v.tick }
            S.press_log[#S.press_log + 1] = { tick = v.tick, name = protect }
        end
    end
    -- WHAT FLIES AT THIS RAIDER (a homing projectile's dst is its target's
    -- live tile: world.lua banner), and the death ball at anyone
    local magic_in_air, ranged_in_air, death_land = false, false, nil
    local soonest, soonest_style = nil, nil
    local pr, projs = QD.world.projectiles(30)
    -- raid seam52 play_tob_sotetseg_last: THE LANDING WINDOW.  His ball lands
    -- on launch + floor(duration / 30) (seam51 s2, three names: "Primary =
    -- launch + floor(dur/30)"); a ricochet on that tick or the next, by the
    -- pid order (45 of 45 / 31 of 35).  The old estimate, ceil(cycles_left /
    -- 30), dated a ball a tick late whenever its flight was not a whole
    -- number of ticks: seam51 _play t34, a primary of 136 cycles at p2 dated
    -- t39 against a ricochet dated t38, so Protect from Missiles was held over
    -- t38, the primary landed on it, the protections went for five ticks
    -- (the magic press REFUSED on t38) and his t39 melee hit 41 unprayed: the
    -- arrival double ball on every name.  Now each ball carries [lo, hi]:
    -- lo = this tick + floor(cycles_left / 30), hi = lo for his ball (thrown
    -- from his own footprint), lo + 1 for a ricochet; the prayer for next tick
    -- is the colour of the ball that can land on it, his ball first when a
    -- ricochet's late tick and his ball's tick coincide (his is the sure one).
    local bn = b.size or 5
    local function from_him(p)
        return p.src_x ~= nil and p.src_x >= b.x and p.src_x < b.x + bn and p.src_z >= b.z and p.src_z < b.z + bn
    end
    if pr == "ok" then
        for _, p in ipairs(projs) do
            local lo = v.tick + math.floor((p.cycles_left or 0) / 30)
            local primary = from_him(p)
            local hi = primary and lo or lo + 1
            local land = lo
            -- a homing shot's dst is the target's tile as the client last drew
            -- it, a tick behind a raider who is walking: within a tile is this
            -- raider's (the seats are 3 or more apart; on the front tile all
            -- three share every shot anyway)
            local mine = math.abs(p.dst_x - v.me.x) <= 1 and math.abs(p.dst_z - v.me.z) <= 1
            -- raid seam49: the projectile's own target when the client knows
            -- this raider's pid (MAP_PROJANIM's target, `-(pid) - 1` for a
            -- player: src/world/entity_projectile.h).  The tile test above took
            -- a ball at a raider who was running to its seat after a maze (dst
            -- a tick behind, two tiles away) as nobody's, and the ricochet of
            -- the other colour aimed at the raider beside it as its own.
            if st.my_pid ~= nil and p.target ~= nil and p.target < 0 then
                mine = (p.target == -st.my_pid - 1)
            end
            if (p.spotanim_id == P.proj_magic or p.spotanim_id == P.proj_ranged) and mine then
                local style = (p.spotanim_id == P.proj_magic) and "protectfrommagic" or "protectfrommissiles"
                -- the ball that lands FIRST after this tick decides (a press
                -- takes on the server's next tick, and the prayer is read at
                -- the landing: S tob_sote_impact)
                local nxt = v.tick + 1
                local key
                if hi < nxt then
                    key = 100000 + lo
                elseif lo <= nxt then
                    key = (primary and 0 or 10) + lo
                else
                    key = 1000 + lo * 2 + (primary and 0 or 1)
                end
                if soonest == nil or key < soonest then soonest, soonest_style = key, style end
            end
            if p.spotanim_id == P.proj_magic and mine then
                magic_in_air = true
            elseif p.spotanim_id == P.proj_ranged and mine then
                ranged_in_air = true
            elseif p.spotanim_id == P.proj_death then
                if death_land == nil or land < death_land then death_land = land end
                if S.death_seen ~= b.seq_tick then
                    S.death_seen = b.seq_tick
                    S.death_balls = S.death_balls + 1
                end
            end
        end
    end
    if death_land ~= nil and death_land > S.death_land then S.death_land = death_land end
    local range = QD.raid._play_sotetseg_range(b, v.me.x, v.me.z)
    local adjacent = range <= 1
    -- raid seam51 play_tob_sotetseg_whole: HIS ATTACK CLOCK.  He attacks every
    -- 5 ticks (blert boss.cadence 5 [5-5], 20 rooms) and 10 after a death ball
    -- (S tob_sote_attack: "his next attack is TEN ticks after it"); an attack
    -- is dated to the plan tick that first SEES its seq (raid seam51 Verzik:
    -- the seen tick equalled the tick log's in every sample, the row's age
    -- was a tick early).  His melee is decided and its prayer read on the
    -- tick it is SENT (the owner's ruling), so Protect from Melee must be lit
    -- ON his next attack tick N, pressed on N-1.
    if (b.seq_id == P.seq_melee or b.seq_id == P.seq_ball) and b.seq_tick ~= nil and b.seq_tick ~= S.atk_seq_tick then
        S.atk_seq_tick = b.seq_tick
        S.last_attack = v.tick
        S.next_attack = v.tick + 5
        S.attacks_seen = (S.attacks_seen or 0) + 1
    end
    if S.last_attack ~= nil and S.death_seen ~= nil and S.death_seen == S.atk_seq_tick and S.next_attack == S.last_attack + 5 then
        S.next_attack = S.last_attack + 10
    end
    local due = S.next_attack ~= nil and v.tick + 1 >= S.next_attack
    -- THE PRAYER FOR NEXT TICK (a press on tick t is in force on t+1).
    -- A ball or a ricochet at this raider wins: unprayed it "takes the
    -- victim's protection prayers away for five ticks" (S tob_sotetseg.rs2:15,
    -- ~prayer_block_protection at the impact), and every attack in those five
    -- ticks lands unprayed (seam51 s1: a melee-first rule on his due tick
    -- dropped the ricochet's colour on its impact, sva p0/p1 t105, and the
    -- refused presses held both raiders at no protection for 20 ticks).
    -- The ricochets are launched at A+2 and land at A+4 or A+5, his next
    -- attack's tick (seam51 s2, three names: launch + floor(duration/30) when
    -- this raider's pid is above the pid it bounced off, 45 of 45; a tick later
    -- when below, 31 of 35: its impact queue runs in the next tick's player
    -- phase.  A schedule built on that rule, seam51 s3b, lost the colour on
    -- the client's pid and was withdrawn: DRIVER_NOTES), so the melee
    -- on that tick is the one-in-three roll a raider takes on the wrong
    -- prayer (S tob_sote_attack: a random target, melee a licence at range 1).
    -- The colour is held while the ball is listed and one tick after it
    -- vanishes (the client ends a flight up to a tick before the server's
    -- impact).  Otherwise Protect from Melee near him, Magic away from him.
    if magic_in_air then S.hold = math.max(S.hold, v.tick + 1) end
    if ranged_in_air then S.ranged_hold = math.max(S.ranged_hold, v.tick + 1) end
    local protect = "protectfrommagic"
    if soonest_style ~= nil then
        protect = soonest_style
        S.aimed = S.aimed + 1
    elseif v.tick <= S.hold and v.tick <= S.ranged_hold then
        protect = (S.hold >= S.ranged_hold) and "protectfrommagic" or "protectfrommissiles"
        S.aimed = S.aimed + 1
    elseif v.tick <= S.hold then
        protect = "protectfrommagic"
        S.aimed = S.aimed + 1
    elseif v.tick <= S.ranged_hold then
        protect = "protectfrommissiles"
        S.aimed = S.aimed + 1
    elseif range <= 3 then
        protect = "protectfrommelee"
        if due then S.melee_due = (S.melee_due or 0) + 1 end
    end
    if protect == "protectfrommagic" then S.magic_ticks = S.magic_ticks + 1 end
    if protect == "protectfrommelee" then S.melee_ticks = S.melee_ticks + 1 end
    protect_press(protect)
    intent.want.piety = true
    if v.lit.piety ~= true then
        if S.piety_sent ~= nil and v.tick - S.piety_sent <= 2 then
            v.lit.piety = true
        elseif v.prayer > 0 then
            S.piety_sent = v.tick
        end
    end
    -- WHERE TO STAND: the seat, or the front tile while the death ball is due
    local n = b.size or 5
    local sx, sz = QD.raid._play_sotetseg_seat(st, b)
    local gathering = S.death_land >= v.tick - 1 and S.death_land - v.tick <= P.gather_lead
    if gathering then
        sx, sz = b.x + math.floor(n / 2), b.z - 1
        if S.gather_for ~= S.death_land then
            S.gather_for = S.death_land
            S.gathers = S.gathers + 1
        end
        S.gather_ticks = S.gather_ticks + 1
    end
    local at_seat = v.me.x == sx and v.me.z == sz
    local moved = st.last_me ~= nil and (st.last_me.x ~= v.me.x or st.last_me.z ~= v.me.z)
    local key = sx * 100000 + sz
    if S.seat_key ~= key then
        S.seat_key = key
        S.seat_misses = 0
    end
    -- a seat the server stopped short of three times is not a tile it will
    -- stand the raider on: the attack press from where it stands (it paths
    -- the raider into his range), recorded
    local given_up = S.seat_misses >= 3
    if given_up and S.gave_up_on ~= key then
        S.gave_up_on = key
        S.seats_given_up = (S.seats_given_up or 0) + 1
    end
    if at_seat or given_up then
        if at_seat then S.seat_ticks = S.seat_ticks + 1 end
        intent.attack = true
    else
        local same = st.walk_target ~= nil and st.walk_target.x == sx and st.walk_target.z == sz
        if not same or (not moved and S.walk_sent ~= nil and v.tick - S.walk_sent >= 2) then
            intent.walk = { x = sx, z = sz }
            S.walk_sent = v.tick
            S.walks = (S.walks or 0) + 1
            if same then S.seat_misses = S.seat_misses + 1 end
        end
    end
    -- THE SUPPLIES (the Entry rule with Normal's numbers): a melee per attack
    -- in his range, prayed or not by the prayer this tick asks for; a ball
    -- and a melee unprayed while the protections are disabled; the death
    -- ball's share when it lands inside the horizon (all three on the front
    -- tile: W:794 split, yt_4i4lv-srJkw.md:95)
    if st.refusals > (S.refusals_seen or 0) then
        S.refusals_seen = st.refusals
        S.disabled_until = v.tick + 6
    end
    local disabled = S.disabled_until ~= nil and v.tick <= S.disabled_until
    local function threat(h)
        -- raid seam42 play_tob_sotetseg_follows_blert: ONE attack of his, not
        -- one per five ticks of the horizon.  He swings at one raider a
        -- cycle and the food comes between (eat delay 3), so the hit to keep
        -- above is the next one; the horizon's two worst-case melees made
        -- every raider eat and brew at 80-115 of 99 (seam42 s1: 12 eats and
        -- 13 drinks a raider, the brews' Attack drain took the scythe's
        -- accuracy from 0.87 to 0.29) where the Blert trios eat at 29% and
        -- 41.5% of their hitpoints (sotetseg_normal_3.json
        -- role.melee1/2.eat_at_hp_pct) and lose 92-108 in the whole room.
        local attacks = 1
        local per = 0
        if adjacent then per = (protect == "protectfrommelee") and N.melee_prayed or N.melee end
        -- raid seam50: ONE attack while the prayers are out as well (his
        -- ball or his melee, never both in one cycle): the sum had every
        -- raider eat at 82-95 of 99 (seam49 s2: p2 ate 14 times in maze2's
        -- phase) where the Blert trios eat at 29% and 41.5%
        if disabled then per = math.max(N.ball, adjacent and N.melee or 0) end
        local total = attacks * per
        if S.death_land >= v.tick and S.death_land - v.tick <= h then
            total = total + math.floor(N.death / st.party) + 1
        end
        return total
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    -- raid seam51 play_tob_sotetseg_whole: THE BOOST BACK, Maiden's rule
    -- ("the super combat again under 108": raid_play_tob_maiden.lua; a
    -- boost is set from the base level, wiki Super combat potion "+5 +15%",
    -- 118 at 99).  A brew's drain (wiki Saradomin brew: -10% -2 each dose)
    -- put right by a restore leaves the scythe at 99 for the rest of the
    -- room: seam50 s1's splats were 0 on 26% of hits before the first maze
    -- and 42-44% after, where the reference's trios deal ~33 a swing
    -- (output.phase.*.boss_hp_per_tick over role.*.phase.*.attacks_boss).
    if intent.drink == nil and v.tick - (st.last_drink or -1000) >= QD.RAID_PLAY_DRINK_DELAY then
        local _, at = QD.skill.read("attack")
        local _, sg = QD.skill.read("strength")
        local combat = nil
        for _, dose in ipairs({ "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat" }) do
            local cr, n = QD.inv.count(dose)
            if combat == nil and cr == "ok" and (tonumber(n) or 0) > 0 then combat = dose end
        end
        if combat ~= nil and at ~= nil and sg ~= nil and ((at.level or 999) < 108 or (sg.level or 999) < 108) then
            intent.drink = combat
            S.reboosts = (S.reboosts or 0) + 1
        end
    end
    -- raid seam42 play_tob_sotetseg_follows_blert: THE ELDER MAUL, once a
    -- phase.  The Normal trios on Blert swing ELDER_MAUL once in a phase per
    -- raider (sotetseg_normal_3.json weapons: melee1 start 15 of 19 rooms,
    -- maze1 13, maze2 12), and their scythes then deal ~30 a swing (1000
    -- hitpoints a phase from role.*.phase.start.attacks_boss 10 each) where
    -- ours dealt 22 into his full Defence 200.  The special "reduces the
    -- target's Defence by 35% of its current level" on a hit
    -- (pvm_elder_maul.rs2:4-41) and costs 500 energy; it is armed from the
    -- orb like Maiden's hammer (raid_play_tob_maiden.lua opener), proved by
    -- the energy it spends, and the scythe goes back on the tick after.
    local phase = S.phase or 0
    S.em = S.em or { phases = {}, log = {} }
    local em = S.em.phases[phase]
    local _, energy = QD.var.varp("varp300_sa_energy")
    energy = tonumber(energy) or 0
    -- raid seam51 play_tob_sotetseg_whole: THE MAUL IN TWO STEPS, Maiden's
    -- opener shape (raid_play_tob_maiden.lua THE OPENER, proved there): the
    -- maul goes on in one block, the special is armed from the orb with the
    -- attack press on the next tick, the scythe goes back once its 500 is
    -- spent.  The one-block arm missed: seam50 s1 p1 put the maul on at t110,
    -- swung it plain (seq 7516) at t113 and specced at t119, three attacks
    -- where a scythe would have swung four.  It OPENS the phase (the first
    -- attack after the room's start or a maze, so every scythe swing after
    -- it meets the lowered Defence; the reference's trios swap 6-20 ticks into
    -- a phase, react.phase.maze1.*.swap) or, when the phase's first swing went
    -- by, it goes on the tick after a scythe swing, four before the next.
    local just_swung = st.engaged and st.last_swing ~= nil and v.tick - st.last_swing <= 1
    local fresh = st.last_swing == nil or st.last_swing < (S.phase_start or 0)
    -- raid seam51: TWO SPECIALS A PHASE, shared out.  His Defence is back at
    -- its level after every maze (seam51 s1 svb: zero splats 17% in the start
    -- phase after three specials, 25% after maze 1's three, 48% after maze 2
    -- with none: the energy, 1000 a raider and 10% a 50 ticks back, was spent
    -- by then; the reference's trios swing the maul in 12-15 of 19 rooms in
    -- EVERY phase, sotetseg_normal_3.json weapons ELDER_MAUL).  Each raider
    -- owns two of the three phases (role 1: start and maze 1, role 2: start and
    -- maze 2, role 3: maze 1 and maze 2), and specs in another only with the
    -- energy for its own still to come.
    local own = { [1] = { [0] = true, [1] = true }, [2] = { [0] = true, [2] = true }, [3] = { [1] = true, [2] = true } }
    local mine_phases = own[st.role or 1] or own[1]
    local still = 0
    for later = phase + 1, 2 do
        if mine_phases[later] then still = still + 1 end
    end
    local spec_ok = (mine_phases[phase] == true and energy >= 500) or energy >= 500 * (1 + still)
    if em == nil and not gathering and intent.eat == nil and spec_ok and st.party > 1
        and (fresh or (at_seat and just_swung)) then
        em = { stage = "equip", at = v.tick, fresh = fresh }
        S.em.phases[phase] = em
        intent.gear = { "elder_maul" }
        intent.attack = false
        return intent
    end
    if em ~= nil and em.stage ~= "done" then
        if em.stage == "equip" then
            -- armed from the seat (walking in, the walk goes on: no attack
            -- press, so nothing swings the maul plain on the way)
            if not (at_seat or given_up) then
                intent.attack = false
                return intent
            end
            em.stage, em.arm, em.energy0 = "swing", v.tick, energy
            intent.spec = true
            intent.attack = true
            st.engaged = false
            return intent
        elseif em.stage == "swing" then
            if energy <= em.energy0 - 500 or v.tick - em.arm > 12 then
                em.fired = (energy <= em.energy0 - 500) and v.tick or nil
                em.stage = "done"
                intent.gear = { "scythe_of_vitur" }
                st.engaged = false
                S.em.log[#S.em.log + 1] = "phase " .. phase .. (em.fresh and " opener" or "") .. " on t" .. em.at
                    .. (em.fired and (" fired t" .. em.fired) or " gave up t" .. v.tick) .. (em.rearm and (" rearmed " .. em.rearm) or "")
            else
                local _, armed = QD.var.varp("varp301_sa_attack")
                if tonumber(armed) == 0 and v.tick - em.arm >= 2 and (em.rearm or 0) < 3 then
                    em.rearm = (em.rearm or 0) + 1
                    intent.spec = true
                end
                intent.attack = true
                return intent
            end
        end
    end
    intent.attack = QD.raid._play_attack(st, v, intent.attack)
    return intent
end

-- THE RAIDERS WHO READ THE GLOW (the trio's two who are not chosen).  "The
-- remaining players will be forcibly teleported to the other end of the
-- arena" (W:799); "players in the real world will see the tile the player in
-- the Shadow Realm is standing on with a red glow" (W:801); "One person will
-- be able to see a path through the maze. They need to run through it
-- first. This will highlight the tile they click on to the other players in
-- the raid. Everyone else just needs to follow the path. For beginners, only
-- use straight line movement" (yt_4i4lv-srJkw.md:97).  The glow is ONE tile
-- at a time (S tob_sote_mirror lights the runner's tile and puts the last one
-- out), so the raider keeps every tile it saw lit, in order; two tiles seen
-- one after the other lie on one straight run (the runner walks corner to
-- corner), and the path between them is the run.  The raider waits off the
-- grid's south edge until the glow has reached the last row, checks the path
-- has the maze's shape (S tob_sote_path_has: one tile on an even row, a run
-- on an odd one), then walks it corner to corner and steps off the north
-- edge.  A wrong tile is "a blast ... 6.67% of the player's current Hitpoints
-- + 15 every tick" to everyone beside it (W:803): a path that fails the
-- check is never walked (the maze still ends when the runner steps off: S
-- tob_sote_grid_occupied counts only raiders ON a grid).
-- raid seam52 play_tob_sotetseg_last: THE TILES BETWEEN TWO GLOWS.
--   QD.raid._play_sotetseg_gap(last, now, gz, consecutive) -> points, certain
-- `last` and `now` are {x, z} glows in read order, `gz` the grid's south row,
-- `consecutive` true when the two reads were a tick apart.  `points` are the
-- corners of the straight runs from `last` to `now` (first `last`, last
-- `now`); `certain` is false when the maze's shape (an even row is one tile,
-- an odd row the run between two of them: S tob_sote_path_has) does not name
-- the turn -- the corner on an odd row two rows on, or more rows than one tick
-- of the runner covers -- and `points` is then the parity guess, never to be
-- walked before the glows are complete.
function QD.raid._play_sotetseg_gap(last, now, gz, consecutive)
    assert(last)
    assert(now)
    assert(gz)
    local lrow, rrow = last[2] - gz, now[2] - gz
    local dist = math.abs(now[1] - last[1]) + math.abs(now[2] - last[2])
    local function parity()
        if lrow % 2 == 0 then
            return { last, { last[1], now[2] }, now }
        end
        return { last, { now[1], last[2] }, now }
    end
    if consecutive and dist <= 2 then
        -- one tick of the runner: a straight run, or the one corner the
        -- row's parity allows
        if last[1] == now[1] or last[2] == now[2] then
            return { last, now }, true
        end
        return parity(), true
    elseif lrow == rrow and lrow % 2 == 1 then
        return { last, now }, true
    elseif rrow == lrow + 1 then
        return parity(), true
    elseif rrow == lrow + 2 and lrow % 2 == 0 then
        return { last, { last[1], last[2] + 1 }, { now[1], last[2] + 1 }, now }, true
    end
    -- two-way: the parity guess, so later glows join on; never walked past
    return parity(), false
end

-- raid seam52 play_tob_sotetseg_last: THE POLL.  The follower's decide
-- (below) reads the glow at the top of the tick and sends its step at once
-- (api_drive.move_to, no block); then, while the glow is live and nothing
-- else is to be sent, it keeps reading on every frame until the tick turns,
-- so the loop wakes on the next tick with every glow the client was shown.
function QD.raid._play_sotetseg_follow(st, v, intent)
    local out = QD.raid._play_sotetseg_follow_read(st, v, intent)
    local fw = st.sote ~= nil and st.sote.fw or nil
    if fw ~= nil and fw.read_glow ~= nil and fw.off_north == nil and v.me.z < st.origin.z + st.plan.over_lz + st.plan.maze_rows
        and out.walk == nil and out.eat == nil and out.drink == nil then
        local t0 = v.tick
        local polls = 0
        QD.await({ level = function()
            local tr, now = QD.tick()
            if tr ~= "ok" or now ~= t0 then return true end
            polls = polls + 1
            if fw.complete == nil then fw.read_glow(now) end
            local wt = st.walk_target
            if fw.next_step ~= nil and wt ~= nil and #fw.samples > 0 and api_drive.move_to ~= nil then
                local mr, me = QD.world.tile()
                if mr == "ok" and me ~= nil and me.x == wt.x and me.z == wt.z then
                    local nx, nz = fw.next_step(me.x, me.z)
                    if nx ~= nil and (nx ~= me.x or nz ~= me.z) and api_drive.move_to(nx, nz) == "ok" then
                        st.walk_target = { x = nx, z = nz }
                        fw.early_steps = (fw.early_steps or 0) + 1
                    end
                end
            end
            return false
        end, note = "sotetseg follower: the glow and the next run on every frame of the tick" }, 2)
        fw.polls = (fw.polls or 0) + polls
    end
    return out
end

function QD.raid._play_sotetseg_follow_read(st, v, intent)
    local P, S = st.plan, st.sote
    st.boss_gone = 0
    P.walk_prayers[1] = "protectfrommagic"
    intent.want.protectfrommagic = true
    if v.lit.protectfrommagic ~= true and S.pray_sent ~= nil and S.pray_sent.name == "protectfrommagic" and v.tick - S.pray_sent.tick <= 2 then
        v.lit.protectfrommagic = true
    elseif v.lit.protectfrommagic ~= true and v.prayer > 0 then
        S.pray_sent = { name = "protectfrommagic", tick = v.tick }
        S.press_log[#S.press_log + 1] = { tick = v.tick, name = "protectfrommagic" }
    end
    -- Piety out (want leaves it off); an off is a toggle, so it is sent once
    -- and treated as read for two ticks (the runner's own guard)
    if v.lit.piety == true and S.piety_off ~= nil and v.tick - S.piety_off <= 2 then
        v.lit.piety = false
    elseif v.lit.piety == true then
        S.piety_off = v.tick
    end
    local fw = S.fw
    if fw == nil then
        fw = { n = #S.follows + 1, seen = v.tick, samples = {}, path = {}, path_n = 0, complete = nil, bad = nil,
            gaps = 0, walks = 0, order = nil, idx = 0, on_grid = 0, off_path = 0, off_north = nil, start_walk = nil,
            live = {}, live_steps = 0 }
        S.follows[#S.follows + 1] = fw
        S.fw = fw
    end
    local gx, gz = st.origin.x + P.over_lx, st.origin.z + P.over_lz
    local cols, rows = P.maze_cols, P.maze_rows
    local function on_grid(x, z) return x >= gx and x < gx + cols and z >= gz and z < gz + rows end
    local function add(x, z)
        local k = x * 100000 + z
        if not fw.path[k] then
            fw.path[k] = true
            fw.path_n = fw.path_n + 1
        end
    end
    local function run_to(ax, az, bx, bz)
        local dx = (bx > ax and 1) or (bx < ax and -1) or 0
        local dz = (bz > az and 1) or (bz < az and -1) or 0
        local x, z = ax, az
        add(x, z)
        while x ~= bx or z ~= bz do
            x, z = x + dx, z + dz
            add(x, z)
        end
    end
    -- READ THE GLOW
    -- raid seam52 play_tob_sotetseg_last: EVERY TICK, AND NO GUESS WALKED.
    -- The arena shows one lit tile, the runner's ("The players in the arena
    -- will only be able to see the current location of the transported
    -- player": S tob_sote_mirror), and the server moves it every tick the
    -- runner moves (seam51 _play ticklog: one loc_set 33035 a tick, t90-t108).
    -- Read on consecutive ticks, two glows are one tick of the runner apart
    -- (at most two tiles, never round a corner inside a tick: the same log),
    -- and the maze's shape (an even row is one tile, an odd row the run
    -- between two of them: S tob_sote_path_has) names the tiles between.  A
    -- read the loop did not take (a block that ran over a tick) can leave a
    -- gap the shape does not settle: the corner on an odd row two rows on
    -- may be either end (seam51 blasts at (19,27) and (16,31) were exactly
    -- that parity guess).  Such a gap is recorded and the follower never
    -- walks past the last tile before it (QD.raid._play_sotetseg_follow's
    -- live block caps the reach there).
    -- raid seam52 s2: a party member reads its world as the packets reach
    -- it, and the boundary its loop wakes on can come before or after this
    -- tick's loc changes: read once a tick, two glows a tick apart were
    -- three and four tiles apart on every name (read skips 0, gaps 4-6).  The
    -- read is a function, taken at the top of the tick and again on every
    -- frame until the tick turns (QD.raid._play_sotetseg_follow's poll
    -- below), so a glow lit for one tick is seen whenever it arrived.
    local function read_glow(now)
    if fw.complete ~= nil then return end
            local lr, _, locs = QD.world.loc_copies(P.path_loc, 40)
            local read_any = false
            -- raid seam52 s1: the client can list more than one lit tile on a read
            -- (the tile the runner left still lit beside the new one) while the
            -- server lights one a tick (ticklog: one loc_set 33035 a tick); taken
            -- in pool order the old one came after the new and the route doubled
            -- back, a false two-way gap on every name (read skips 0, gaps 2-4).  A
            -- path never crosses itself, so a glow already read is stale; the new
            -- ones are taken nearest the last first.
            fw.glow_seen = fw.glow_seen or {}
            local fresh = {}
            if lr == "ok" and locs ~= nil then
                local lit_n = 0
                for _, r in ipairs(locs) do
                    if r.level == 0 and on_grid(r.x, r.z) then
                        lit_n = lit_n + 1
                        read_any = true
                        if not fw.glow_seen[r.x * 100000 + r.z] then fresh[#fresh + 1] = r end
                    end
                end
                if lit_n > 1 then fw.multi_lit = (fw.multi_lit or 0) + 1 end
                local tail = fw.samples[#fw.samples]
                if tail ~= nil and #fresh > 1 then
                    table.sort(fresh, function(a1, a2)
                        return math.abs(a1.x - tail[1]) + math.abs(a1.z - tail[2]) < math.abs(a2.x - tail[1]) + math.abs(a2.z - tail[2])
                    end)
                end
            end
            if #fresh > 0 then
                for _, r in ipairs(fresh) do
                    fw.glow_seen[r.x * 100000 + r.z] = true
                    do
                        local last = fw.samples[#fw.samples]
                        if last == nil or last[1] ~= r.x or last[2] ~= r.z then
                            fw.samples[#fw.samples + 1] = { r.x, r.z, now }
                            fw.route = fw.route or {}
                            local function route_to(bx, bz)
                                local tail = fw.route[#fw.route]
                                if tail == nil then
                                    fw.route[1] = { bx, bz }
                                    return
                                end
                                local ddx = (bx > tail[1] and 1) or (bx < tail[1] and -1) or 0
                                local ddz = (bz > tail[2] and 1) or (bz < tail[2] and -1) or 0
                                local x, z = tail[1], tail[2]
                                while x ~= bx or z ~= bz do
                                    x, z = x + ddx, z + ddz
                                    fw.route[#fw.route + 1] = { x, z }
                                end
                            end
                            local function via(points)
                                local ax, az = points[1][1], points[1][2]
                                for i = 2, #points do
                                    run_to(ax, az, points[i][1], points[i][2])
                                    route_to(points[i][1], points[i][2])
                                    ax, az = points[i][1], points[i][2]
                                end
                            end
                            if last == nil then
                                route_to(r.x, r.z)
                                add(r.x, r.z)
                            else
                                local consecutive = fw.prev_read ~= nil and now - fw.prev_read <= 1
                                local dist = math.abs(r.x - last[1]) + math.abs(r.z - last[2])
                                local points, certain = QD.raid._play_sotetseg_gap(last, { r.x, r.z }, gz, consecutive)
                                if not consecutive or dist > 2 then fw.gaps = fw.gaps + 1 end
                                if not certain then
                                    fw.ambiguous = (fw.ambiguous or 0) + 1
                                    if fw.uncertain_at == nil then fw.uncertain_at = #fw.route; fw.gap_held = 0 end
                                end
                                via(points)
                            end
                            if r.z - gz == rows - 1 then fw.complete = now end
                        end
                    end
                end
            end
            if read_any then
                if fw.prev_read ~= nil and now - fw.prev_read > 1 then
                    fw.read_skips = (fw.read_skips or 0) + (now - fw.prev_read - 1)
                end
                fw.prev_read = now
            end
    end
    fw.read_glow = read_glow
    read_glow(v.tick)
    if fw.complete ~= nil and not fw.shape_checked then
        fw.shape_checked = true
        if fw.complete ~= nil then
            -- the maze's shape: every row lit, an even row on one tile
            local ok = true
            for row = 0, rows - 1 do
                local count = 0
                for col = 0, cols - 1 do
                    if fw.path[(gx + col) * 100000 + (gz + row)] then count = count + 1 end
                end
                if count == 0 or (row % 2 == 0 and count ~= 1) then ok = false end
            end
            fw.bad = not ok
            -- raid seam49: a gap the glow left two-way (three tiles or more:
            -- a read missed while the runner ran two a tick) may hold the turn
            -- on either row; the parity guess walked onto a dark tile is a
            -- blast every tick (W:803).  Such a path is not walked: the raider
            -- stays off the grid and the maze ends when the runner steps off
            -- (S tob_sote_grid_occupied), the documented fallback above.
            if (fw.ambiguous or 0) > 0 then fw.bad = true end
            if ok then
                local first = fw.samples[1]
                local order, seen = { { first[1], first[2] } }, {}
                local cx, cz = first[1], first[2]
                seen[cx * 100000 + cz] = true
                for _ = 1, 120 do
                    local nx, nz = nil, nil
                    for _, d in ipairs({ { 0, 1 }, { -1, 0 }, { 1, 0 } }) do
                        local k = (cx + d[1]) * 100000 + (cz + d[2])
                        if nx == nil and fw.path[k] and not seen[k] then nx, nz = cx + d[1], cz + d[2] end
                    end
                    if nx == nil then break end
                    cx, cz = nx, nz
                    seen[cx * 100000 + cz] = true
                    order[#order + 1] = { cx, cz }
                end
                fw.order = order
                if #order ~= fw.path_n or cz - gz ~= rows - 1 then fw.bad = true end
            end
        end
    end
    -- eat up while nothing can hit (no damage while the maze is on: W:799)
    local function threat(h) return 60 end
    -- raid seam52: nothing but the step while the glow is read.  A block that
    -- waits on an eat or a drink can run over a tick, and the tick it eats is
    -- a glow not read (seam51 _play: both followers ate on t104, the tick of
    -- the second guessed corner).  Nothing hits a follower walking the lit
    -- route, so the bite waits for the last row.
    local reading = #fw.samples > 0 and fw.complete == nil
    if reading then
        fw.supplies_held = (fw.supplies_held or 0) + 1
    else
        intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    end
    -- the stall: S p_stun 5 from the proc (the idle form shows on the proc tick)
    if v.tick < fw.seen + 5 then
        return intent
    end
    local moved = st.last_me ~= nil and (st.last_me.x ~= v.me.x or st.last_me.z ~= v.me.z)
    -- raid seam52: while the glow is read the step is sent on its own, the
    -- same api_drive.move_to a together block's walk sends (pointer.lua
    -- QD._together_move), without the block's wait for the tile to change:
    -- that wait is what can carry the loop over a tick and lose a glow.  The
    -- next tick's read of our own tile is the confirmation.
    local function go(x, z)
        local same = st.walk_target ~= nil and st.walk_target.x == x and st.walk_target.z == z
        if not same or not moved then
            fw.walks = fw.walks + 1
            if #fw.samples > 0 and api_drive.move_to ~= nil and api_drive.move_to(x, z) == "ok" then
                st.walk_target = { x = x, z = z }
                st.engaged = false
                fw.direct_walks = (fw.direct_walks or 0) + 1
                st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
                return
            end
            intent.walk = { x = x, z = z }
        end
    end
    if fw.off_north ~= nil or v.me.z >= gz + rows then
        -- off the north edge: done; the fight resumes when he wakes
        fw.off_north = fw.off_north or v.tick
        return intent
    end
    local first = fw.samples[1]
    if on_grid(v.me.x, v.me.z) then
        fw.on_grid = fw.on_grid + 1
        if not fw.path[v.me.x * 100000 + v.me.z] then fw.off_path = fw.off_path + 1 end
    end
    if first == nil then
        return intent
    end
    local wx, wz = first[1], gz - 1
    -- raid seam50 play_tob_sotetseg_whole: THE LIVE FOLLOW, as the Blert
    -- trios walk it.  Their two thrown raiders step onto the grid 12-14 ticks
    -- after the proc and off its north edge at 26-28, a tick or two behind
    -- the runner (build/blert/sotetseg 16ff015b: Xvok and drek on the grid
    -- from +14, row 15 at +26/+28; the runner back beside him at +28), and
    -- the maze is over 28 [14-47] ticks after the proc (blert_api/
    -- sote_maze.csv, 26 mazes) with his first attack taken a tick later in
    -- 25 of 26.  Ours waited for the whole path and then walked it: 46-58.
    -- The walk is the route in lit order, `follow_lag` tiles behind the newest
    -- glow while the runner is on the grid, to its end once the last row is
    -- lit, then one step north.  Each target is the far end of the straight
    -- run from the tile the raider stands on, so a target extended mid-run is
    -- on the same line (the server cannot cut a corner on it); the arena
    -- tornado (S tob_sote_tornado_step, one tile a tick from the path's start)
    -- never catches a raider who runs the path two tiles a tick behind the
    -- runner.
    -- raid seam52 s3: THE NEXT STRAIGHT RUN from a route tile, for the poll.
    -- A member sees its own arrival at a corner whenever the packet lands,
    -- often after the boundary its loop woke on, and so stood a tick at every
    -- corner (s3 _play maze 1: t88-90, t94-95, t98-99, t100-101, t102-103;
    -- the glow complete t128, off the north edge t138; mazes 35-47 ticks
    -- against 30-36).  The poll sends the next run on the frame the arrival
    -- shows, inside the same tick.
    fw.next_step = function(mx, mz)
        local route = fw.route
        if route == nil or #route == 0 or fw.uncertain_at ~= nil then return nil end
        local done = fw.complete ~= nil
        local reach = #route - (done and 0 or P.follow_lag)
        local here = nil
        for i = #route, 1, -1 do
            if here == nil and route[i][1] == mx and route[i][2] == mz then here = i end
        end
        if here == nil then return nil end
        if done and here == #route then return mx, mz + 1 end
        if here >= reach then return nil end
        local dx = route[here + 1][1] - route[here][1]
        local dz = route[here + 1][2] - route[here][2]
        local j = here + 1
        while j < reach and route[j + 1][1] - route[j][1] == dx and route[j + 1][2] - route[j][2] == dz do
            j = j + 1
        end
        return route[j][1], route[j][2]
    end
    if P.follow_live and fw.route ~= nil and #fw.route > 0 then
        local route = fw.route
        local done = fw.complete ~= nil
        local reach = #route - (done and 0 or P.follow_lag)
        -- raid seam52: NEVER PAST A TWO-WAY GAP.  A follower not yet on the
        -- grid when one is read stays off it: the maze ends when the runner
        -- steps off and nobody stands on either grid (S tob_sote_grid_occupied),
        -- the documented fallback.  One already on it walks to the last tile
        -- before the gap and waits there for the glows to finish; only then,
        -- with nothing more to read, does it take the corner the shape
        -- makes likelier (counted: guessed).
        if fw.uncertain_at ~= nil and (fw.stayed_off or (fw.on_grid == 0 and not on_grid(v.me.x, v.me.z))) then
            fw.stayed_off = true
            if v.me.x ~= wx or v.me.z ~= wz then go(wx, wz) end
            return intent
        end
        if fw.uncertain_at ~= nil then
            -- one tick for the next glow, then the likelier corner: held
            -- longer, the tornado walking the path from its start reached the
            -- holder (seam52 s1 _play t104: 45 and 40 after 10 ticks held)
            if done or (fw.gap_held or 0) >= 1 then
                fw.uncertain_at = nil
                fw.guess_walked = (fw.guess_walked or 0) + 1
            else
                reach = math.min(reach, fw.uncertain_at)
            end
        end
        local here = nil
        for i = #route, 1, -1 do
            if here == nil and route[i][1] == v.me.x and route[i][2] == v.me.z then here = i end
        end
        if here == nil then
            if v.me.x == wx and v.me.z == wz then
                if reach >= 1 then
                    fw.start_walk = fw.start_walk or v.tick
                    go(route[1][1], route[1][2])
                end
            elseif not on_grid(v.me.x, v.me.z) then
                go(wx, wz)
            else
                -- on the grid off the route (a step the server took short):
                -- back to the nearest route tile at or before the reach
                local best, bd = nil, 999
                for i = 1, math.max(1, reach) do
                    local d = math.max(math.abs(route[i][1] - v.me.x), math.abs(route[i][2] - v.me.z))
                    if d < bd then best, bd = i, d end
                end
                go(route[best][1], route[best][2])
            end
            return intent
        end
        fw.live_steps = fw.live_steps + 1
        if done and here == #route then
            go(v.me.x, v.me.z + 1)
            return intent
        end
        if here >= reach and fw.uncertain_at ~= nil then
            fw.held = (fw.held or 0) + 1
            fw.gap_held = (fw.gap_held or 0) + 1
        end
        if here < reach then
            local dx = route[here + 1][1] - route[here][1]
            local dz = route[here + 1][2] - route[here][2]
            local j = here + 1
            while j < reach and route[j + 1][1] - route[j][1] == dx and route[j + 1][2] - route[j][2] == dz do
                j = j + 1
            end
            go(route[j][1], route[j][2])
        end
        return intent
    end
    -- raid seam42: on the grid, a new walk is sent only from the end of the
    -- last one.  Every target is a straight run from the tile it was chosen
    -- on; a target changed mid-run is routed by the server from wherever the
    -- raider has got to, and its shortest route cuts the corner onto a dark
    -- tile (seam42 s3 p1 t101-103: (17,25) -> (15,25) -> (15,27), a 21
    -- blast).  Still on the way (moved this tick, not there yet): wait.
    if on_grid(v.me.x, v.me.z) and moved and st.walk_target ~= nil
        and (v.me.x ~= st.walk_target.x or v.me.z ~= st.walk_target.z) then
        return intent
    end
    if fw.complete == nil or fw.bad or fw.order == nil then
        -- wait off the grid's south edge below the path's first tile (or, on
        -- the grid already, where the glow last let us stand)
        if not on_grid(v.me.x, v.me.z) and (v.me.x ~= wx or v.me.z ~= wz) then go(wx, wz) end
        return intent
    end
    local here = nil
    for i, o in ipairs(fw.order) do
        if o[1] == v.me.x and o[2] == v.me.z then here = i end
    end
    local n = #fw.order
    if here == nil then
        if v.me.x == wx and v.me.z == wz then
            fw.start_walk = fw.start_walk or v.tick
            go(fw.order[1][1], fw.order[1][2])
        elseif not on_grid(v.me.x, v.me.z) then
            go(wx, wz)
        end
        return intent
    end
    fw.idx = here
    if here == n then
        go(v.me.x, v.me.z + 1)
        return intent
    end
    local dx = fw.order[here + 1][1] - fw.order[here][1]
    local dz = fw.order[here + 1][2] - fw.order[here][2]
    local j = here + 1
    while j < n and fw.order[j + 1][1] - fw.order[j][1] == dx and fw.order[j + 1][2] - fw.order[j][2] == dz do
        j = j + 1
    end
    go(fw.order[j][1], fw.order[j][2])
    return intent
end
