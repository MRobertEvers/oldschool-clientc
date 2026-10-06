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
    -- seam's choice: no source gives a number.
    party_end_hold = 6,
    -- the death ball's gather starts this many ticks before it lands (the
    -- longest walk from a seat to the front tile is 5 tiles: 3 ticks running,
    -- one for the press, one to spare).  The seam's choice.
    gather_lead = 5,
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
function QD.raid._play_sotetseg_seat(st, b)
    local n = b.size or 5
    local role = st.role or 1
    if role == 2 then return b.x - 1, b.z end               -- west, his south-west end
    -- north-west: the west face's northern tile the server stands a raider on
    -- (its north end, z + 4, is not: s33soa's raider stopped on z + 3 three
    -- times, 279 ticks there)
    if role == 3 then return b.x - 1, b.z + n - 2 end
    return b.x + n, b.z + math.floor(n / 2)                 -- east, the middle of his east face
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
            return QD.raid._play_sotetseg_follow(st, v, intent)
        end
        return intent
    end
    if S.fw ~= nil then
        S.fw.done = S.fw.done or v.tick
        S.fw = nil
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
    local pr, projs = QD.world.projectiles(30)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            local land = v.tick + math.ceil((p.cycles_left or 0) / 30)
            -- a homing shot's dst is the target's tile as the client last drew
            -- it, a tick behind a raider who is walking: within a tile is this
            -- raider's (the seats are 3 or more apart; on the front tile all
            -- three share every shot anyway)
            local mine = math.abs(p.dst_x - v.me.x) <= 1 and math.abs(p.dst_z - v.me.z) <= 1
            if p.spotanim_id == P.proj_magic and mine then
                magic_in_air = true
                if land > S.hold then S.hold = land end
            elseif p.spotanim_id == P.proj_ranged and mine then
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
    if death_land ~= nil and death_land > S.death_land then S.death_land = death_land end
    local range = QD.raid._play_sotetseg_range(b, v.me.x, v.me.z)
    local adjacent = range <= 1
    local protect = "protectfrommagic"
    if magic_in_air or v.tick <= S.hold then
        protect = "protectfrommagic"
        S.aimed = S.aimed + 1
    elseif ranged_in_air or v.tick <= S.ranged_hold then
        protect = "protectfrommissiles"
        S.aimed = S.aimed + 1
    elseif adjacent then
        protect = "protectfrommelee"
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
        local attacks = math.ceil(h / 5)
        local per = 0
        if adjacent then per = (protect == "protectfrommelee") and N.melee_prayed or N.melee end
        if disabled then per = N.ball + (adjacent and N.melee or 0) end
        local total = attacks * per
        if S.death_land >= v.tick and S.death_land - v.tick <= h then
            total = total + math.floor(N.death / st.party) + 1
        end
        return total
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
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
function QD.raid._play_sotetseg_follow(st, v, intent)
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
            gaps = 0, walks = 0, order = nil, idx = 0, on_grid = 0, off_path = 0, off_north = nil, start_walk = nil }
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
    if fw.complete == nil then
        local lr, _, locs = QD.world.loc_copies(P.path_loc, 40)
        if lr == "ok" and locs ~= nil then
            for _, r in ipairs(locs) do
                if r.level == 0 and on_grid(r.x, r.z) then
                    local last = fw.samples[#fw.samples]
                    if last == nil or last[1] ~= r.x or last[2] ~= r.z then
                        fw.samples[#fw.samples + 1] = { r.x, r.z, v.tick }
                        if last == nil then
                            add(r.x, r.z)
                        elseif last[1] == r.x or last[2] == r.z then
                            run_to(last[1], last[2], r.x, r.z)
                        else
                            -- two glows not on one run (a tick the read missed):
                            -- the maze's shape says which way the corner went
                            -- (an even row is left northward, an odd row along it)
                            fw.gaps = fw.gaps + 1
                            local cx, cz = r.x, last[2]
                            if (last[2] - gz) % 2 == 0 then cx, cz = last[1], r.z end
                            run_to(last[1], last[2], cx, cz)
                            run_to(cx, cz, r.x, r.z)
                        end
                        if r.z - gz == rows - 1 then fw.complete = v.tick end
                    end
                end
            end
        end
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
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    -- the stall: S p_stun 5 from the proc (the idle form shows on the proc tick)
    if v.tick < fw.seen + 5 then
        return intent
    end
    local moved = st.last_me ~= nil and (st.last_me.x ~= v.me.x or st.last_me.z ~= v.me.z)
    local function go(x, z)
        local same = st.walk_target ~= nil and st.walk_target.x == x and st.walk_target.z == z
        if not same or not moved then
            intent.walk = { x = x, z = z }
            fw.walks = fw.walks + 1
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
    if fw.complete == nil or fw.bad or fw.order == nil then
        -- wait off the grid's south edge below the path's first tile
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
