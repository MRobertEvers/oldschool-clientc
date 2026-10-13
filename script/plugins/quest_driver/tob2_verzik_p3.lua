-- quest-driver / tob2_verzik_p3: VERZIK VITUR PHASE 3, HARD MODE, five raiders,
-- on the v2 layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md; the plan:
-- docs/minigames/theater_of_blood/tob2_rooms/verzik_p3.md).
--
--   QD.raid.verzik_p3_v2_solve(opts) -> result, detail, S
--   opts = the relay's v1 opts: { max_ticks, base (the room origin), weapon,
--   spec_weapon (dragon_claws | dragon_dagger | crystal_halberd; none = no
--   special; owner 2026-10-12: never the warhammer in P3) }
--
-- PREDICT and STRATEGY only. The perception is v1's, CALLED, never copied
-- (raid_solve_verzik_p3.lua): her clock (slots, specials, the HM tank re-pick
-- from her face), the projectiles (webs in flight, the ball's chain, the auto
-- style), the crab blasts (raid_solve_verzik_p2.lua _vzp2_crabs), the tornado
-- owners, the ball roles, the web run's stack machine (_vzp3_web_run, its
-- S.wr state; its order is not used), her attackability (_vzp3_target). The
-- Hard Mode yellows' split is _vzhm_yellow_chains/_match (its caller in
-- raid_solve_verzik_hm.lua is a file local, so VZ.yellows below runs them).
--
-- ===================================================================== MODES
--
-- Re-derived every tick from the view (E14), in this order:
--
--   wait          her P3 form not up (the transition): top up, pray
--   yellows       her charge: my three pools (strike k: alone on mine[k] at
--                 the end of strike-1, every other pool off), her target from
--                 the last strike
--   ball-target   the ball is on me: on the anchor at its read, a WALK hold
--   ball-next     I am the next: within 1 of the anchor at my read
--   ball-clear    3+ off the anchor for the whole flight
--   web-<state>   the webs, not enraged: v1's stack (gather/hold/step/join/
--                 lost) as soft require-zones on the stack's tile and its
--                 predicted clockwise steps; her target from the spin
--   kite          enraged: her target, my tornadoes as CHASERS (the planner
--                 walks them), the scans, the webs (and my own tiles' webs)
--   tank          her known tank is me: under her (inner 5x5) or 3+ off at
--                 the end of every slot-1 (the scans)
--   away          Hard Mode, tank not known yet: 3+ off at every scan
--   dps           swing beside her; the tank keeps her melee off
--
-- The spec weapon is a sub-machine on top (idle/wield/armed/back, v1 main
-- tree _vzp3_special): wield while attacking with a special's energy, press
-- the orb with the attack, back to the main weapon once spent.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.VZP3_V2 = {
    H = 12, BEAM = 32,
    -- THE ONE CURRENCY (budget from the committed v1 on vp2/vp4/vp6, p3cal.py):
    -- her pool 4550 (3500 + the HM heal 1050) in 332-401 ticks, the team took
    -- 1020-1489 there (all but ~1% hers: the autos on every raider). Team hp
    -- taken per tick / team damage dealt per tick = 1204/4550 (vp2), 1020/4550,
    -- 1489/4550 -> 0.22-0.33. A swing lost is that much hp on the team.
    HP_PER_BOSS_HP = 0.27,
    SWING_EXP = 16,             -- v1 measured: 15-17 a tentacle swing on her (hit_npc / swings)
    -- HER AUTOS: on every raider, halved by the matching overhead at landing
    -- (oracle P4); v1 measured 6.9 a hit; HM max 61 (QD.VZHM)
    AUTO_EXP = 7, AUTO_MAX = 61, AUTO_LAND = 3, AUTO_SOON = 3,
    PRAYED = 0.5,
    -- the tornado's touch: half my hp, and she heals three times that
    TORNADO_HEAL = 3,
    -- crab blasts: within 2 of its 2x2 63/26 (forbidden), at 3 a flat 7-8 (priced)
    BLAST_NEAR = 2, BLAST_FAR = 3, BLAST_FAR_DMG = 8, CRAB_SIZE = 2,
    -- the flat 7 at gap 3: v1 main forbids it (owner 2026-10-12, "NO avoidable
    -- damage", xv44). As a RULE it outranks the hp risk, and the plan took
    -- 35-40 tornado touches to keep off it (vp_v2n: 44 touches predicted and
    -- taken, 4/8): priced (8) unless this is set
    BLAST_FAR_FORBID = true,
    MY_CRAB_COST = 1.0,
    CRAB_EARLY = 5, CRAB_EARLY_REACH = 4,        -- a blast a tick later, priced: > a swing's ~4
    EDGE_CRAB_MARGIN = 5, EDGE_CRAB_WEIGHT = 1.0, -- off the walls while a crab is up         -- a tick within 2 of my own crab (it lights the fuse; escapable)    -- ZERO AVOIDABLE (owner 2026-10-12) supersedes the pricing; the touches it cost in vp_v2n were the ball margin's (below) and the priced chaser
    -- webs: on one from a tick before it lands to its explosion, the middle
    -- of a run too (forbidden); my own tick-end tiles in the throws' window
    -- draw a web WEB_STAY tick-ends on (v1 kite's own_web)
    WEB_DMG = 20, WEB_MAX = 40, WEB_STAY = 4, WEB_LIFE = 12, MARK_AHEAD = 7,
    -- the stack (v1 web run) as soft costs: off its tile a tick costs this
    STACK_COST = 2.0, STACK_PULL = 0.5,
    -- the ball and the yellows' approach pulls (v1 1.5)
    BALL_PULL = 1.5, BALL_MARGIN = 30, POOL_PULL = 1.5,
    -- the ball pass (VZ.ball_chain): "follow" (the next keeps near the
    -- kiting target) or "fixed" (the target holds, the next takes a fixed
    -- neighbour); the follow pull a tick (> a swing's ~4.3), the gap-2 price
    BALL_DESIGN = "meet", BALL_NEAR_COST = 6, BALL_CLEAR2_COST = 2,
    BALL_OFF_HER = 3,
    ALTS_GATE = 3,              -- her nine options (path_alts) with my tornado (or a live crab, +1) this close
    CRAB_SPREAD_PULL = 0.3,     -- a kiting seat's pull to its own side of her while a crab is up
    STATION_PULL = 0.05,
    UNDER_COST = 1.5,           -- a tick ended under her (her random step), < a swing's 4.3        -- a seat's side of her ring (the ball explodes on 2+ round its target)
    EDGE = 3, EDGE_WEIGHT = 0.3,
    -- FLOORS (QD.VZHM 75/70 is over an UNPRAYED 61; every auto is prayed,
    -- oracle P4, and QD.T2.rescue eats on a predicted death)
    EAT_FLOOR = 65, BREW_FLOOR = 65, TOP_UP = 90,
    -- THE HP-SAFETY INVARIANT (VZ.hp_floor): the un-bounced ball's hit on its
    -- holder (74 every time it was seen, relay_r13..r16: f71 75->1, f78 68,
    -- f15 62 = all it had), the window of autos it must also outlive (one
    -- brew a DRINK_GAP: a landing within SAFE_WIN cannot be drunk for)
    BALL_MISS = 74, SAFE_WIN = 3, NEXT_FLOOR = 90,
    SPEC_RETRY = 2,
    -- boosts (VZ.boosts): a super restore at DRAIN_AT under base, a super
    -- combat while either stat is under base + REDOSE_UNDER (a restore first
    -- once RESTORE_FIRST under: the combat adds to the drained level)
    DRAIN_AT = 20, REDOSE_UNDER = 5, RESTORE_FIRST = 8,
    -- combat pacing (VZ.combat_paced): a dose keeps the boost ~COMBAT_SPAN
    -- ticks under the brews' drain (v2 measured: 5.7 doses in 321 ticks);
    -- her pool and heal, the team's dps before it is measured
    COMBAT_SPAN = 56, P3_TICKS_GUESS = 330, P3_HP = 3500, HM_HEAL = 1050, HEAL_AT = 0.05,
    DPS_GUESS = 14, DPS_WINDOW = 40,
    -- P3's special weapons (owner 2026-10-12; v1 main tree QD.VZP3.SPEC_WEAPONS):
    -- energy a special costs of 1000, both hands (the parrying dagger to the
    -- pack: a free slot), the special's seq, the weapon's ticks, and the
    -- special's expected damage on her (PROBE each: claws four hits; the
    -- dagger two; the halberd two on a large npc) -> value x HP_PER_BOSS_HP
    SPEC_WEAPONS = {
        dragon_claws = { cost = 500, two_handed = true, seq = "human_dragon_claws_spec", ticks = 4, exp = 35 },
        dragon_dagger = { cost = 250, two_handed = false, seq = "puncture", ticks = 4, exp = 24 },
        crystal_halberd = { cost = 300, two_handed = true, seq = "dragon_halberd_special_attack", ticks = 7, exp = 28 },
        -- burning claws (obj bone_claws, owner 2026-10-12: PREFERRED; pvm_bone_claws.rs2:
        -- three hits at 75-175%, 50-150%, 25-125% of max, each its own accuracy
        -- roll; the burn is not in this content): ~3 x max x 1.0 x accuracy (PROBE)
        bone_claws = { cost = 350, two_handed = true, seq = "human_weapon_burning_claws_02_spec", ticks = 4, exp = 33 },
    },
}

local VZ = {}
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end

-- =================================================================== OBSERVE

VZ.F_KEYS = { "boss", "transition", "bat", "webs", "tornadoes", "crabs", "raiders", "mates", "enraged",
              "enrage_now", "scans", "charging", "ball_thrown", "weapon_held" }

-- v1's perception, once a tick (the clock, the projectiles and the tornado
-- owners keep state); a second F of the same tick gets the first one's fields.
function VZ.observe(S, F)
    local P = S.vz
    if P.obs_tick == F.tick then
        if P.obs_F ~= F then
            for _, k in ipairs(VZ.F_KEYS) do F[k] = P.obs_F[k] end
        end
        return
    end
    P.obs_tick, P.obs_F = F.tick, F
    local ids = S.ids
    F.webs, F.tornadoes, F.crabs = {}, {}, {}
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if id == ids.p3 then F.boss = row
        elseif id == ids.transition then F.transition = row
        elseif id == ids.bat then F.bat = row
        elseif id == ids.web then F.webs[#F.webs + 1] = row
        elseif id == ids.tornado then F.tornadoes[#F.tornadoes + 1] = row
        elseif ids.crabs[id] then F.crabs[#F.crabs + 1] = row
        end
    end
    -- the raiders still in the fight (a dead seat sits in the cage; v1)
    local A, base = QD.VZP3.ARENA, S.base
    local alive, mates = {}, {}
    P.seen_at = P.seen_at or {}
    for _, rd in ipairs(F.raiders) do
        local lx, lz = rd.x - base.x, rd.z - base.z
        P.roster = P.roster or {}
        P.dead = P.dead or {}
        if lx >= A.x0 and lx <= A.x1 and lz >= A.z0 and lz <= A.z1 then
            alive[#alive + 1] = rd
            P.roster[rd.pid] = true
            if not rd.me then mates[#mates + 1] = rd end
        else
            -- (a seat in the spectator cage is dead: forgotten at once - its
            -- last arena tile, 9 ticks old, made it the ball's "next" and the
            -- target ate the un-bounced kill, relay_r10_128 f123 t3412-3420)
            P.seen_at[rd.pid] = nil
            -- (and out of the ball's chain for good: the chain order is the
            -- roster in pid order, the caged seats out)
            if P.roster[rd.pid] then P.dead[rd.pid] = true end
        end
    end
    F.raiders, F.mates = alive, mates
    -- every raider's last tile seen (the players list holds only the seats
    -- in VIEW, ~15 tiles: the ball's chain reads seats across her)
    P.seen_at = P.seen_at or {}
    for _, rd in ipairs(alive) do P.seen_at[rd.pid] = { pid = rd.pid, x = rd.x, z = rd.z, t = F.tick } end
    F.enraged = S.enraged or #F.tornadoes > 0
    F.enrage_now = F.enraged and not S.enraged
    S.enraged = F.enraged
    F.weapon_held = not QD.raid._tob_worn(S, ids.weapon) and QD.raid._tob_count(S, ids.weapon) > 0
    if P.last_hp and F.hp < P.last_hp then P.taken = P.taken + (P.last_hp - F.hp) end
    P.last_hp = F.hp
    F.scans = {}
    if F.boss == nil then return end
    local autos = S.autos
    QD.raid._vzp3_projectiles(S, F)
    VZ.ball_flight(S, F)
    QD.raid._vzp3_clock(S, F)
    if S.autos ~= autos and not F.ball_thrown then P.auto_at = F.tick end
    QD.raid._vzp2_crabs(S, F)          -- (its kill blasts only: a crab shot down)
    VZ.crabs(S, F)
    QD.raid._vzp3_tornadoes(S, F)
    VZ.yellows(S, F)
    VZ.her_clock(S, F)
    S.target = QD.raid._vzp3_target(S, F)
    -- the web run's machine (v1, its S.wr state; the order it returns is not
    -- used): the stack's tile, the tick its index last changed
    QD.raid._vzp3_web_run(S, F)
    local W = S.wr
    if W and W.state ~= "done" and W.idx ~= P.wr_idx then
        if P.wr_idx ~= nil and W.webs_at == P.wr_webs then P.wr_moved = F.tick else P.wr_moved = nil end
        P.wr_idx, P.wr_webs = W.idx, W.webs_at
    end
end

-- THE HARD YELLOWS (raid_solve_verzik_hm.lua yellow_pools, whose functions
-- are file locals there; the split and the match are v1's): the strikes from
-- her charge, my chain of three pools, the same on every seat.
function VZ.yellows(S, F)
    local Y0 = QD.VZHM
    if S.blast == nil or not F.charging then
        S.hm_y = nil
        return
    end
    if S.hm_y == nil then
        local y0 = S.blast - QD.VZP3.YELLOW_CHARGE
        S.hm_y = { y0 = y0, strikes = {} }
        for i, d in ipairs(Y0.YELLOW_STRIKES) do S.hm_y.strikes[i] = y0 + d end
        S.blast = y0 + Y0.YELLOW_LAST
        if S.next_slot then S.next_slot = S.next_slot + (Y0.YELLOW_LAST - QD.VZP3.YELLOW_CHARGE) end
        QD.raid._vzp3_trace(S, F.tick, "hard yellows: strikes t" .. table.concat(S.hm_y.strikes, ",t"))
    end
    if S.hm_y.mine or S.hm_y.split_failed then return end
    local r, rows = api_drive.spotanims(0)
    if r ~= "ok" then return end
    local seen, pools = {}, {}
    for _, s in ipairs(rows) do
        if s.spotanim_id == S.ids.pool then
            local k = s.x .. "," .. s.z
            if not seen[k] then
                seen[k] = true
                pools[#pools + 1] = { x = s.x, z = s.z }
            end
        end
    end
    local n = #F.raiders
    if n == 0 or #pools < 3 * n then return end
    table.sort(pools, function(a, b) return a.x < b.x or (a.x == b.x and a.z < b.z) end)
    local chains, hop = QD.raid._vzhm_yellow_chains(pools, n)
    if chains == nil then
        S.hm_y.split_failed = true
        QD.raid._vzp3_trace(S, F.tick, "hard yellows: no split of " .. #pools .. " pools into " .. n .. " chains")
        return
    end
    local got = QD.raid._vzhm_yellow_match(F.raiders, chains)
    S.hm_y.pools = pools
    for j, rd in ipairs(F.raiders) do
        if rd.pid == F.pid then S.hm_y.mine = got[j] end
    end
    local m = S.hm_y.mine
    if m then
        QD.raid._vzp3_trace(S, F.tick, string.format("my pools %d,%d %d,%d %d,%d (hop %d)", m[1].x, m[1].z,
            m[2].x, m[2].z, m[3].x, m[3].z, hop))
    end
end

-- ===================================================================== CRABS
--
-- THE CONTENT'S CRAB (tob_verzik.rs2 ~tob_verzik_crab_tick, read against
-- the tick log in vp_v2r): a FIXED owner (`nvar_owner`), whose tile at the
-- end of the last tick it faces every tick (npc_facesquare: the row's
-- face_x/face_z, stamped face_tick); still for its first 3 ticks; then a
-- step a tick toward that tile (each axis off its 2x2); ARRIVAL on tick A
-- when age > 3 and the owner's end-of-(A-1) tile is within 2 of its 2x2
-- (`npc_range`); it stops and blasts on max(born + 7, A + 1), reading the
-- tiles at the end of max(born + 6, A): 63 / 26 / 7 at gap <= 1 / 2 / 3.
-- An arrival is therefore known ONE tick ahead from the tiles I see now.
-- (v1 guessed the owner as the nearest raider and the stop 6 ticks ahead.)
function VZ.crabs(S, F)
    local P = S.vz
    P.crab = P.crab or {}
    P.rt = P.rt or {}
    for _, rd in ipairs(F.raiders) do
        P.rt[rd.pid] = P.rt[rd.pid] or {}
        P.rt[rd.pid][F.tick] = { x = rd.x, z = rd.z }
        P.rt[rd.pid][F.tick - 12] = nil
    end
    local n = QD.VZP3_V2.CRAB_SIZE
    local function gap(x, z, cx, cz) return QD.raid._tob_gap(x, z, cx, cz, n) end
    for _, row in ipairs(F.crabs) do
        local c = P.crab[row.slot]
        if c == nil then
            c = { born_lo = F.tick - 1, born_hi = F.tick }
            P.crab[row.slot] = c
        end
        c.x, c.z, c.seen = row.x, row.z, F.tick
        -- THE OWNER: the raider whose tile at the end of face_tick - 1 the
        -- crab faces. VOTED, one vote a new face stamp, the owner the pid
        -- with two votes more than any other (a face matched two raiders
        -- standing together, and a first-match latch took the wrong one:
        -- vb_claws vp1 t1042, a 44 from a crab given to seat 3)
        c.votes = c.votes or {}
        if row.face_tick and row.face_tick >= 0 and row.face_tick ~= c.voted_at then
            c.voted_at = row.face_tick
            local hits = {}
            for pid, hist in pairs(P.rt) do
                -- (the drive stamps the face a tick early, as it does a seq
                -- (R4): the tile is the owner's at the end of face_tick -
                -- vdbg2 vp1 t1058, read at face_tick - 1 it named seat 4)
                local tl = hist[row.face_tick]
                if tl and tl.x == row.face_x and tl.z == row.face_z then hits[#hits + 1] = pid end
            end
            if #hits == 1 then c.votes[hits[1]] = (c.votes[hits[1]] or 0) + 1 end
            local best, bv, second = nil, 0, 0
            for pid, v in pairs(c.votes) do
                if v > bv or (v == bv and best and pid < best) then second, best, bv = math.max(second, bv), pid, v
                elseif v > second then second = v end
            end
            local owner = (best and bv - second >= 2) and best or nil
            if owner ~= c.owner and api_drive.ticklog_mark then
                api_drive.ticklog_mark("vzc p" .. tostring(F.pid) .. " t" .. F.tick .. " crab " .. row.slot .. " owner " .. tostring(owner))
            end
            c.owner = owner
        end
        if (row.health_ratio == 0) or (S.ids.death_seqs[row.seq_id]) then c.blasted = true end
        -- ARRIVAL AT now+1, decided from the tiles at the end of now (the
        -- owner's, or any raider's while the owner is unknown)
        -- (an arrived crab never steps again: one that moved was not arrived -
        -- the latch is dropped; vb_claws vp1 t1036-1039)
        if c.read_lo and c.at and (c.at.x ~= c.x or c.at.z ~= c.z) then c.read_lo, c.read_hi = nil, nil end
        c.maybe = nil
        if c.read_lo == nil and not c.blasted and F.tick + 1 - c.born_lo > 3 then
            local near, any = false, false
            for _, rd in ipairs(F.raiders) do
                if gap(rd.x, rd.z, c.x, c.z) <= 2 then
                    any = true
                    if rd.pid == c.owner then near = true end
                end
            end
            -- owner unknown: a POSSIBLE arrival, this tick's only (not latched)
            if any and c.owner == nil then
                c.maybe = { lo = math.max(c.born_lo + 6, F.tick + 1), hi = math.max(c.born_hi + 6, F.tick + 1) }
            end
            if near then
                c.at = { x = c.x, z = c.z }
                local A = F.tick + 1
                c.read_lo, c.read_hi = math.max(c.born_lo + 6, A), math.max(c.born_hi + 6, A)
                if api_drive.ticklog_mark then
                    api_drive.ticklog_mark("vzc p" .. tostring(F.pid) .. " t" .. F.tick .. " crab " .. row.slot .. " arrives t" .. A
                        .. " reads t" .. c.read_lo .. ".." .. c.read_hi .. " at " .. c.x .. "," .. c.z)
                end
            end
        end
        -- (a blast is its death: a crab still standing two ticks after its
        -- predicted read was NOT arrived - drop the latch and keep modelling
        -- it; marked blasted it was never seen again, vc_claws vp1 t1060: 61)
        if c.read_hi and F.tick > c.read_hi + 2 and not c.blasted then c.read_lo, c.read_hi = nil, nil end
        -- its path ahead (not arrived): a step a tick toward its owner's tile
        -- now (held), stopping once within 2 of it
        c.path = {}
        local x, z = c.x, c.z
        local tgt = nil
        for _, rd in ipairs(F.raiders) do if rd.pid == c.owner then tgt = rd end end
        -- (owner unknown: toward the nearest raider - a step could go to any,
        -- so the band round it is one wider below)
        if tgt == nil then
            local bd = nil
            for _, rd in ipairs(F.raiders) do
                local d = gap(rd.x, rd.z, c.x, c.z)
                if bd == nil or d < bd then tgt, bd = rd, d end
            end
        end
        for k = 1, QD.VZP3_V2.H do
            -- (npc_walk($at): its SW tile steps toward the owner's tile on
            -- each axis - vdbg3 vp4 t905, 6441,87 -> 6442,88 toward 6445,88)
            if tgt and F.tick + k - c.born_lo > 3 and gap(tgt.x, tgt.z, x, z) > 2 then
                x = x + ((tgt.x > x) and 1 or ((tgt.x < x) and -1 or 0))
                z = z + ((tgt.z > z) and 1 or ((tgt.z < z) and -1 or 0))
            end
            c.path[k] = { x = x, z = z }
        end
    end
    for slot, c in pairs(P.crab) do
        if c.seen ~= F.tick and F.tick - (c.seen or 0) > 3 then P.crab[slot] = nil end
    end
end

-- ================================================================== HELPERS

-- Her melee is off me at the scans? (v1: her known tank is someone else)
function VZ.exempt(S, F)
    return S.tank_ok and S.tank_pid ~= nil and F.pid ~= nil and S.tank_pid ~= F.pid
end

function VZ.tank_known(S)
    return S.tank_ok == true and S.tank_pid ~= nil
end

-- The ball's read tick for my role (v1 _vzp3_decide's `holding`).
function VZ.ball_read(S, F, role, land)
    land = land or S.ball.land
    if role == "target" then return land - 1 end
    return (F.pid < S.ball.cur) and land or (land - 1)
end

-- THE BALL PASS AS A PROTOCOL (owner 2026-10-12: "a better algorithm for
-- passing the bounce"), from facts every seat sees the same (E14):
--   TARGET   the projectile's target pid (v1 S.ball.cur)
--   NEXT     the first seat after the target in pid order (cyclic) that is
--            alive (seen in the arena, never in the cage) and has not held the
--            ball this chain - pids alone, no positions (the old "nearest"
--            read each seat's own view, and two views picked two seats)
--   design "fixed"  the target HOLDS its tile at first sight; the next takes
--            a fixed neighbour of it (the walkable one farthest from her
--            footprint at first sight, ties N,E,S,W,NE,SE,SW,NW)
--   design "follow" (PREFERRED, owner): the target kites freely; the next
--            keeps its swings and its tornado and is pulled (soft, above a
--            swing) to within 1 of the target's predicted tile, and must be
--            there (a rule) on its read tick
--   EVERYONE ELSE  forbidden within 1 of the target at the read ticks
-- -> role ("target" | "next" | "clear"), the target's tile (fixed: at first
-- sight; follow: predicted), the next's pid, the next's tile (fixed only).
-- DESIGN "meet" (owner 2026-10-12, the main candidate): a RANKED MEETING
-- TILE, planned like the yellows. Chosen once when the hop is first seen,
-- from shared facts only (every seat's first-sight tiles of the target and
-- the next, her footprint then):
--   NEXT     the unvisited seat nearest the target's first-sight tile, ties
--            to the lower pid
--   T        the acceptable tile minimising the slower seat's walk (max of
--            the two Chebyshev distances), then their sum, then x, z;
--            acceptable = inside the arena (1 off its edge), 3+ off her
--            footprint, no web on it at the reads, no crab blast band on it,
--            and both seats can run to it before their reads (2 tiles a tick)
--   RULES    only at the read ticks: the target ON T, the next within 1 of
--            T, everyone else 2+ off T. No hold, no pull: both kite and swing
--            freely; the planner's lookahead brings them in time.
--   RE-CHECK each tick: T turned unacceptable (a web landed on it, a crab
--            blast band) -> the next tile in the same ranking, same tick on
--            every seat. None acceptable -> batch 13's meeting tile.
function VZ.ball_meet_rank(S, F, Bl, t_from, n_from, reads, land_hi)
    local V, A, base = QD.VZP3, QD.VZP3.ARENA, S.base
    local b = Bl.her or F.boss
    local list, why = {}, {}
    local function bad(x, z)
        if x < base.x + A.x0 + 1 or x > base.x + A.x1 - 1 or z < base.z + A.z0 + 1 or z > base.z + A.z1 - 1 then return "edge" end
        if b and QD.raid._tob_gap(x, z, b.x, b.z, V.SIZE) < 3 then return "her" end
        for _, h in ipairs(S.web_hazards) do
            if h.x == x and h.z == z and h.from <= land_hi and h.to >= reads then return "web" end
        end
        for _, c in pairs(S.vz.crab or {}) do
            if not c.blasted and QD.raid._tob_gap(x, z, c.x, c.z, 2) <= 3 then return "crab" end
        end
        return nil
    end
    local ticks = math.max(1, reads - F.tick)
    -- WALKING distances round her body (8-way steps, her footprint at first
    -- sight blocked, the arena's edge): by straight-line distance a tile
    -- across her looked reachable and was not (bm_x vp10 t891: the target
    -- 6 off with her between, plan risk from k3, the un-bounced landing)
    -- (cheap: an 8-way flood on a 19x19 window as flat arrays; the
    -- instruction budget is 400000 a tick, E13 - the first version spent it)
    local R = 9
    local W = 2 * R + 1
    local ox, oz = t_from.x - R, t_from.z - R
    local ax0, ax1, az0, az1 = base.x + A.x0, base.x + A.x1, base.z + A.z0, base.z + A.z1
    local hx0, hx1, hz0, hz1 = -1, -2, -1, -2
    if b then hx0, hx1, hz0, hz1 = b.x, b.x + V.SIZE - 1, b.z, b.z + V.SIZE - 1 end
    local DX = { -1, 0, 1, -1, 1, -1, 0, 1 }
    local DZ = { -1, -1, -1, 0, 0, 1, 1, 1 }
    local function bfs(from)
        local dist = {}
        if from == nil then return dist end
        local fx, fz = from.x - ox, from.z - oz
        if fx < 0 or fx >= W or fz < 0 or fz >= W then return dist end
        local qx, qz, h, n = { fx }, { fz }, 1, 1
        dist[fx * W + fz] = 0
        while h <= n do
            local cx, cz = qx[h], qz[h]
            local d = dist[cx * W + cz] + 1
            h = h + 1
            for k = 1, 8 do
                local x, z = cx + DX[k], cz + DZ[k]
                if x >= 0 and x < W and z >= 0 and z < W and dist[x * W + z] == nil then
                    local wx, wz = x + ox, z + oz
                    if wx >= ax0 and wx <= ax1 and wz >= az0 and wz <= az1
                        and not (wx >= hx0 and wx <= hx1 and wz >= hz0 and wz <= hz1) then
                        dist[x * W + z] = d
                        n = n + 1
                        qx[n], qz[n] = x, z
                    end
                end
            end
        end
        return dist
    end
    local dT = bfs(t_from)
    local dN = bfs(n_from)
    local function at(dist, x, z)
        local lx, lz = x - ox, z - oz
        if lx < 0 or lx >= W or lz < 0 or lz >= W then return nil end
        return dist[lx * W + lz]
    end
    for x = t_from.x - 7, t_from.x + 7 do
        for z = t_from.z - 7, t_from.z + 7 do
            local dt = at(dT, x, z)
            local dn = 0
            if n_from and dt then
                -- (within 1 of T: the best of T and its 8 neighbours)
                dn = nil
                dn = at(dN, x, z)
                for k = 1, 8 do
                    local v = at(dN, x + DX[k], z + DZ[k])
                    if v and (dn == nil or v < dn) then dn = v end
                end
            end
            if dt and dn and (dt + 1) // 2 <= ticks - 1 and (dn + 1) // 2 <= ticks - 1 then
                local r = bad(x, z)
                if r == nil then
                    list[#list + 1] = { x = x, z = z, k1 = math.max(dt, dn), k2 = dt + dn }
                end
            end
        end
    end
    table.sort(list, function(p, q)
        if p.k1 ~= q.k1 then return p.k1 < q.k1 end
        if p.k2 ~= q.k2 then return p.k2 < q.k2 end
        if p.x ~= q.x then return p.x < q.x end
        return p.z < q.z
    end)
    return list, bad
end

function VZ.ball_chain(S, F)
    local Bl = S.ball
    local P = S.vz
    if Bl == nil or Bl.land == nil or Bl.cur == nil or F.pid == nil then return nil end
    -- the roster in pid order, the dead out
    local pids = {}
    for pid in pairs(P.roster or {}) do if not (P.dead or {})[pid] then pids[#pids + 1] = pid end end
    table.sort(pids)
    if Bl.next_for ~= Bl.cur then
        Bl.next_for, Bl.next_pid = Bl.cur, nil
        if QD.VZP3_V2.BALL_DESIGN == "meet" then
            local seen = P.seen_at or {}
            local t0 = Bl.anchor or (seen[Bl.cur] and { x = seen[Bl.cur].x, z = seen[Bl.cur].z }) or nil
            local best, bd, nfrom = nil, nil, nil
            for _, rd in pairs(seen) do
                if F.tick - rd.t <= 10 and not Bl.visited[rd.pid] and rd.pid ~= Bl.cur and not (P.dead or {})[rd.pid] and t0 then
                    local d = cheb(rd.x, rd.z, t0.x, t0.z)
                    if bd == nil or d < bd or (d == bd and rd.pid < best) then best, bd, nfrom = rd.pid, d, { x = rd.x, z = rd.z } end
                end
            end
            Bl.next_pid = best
            Bl.ttile, Bl.ntile = t0, nil
            Bl.her = F.boss and { x = F.boss.x, z = F.boss.z } or nil
            Bl.meet_list, Bl.meet_i = nil, nil
            if t0 then
                local land_hi = math.max(Bl.land, Bl.land_hi or Bl.land)
                Bl.meet_list, Bl.meet_bad = VZ.ball_meet_rank(S, F, Bl, t0, nfrom, Bl.land - 1, land_hi)
                Bl.meet_i = 1
            end
            if api_drive.ticklog_mark then
                local m = Bl.meet_list and Bl.meet_list[1]
                api_drive.ticklog_mark(string.format("vzb p%s t%d meet cur %s next %s t0 %s T %s of %d land %s",
                    tostring(F.pid), F.tick, tostring(Bl.cur), tostring(Bl.next_pid),
                    t0 and (t0.x .. "," .. t0.z) or "-", m and (m.x .. "," .. m.z) or "none",
                    Bl.meet_list and #Bl.meet_list or 0, tostring(Bl.land)))
            end
        end
        local at = nil
        if QD.VZP3_V2.BALL_DESIGN == "meet" then at = nil goto chosen end
        for i, pid in ipairs(pids) do if pid == Bl.cur then at = i end end
        if at then
            for k = 1, #pids - 1 do
                local pid = pids[((at - 1 + k) % #pids) + 1]
                if not Bl.visited[pid] and pid ~= Bl.cur then Bl.next_pid = pid break end
            end
        end
        ::chosen::
        -- the target's tile at first sight (v1's anchor; out of my view, its
        -- last-seen tile) and her footprint then: the fixed design's tiles
        local seen = P.seen_at or {}
        Bl.ttile = Bl.anchor or (seen[Bl.cur] and { x = seen[Bl.cur].x, z = seen[Bl.cur].z }) or nil
        Bl.ntile = nil
        local b = F.boss
        if Bl.ttile and b then
            local V, A, base = QD.VZP3, QD.VZP3.ARENA, S.base
            local best, bg = nil, -1
            for _, d in ipairs({ { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 }, { 1, 1 }, { 1, -1 }, { -1, -1 }, { -1, 1 } }) do
                local x, z = Bl.ttile.x + d[1], Bl.ttile.z + d[2]
                local inside = x >= base.x + A.x0 and x <= base.x + A.x1 and z >= base.z + A.z0 and z <= base.z + A.z1
                local g = QD.raid._tob_gap(x, z, b.x, b.z, V.SIZE)
                if inside and g >= 1 and g > bg then best, bg = { x = x, z = z }, g end
            end
            Bl.ntile = best
        end
        if api_drive.ticklog_mark then
            api_drive.ticklog_mark(string.format("vzb p%s t%d chain cur %s next %s ttile %s ntile %s land %s",
                tostring(F.pid), F.tick, tostring(Bl.cur), tostring(Bl.next_pid),
                Bl.ttile and (Bl.ttile.x .. "," .. Bl.ttile.z) or "-", Bl.ntile and (Bl.ntile.x .. "," .. Bl.ntile.z) or "-",
                tostring(Bl.land)))
        end
    end
    -- the target's tile now (follow): seen, else last seen; its step from the
    -- tick before carried one tick on (it kites; widened by the zone's 1)
    local cur = nil
    for _, rd in ipairs(F.raiders) do if rd.pid == Bl.cur then cur = { x = rd.x, z = rd.z } end end
    if cur == nil and P.seen_at and P.seen_at[Bl.cur] then cur = { x = P.seen_at[Bl.cur].x, z = P.seen_at[Bl.cur].z } end
    local ttile = Bl.ttile
    if QD.VZP3_V2.BALL_DESIGN == "follow" then ttile = cur or Bl.ttile end
    if QD.VZP3_V2.BALL_DESIGN == "meet" and Bl.meet_list then
        -- re-check: the chosen tile turned unacceptable -> the next in rank
        local skipped = {}
        while Bl.meet_list[Bl.meet_i] do
            local m = Bl.meet_list[Bl.meet_i]
            local r = Bl.meet_bad(m.x, m.z)
            if r == nil then break end
            skipped[#skipped + 1] = m.x .. "," .. m.z .. ":" .. r
            Bl.meet_i = Bl.meet_i + 1
        end
        if #skipped > 0 and api_drive.ticklog_mark then
            local m = Bl.meet_list[Bl.meet_i]
            api_drive.ticklog_mark(string.format("vzb p%s t%d meet skip %s -> %s", tostring(F.pid), F.tick,
                table.concat(skipped, " "), m and (m.x .. "," .. m.z) or "none (batch 13 tile)"))
        end
        local m = Bl.meet_list[Bl.meet_i]
        if m then ttile = { x = m.x, z = m.z } else ttile = Bl.ttile end
        Bl.meet_now = ttile
    end
    if ttile == nil then return nil end
    if Bl.cur == F.pid then return "target", ttile, Bl.next_pid, Bl.ntile end
    if Bl.visited[F.pid] then return "clear", ttile, Bl.next_pid, Bl.ntile end
    return (Bl.next_pid == F.pid) and "next" or "clear", ttile, Bl.next_pid, Bl.ntile
end

-- THE BALL'S FLIGHT IS NOT A CONSTANT. v1 lands every throw at first sight
-- + 7 (227 cycles); the content's throw is `~player_projectile(..., 181, 64,
-- 8)`, longer with distance: vw_claws vp2 t946, 251 cycles, landed t954 on
-- a target that had stepped off at the predicted t953 - no bounce, the
-- Hard Mode kill (`$hit = $cur`). The landing as the projectile says it
-- (scriptrun reads it a tick in: floor(cycles_left / 30) + 1, v1's own
-- note), kept beside v1's: S.ball.land_hi, and the hold covers both.
function VZ.ball_flight(S, F)
    local Bl = S.ball
    if Bl == nil or Bl.cur == nil or Bl.land == nil then return end
    if Bl.land_for == Bl.cur then return end
    local r, projs = api_drive.projectiles(0)
    if r ~= "ok" then return end
    for _, p in ipairs(projs) do
        if p.spotanim_id == S.ids.ball and p.target == -Bl.cur - 1 then
            local hi = F.tick + math.max(0, p.cycles_left or 0) // 30 + 1
            Bl.land_hi = math.max(Bl.land, hi)
            Bl.land_for = Bl.cur
            if Bl.land_hi ~= Bl.land then
                QD.raid._vzp3_trace(S, F.tick, "ball flight: lands t" .. Bl.land .. "..t" .. Bl.land_hi)
            end
        end
    end
end

-- The throws' window: the tick-ends a throw reads (v1 kite's throw0/throw1).
function VZ.throws(S)
    if S.spin_at == nil then return nil end
    local V = QD.VZP3
    return S.spin_at + V.WEB_FIRST_THROW - 1, S.spin_at + V.WEB_FIRST_THROW + V.WEB_THROWS - 2
end

-- My main weapon's / the worn weapon's ticks (v1 _vzp3_weapon_ticks reads worn).
-- (the weapon wielded THIS tick when the gear changes: the press goes out
-- after the wield, and the executive keeps the speed of the press - vp_v2a
-- seat 1 t813-834: read at the hammer's 6 after the switch back, every
-- tentacle swing "unpredicted")
function VZ.speed(S, I)
    local sp = S.vz.spec
    if I and I.gear and #I.gear > 0 then
        local g = I.gear[1]
        if type(g) == "table" then g = g.obj end
        if sp and g == sp.id then return sp.ticks end
        if g == S.ids.weapon then return S.vz.main_ticks end
    end
    if sp and QD.raid._tob_worn(S, sp.id) then return sp.ticks end
    return QD.raid._vzp3_weapon_ticks(S)
end

-- ========================================================= HP SAFETY
--
-- THE INVARIANT (owner 2026-10-12, "ensure that p3 stops having those
-- deaths"): no seat ends a tick where the worst damage that can still reach
-- it before its next possible heal kills it. Every P3 death of r15/r16 was
-- the ball's un-bounced 74 on a holder at 62-75 (f15 p2 t3061, f71 p5 t3127
-- then an auto of 17 at hp 17, f78 p1 t3155): the bounce failed (a tornado
-- or her chase pulled the holder off T) and nothing had planned for a miss.
--   worst   the prayed max of every auto landing within SAFE_WIN (a brew
--           cannot land between them), and on the ball's HOLDER the miss
--           (BALL_MISS; 49 on the last hop) plus the autos landing from the
--           tick before it to SAFE_WIN after it (drunk for during the flight,
--           7+ ticks: two or three brews from first sight)
--   floor   worst + 1, at least EAT_FLOOR; the next holder NEXT_FLOOR (its
--           own flight starts at the bounce)
-- Under the floor a brew OUTRANKS the boosts and the swing (a potion costs
-- no attack delay, C2; the boosts share its timer, C1 - f15 p2 drank a super
-- combat at t3059 as the holder at 71 and the brew at t3060 met the timer).
-- A prayer restore under PRAYER_SIP keeps the timer (unprayed autos are 61).
function VZ.auto_landings(S, F, t0, t1)
    local X, V, P = QD.VZP3_V2, QD.VZP3, S.vz
    local n = 0
    if P.auto_at then
        local L = P.auto_at + X.AUTO_LAND
        if L >= t0 and L <= t1 then n = n + 1 end
    end
    if S.next_slot then
        local cad = F.enraged and V.CADENCE_ENRAGED or V.CADENCE
        local special = V.ROTATION[S.special]
        local i = 0
        while i <= 16 do
            local T = S.next_slot + i * cad
            local L = T + X.AUTO_LAND
            if L > t1 or i > (S.attacks_left or 0) or (i == S.attacks_left and special ~= "crabs") then break end
            if L >= t0 and T > F.tick then n = n + 1 end
            i = i + 1
        end
    end
    return n
end

function VZ.hp_floor(S, F)
    local X = QD.VZP3_V2
    local now = F.tick
    local auto = math.floor(X.AUTO_MAX * X.PRAYED)
    local floor = math.max(X.EAT_FLOOR, VZ.auto_landings(S, F, now + 1, now + X.SAFE_WIN) * auto + 1)
    local why = "autos"
    local Bl = S.ball
    if Bl and Bl.cur and Bl.land then
        local hi = math.max(Bl.land, Bl.land_hi or Bl.land)
        if hi + 1 >= now and Bl.cur == F.pid then
            local miss = (#(Bl.order or {}) >= 4) and 49 or X.BALL_MISS
            local f = miss + VZ.auto_landings(S, F, Bl.land - 1, hi + X.SAFE_WIN) * auto + 1
            if f > floor then floor, why = f, "ball-holder" end
        elseif hi + 1 >= now and Bl.next_pid == F.pid and X.NEXT_FLOOR > floor then
            floor, why = X.NEXT_FLOOR, "ball-next"
        end
    end
    return floor, why
end

function VZ.hp_safety(S, F, I)
    local X, P = QD.VZP3_V2, S.vz
    local floor, why = VZ.hp_floor(S, F)
    P.hp_floor = floor
    local want = S.t2.want
    -- (the resource channel reads these next tick; food stops at base)
    want.eat_below = math.max(want.eat_below or 0, math.min(floor, F.hp_base or floor))
    want.brew_below = math.max(want.brew_below or 0, floor)
    if F.hp >= floor then return end
    if F.tick < S.x2.held.drink_until then return end
    if S.last_drink == F.tick and I.drink == nil then return end   -- the resource channel drank (a brew or a restore)
    if (F.prayer or 0) < QD.TOBS.PRAYER_SIP and I.drink == nil then return end
    local brew = QD.raid._tob_first(S, S.ids.brews)
    if brew == nil then return end
    if I.drink ~= brew then
        I.drink = brew
        S.last_drink = F.tick
        P.safety_n = (P.safety_n or 0) + 1
        QD.raid._vzp3_trace(S, F.tick, string.format("hp-safety brew: hp %d < floor %d (%s)", F.hp, floor, why))
    end
end

-- ================================================================== BOOSTS
--
-- THE TEAM KEPT SUPER-COMBATTED (coordinator's probe, runs/st_v1, st_v2:
-- Attack/Strength at or under base on 18-28% of P3 ticks, worst -55, because
-- every brew dose drains both and the shared supplies looked at Attack only,
-- restored at -10 and re-boosted only from base, every 20 ticks at most).
-- Every tick the drink timer allows and the resource channel drank nothing
-- (it decides first: hp and prayer win):
--   Attack or Strength DRAIN_AT under base (two or three brew doses; main
--   tree v1 QD.VZHM.STAT_DRAIN 20)               -> a super restore
--   either under base + REDOSE_UNDER, both within RESTORE_FIRST of base
--                                                -> a super combat
-- A potion adds no attack delay (C2), so it costs no swing. P3 is the last
-- room: no reserve is kept.
function VZ.boosts(S, F, I)
    local X = QD.VZP3_V2
    local a, st = F.levels.attack, F.levels.strength
    if a == nil or st == nil or a.base <= 0 then return end
    if S.last_drink == F.tick then return end              -- the resource channel drank this tick
    if F.hp < (VZ.hp_floor(S, F)) then return end           -- hp first (VZ.hp_safety)
    -- (no boost and no stat restore while the ball is mine or I am its next:
    -- the potion timer is the holder's brew's - f15 p2's super combat at
    -- t3059, f78 p1's restore at t3148; a prayer restore under PRAYER_SIP is
    -- the resource channel's and still goes)
    local Bl = S.ball
    if Bl and Bl.cur and Bl.land and math.max(Bl.land, Bl.land_hi or Bl.land) + 1 >= F.tick
        and (Bl.cur == F.pid or Bl.next_pid == F.pid) then return end
    if F.tick < S.x2.held.drink_until then return end      -- the potion timer (C1)
    local da, ds = a.level - a.base, st.level - st.base
    local low = math.min(da, ds)
    local drink, why = nil, nil
    if low <= -X.DRAIN_AT then
        drink, why = QD.raid._tob_first(S, S.ids.stat_restores), "restore"
    elseif low < X.REDOSE_UNDER and low > -X.RESTORE_FIRST and VZ.combat_paced(S, F) then
        drink, why = QD.raid._tob_first(S, S.ids.combats), "combat"
    elseif low <= -X.RESTORE_FIRST then
        drink, why = QD.raid._tob_first(S, S.ids.stat_restores), "restore"
    end
    if drink == nil then return end
    if why == "combat" then S.vz.last_combat = F.tick end
    I.drink = drink
    S.last_drink = F.tick
    S.vz.boost_n[why] = (S.vz.boost_n[why] or 0) + 1
end

-- THE COMBATS PACED OVER WHAT IS LEFT OF HER (the budget layer,
-- QD.T2.budget: P3 is the last room, its reserve 0, so spend to win - but
-- spread, so the last dose is not gone with half her hp left). Ticks left =
-- her hp left (her bar x 3500, plus the Hard Mode heal of 1050 while she is
-- above 5%) / the team's damage a tick, measured over her bar's last
-- DPS_WINDOW ticks (DPS_GUESS before that). With stock x COMBAT_SPAN to
-- cover them, drink freely; else a dose at most every ticks-left / stock.
function VZ.combat_paced(S, F)
    local X, P = QD.VZP3_V2, S.vz
    local stock
    if QD.T2.budget and QD.T2.budget.spendable then
        stock = QD.T2.budget.spendable(S, "combat")
    else
        stock = QD.raid.tob_combat_doses(S)
    end
    if stock <= 0 then return false end
    local left = P.ticks_left or X.P3_TICKS_GUESS
    if stock * X.COMBAT_SPAN >= left then return true end
    return F.tick - (P.last_combat or -1000) >= left / stock
end

-- Her hp left and the ticks it takes the team (VZ.observe, once a tick).
function VZ.her_clock(S, F)
    local X, P = QD.VZP3_V2, S.vz
    local b = F.boss
    if b == nil or b.health_ratio == nil or (b.health_scale or 0) <= 0 then return end
    local frac = b.health_ratio / b.health_scale
    if frac <= X.HEAL_AT then P.healed = true end
    local hp = frac * X.P3_HP + (P.healed and 0 or X.HM_HEAL)
    P.hp_hist = P.hp_hist or {}
    P.hp_hist[#P.hp_hist + 1] = { t = F.tick, hp = hp }
    while #P.hp_hist > 1 and P.hp_hist[1].t < F.tick - X.DPS_WINDOW do table.remove(P.hp_hist, 1) end
    local a, z = P.hp_hist[1], P.hp_hist[#P.hp_hist]
    local dps = X.DPS_GUESS
    if z.t - a.t >= X.DPS_WINDOW // 2 and a.hp > z.hp then dps = (a.hp - z.hp) / (z.t - a.t) end
    P.ticks_left = hp / math.max(dps, 1)
end

-- ============================================================ THE SPEC WEAPON
--
-- v1 main tree _vzp3_special's machine on the executive's channels:
--   idle  -> wield   attacking (last plan), energy >= cost, the weapon in the pack
--   wield -> armed   in hand (intents.gear, re-sent every SPEC_RETRY)
--   armed -> back    fired (energy fell by a cost) and not enough for another,
--                    or not in hand; the orb rides the attack (intents.spec)
--   back  -> idle    the main weapon in hand again
function VZ.special(S, F, I)
    local X, P = QD.VZP3_V2, S.vz
    local sp = P.spec
    if sp == nil then return end
    local function enter(state, why)
        QD.raid._vzp3_trace(S, F.tick, "spec weapon: " .. sp.state .. " -> " .. state .. " (" .. why .. ")")
        sp.state = state
    end
    P.energy_last = F.energy
    local last = S.t2.last
    local attacking = last ~= nil and last.path[1].act == "attack"
    if sp.state == "idle" then
        -- (owner 2026-10-12: "all seats should prioritize dumping special
        -- attacks on verzik when attacking" - every swing with a special's
        -- energy is a special, enraged too; the wield is a held entry the
        -- plan prices, the special rides a swing the plan already takes)
        local affordable = F.energy >= sp.cost
        if QD.T2.budget and QD.T2.budget.spec_ok then affordable = QD.T2.budget.spec_ok(S, sp.cost) end
        if attacking and affordable and QD.raid._tob_count(S, sp.id) > 0
            and (not sp.two_handed or QD.T2.exec.free_slots(S) >= 1) then
            enter("wield", "energy " .. F.energy)
        end
    end
    if sp.state == "wield" then
        if QD.raid._tob_worn(S, sp.id) then
            sp.energy = F.energy
            enter("armed", sp.name .. " in hand")
        else
            I.gear = { { obj = sp.id, two_hand = sp.two_handed } }
        end
    end
    if sp.state == "armed" then
        if F.energy <= sp.energy - sp.cost + 10 then
            sp.fired = sp.fired + 1
            sp.energy = F.energy
            if F.energy < sp.cost then enter("back", "fired, energy " .. F.energy) end
        elseif not QD.raid._tob_worn(S, sp.id) then
            enter("back", "not in hand")
        end
        if sp.state == "armed" then I.spec, I.spec_cost = true, sp.cost end
    end
    if sp.state == "back" then
        local back = {}
        if not QD.raid._tob_worn(S, S.ids.weapon) then back[#back + 1] = S.ids.weapon end
        if sp.two_handed and not QD.raid._tob_worn(S, sp.offhand) and QD.raid._tob_count(S, sp.offhand) > 0 then
            back[#back + 1] = sp.offhand
        end
        if #back > 0 then
            I.gear = back
        else
            enter("idle", "back in hand")
        end
    end
    return sp.state
end

-- ==================================================================== PREDICT

function VZ.predict(S, F, B)
    VZ.observe(S, F)
    local b = F.boss
    if b == nil or S.base == nil then return end
    local X, V = QD.VZP3_V2, QD.VZP3
    local now, hz = F.tick, F.tick + X.H
    local base = S.base
    local ax0, az0, ax1, az1 = base.x + V.ARENA.x0, base.z + V.ARENA.z0, base.x + V.ARENA.x1, base.z + V.ARENA.z1
    B.prayer_factor = { magic = X.PRAYED, ranged = X.PRAYED, melee = 1 }
    B.edge{ x0 = ax0, z0 = az0, x1 = ax1, z1 = az1, margin = X.EDGE, weight = X.EDGE_WEIGHT }

    -- HER AUTOS, on every raider wherever (P4): the one in flight with its
    -- style (the prayer at landing applied by B), the slots ahead pre-scaled
    -- by the right prayer (seen at the slot, pressed then: in force before
    -- the landing, R5). Only a landing within AUTO_SOON carries its max into
    -- hp_worst: a later one can still be eaten for (the rescue eats on a
    -- predicted death, not on the sum of the next twelve ticks).
    local P = S.vz
    if P.auto_at and P.auto_at + X.AUTO_LAND >= now + 1 then
        local L = P.auto_at + X.AUTO_LAND
        local style = (S.prot == "magic") and "magic" or "ranged"
        B.hit{ name = "auto", x0 = ax0, z0 = az0, x1 = ax1, z1 = az1, t0 = L - 1, t1 = L - 1,
            dmg_exp = X.AUTO_EXP / X.PRAYED, dmg_max = X.AUTO_MAX, style = style }
    end
    if S.next_slot then
        local cad = F.enraged and V.CADENCE_ENRAGED or V.CADENCE
        local special = V.ROTATION[S.special]
        local i = 0
        while true do
            local T = S.next_slot + i * cad
            local L = T + X.AUTO_LAND
            if T - 1 > hz or i > S.attacks_left or (i == S.attacks_left and special ~= "crabs") then break end
            if L >= now + 1 and T > now then
                local soon = L <= now + X.AUTO_SOON
                B.hit{ name = "auto-slot", x0 = ax0, z0 = az0, x1 = ax1, z1 = az1, t0 = L - 1, t1 = L - 1,
                    dmg_exp = X.AUTO_EXP, dmg_max = soon and math.floor(X.AUTO_MAX * X.PRAYED) or X.AUTO_EXP }
            end
            i = i + 1
        end
    end

    -- HER MELEE at the scans (the end of every slot-1): her tank at range 1
    -- after her follow step brings 50% of 63 on all beside her. Her known
    -- tank: under her (the inner 5x5) or 3+ off; Hard Mode with no tank
    -- known: 3+ off, never under (oracle T4).
    if not VZ.exempt(S, F) then
        -- (NOT the known tank when enraged: sent 3+ off every 5 ticks she
        -- walked after it, and the kite chased her - vp_v2d 13 ticks a swing)
        local away = not VZ.tank_known(S)
        for _, sc in ipairs(F.scans) do
            if away then
                B.zone{ name = "scan-away", x = b.x, z = b.z, size = V.SIZE, lo = 0, hi = 2, t0 = sc.t0, t1 = sc.t1,
                    tier = "lethal" }
            else
                B.zone{ name = "scan", x = b.x, z = b.z, size = V.SIZE, lo = 1, hi = 2, t0 = sc.t0, t1 = sc.t1,
                    tier = "lethal" }
                B.zone{ name = "scan-ring", x = b.x + 1, z = b.z + 1, size = V.SIZE - 2, lo = 1, hi = 1,
                    t0 = sc.t0, t1 = sc.t1, tier = "lethal" }
            end
        end
    end

    -- CRAB BLASTS (VZ.crabs, the content's rule; every band forbidden, ZERO
    -- AVOIDABLE): a blast decided (arrival seen one tick ahead) is gap <= 3
    -- at its read; a crab not arrived is gap <= 1 at every tick end (from 2
    -- a run's two tiles escape the next tick's blast, from 1 nothing does).
    -- A crab shot down blasts from v1's kill window (S.kill_blasts).
    local n = X.CRAB_SIZE
    local function band(name, x, z, g, t0, t1)
        if t1 < now + 1 then return end
        B.hit{ name = name, x0 = x - g, z0 = z - g, x1 = x + n - 1 + g, z1 = z + n - 1 + g,
            t0 = math.max(t0, now + 1), t1 = t1, dmg_exp = 26, dmg_max = 63, forbid = true }
    end
    for _, c in pairs(S.vz.crab or {}) do
        if c.seen == now and not c.blasted then
            if c.read_lo then
                band("crab-blast", c.x, c.z, X.BLAST_FAR, c.read_lo, c.read_hi)
            else
                if c.maybe then band("crab-maybe", c.x, c.z, X.BLAST_FAR, c.maybe.lo, c.maybe.hi) end
                local wide = (c.owner == nil) and 1 or 0
                for k, pos in ipairs(c.path) do
                    band("crab-near", pos.x, pos.z, 1 + ((k == 1) and wide or 0), now + k, now + k)
                end
                if c.owner == F.pid then
                    -- my own crab: gap 2 lights its fuse - escapable, priced
                    B.zone{ name = "my-crab", x = c.path[1].x, z = c.path[1].z, size = n, lo = 0, hi = 2,
                        t0 = now + 1, t1 = hz, tier = "soft", cost = X.MY_CRAB_COST }
                end
            end
        end
    end
    for _, bl in ipairs(S.kill_blasts or {}) do band("crab-killed", bl.x, bl.z, X.BLAST_FAR, bl.from, bl.to) end
    -- KEEP A LEGAL TILE OPEN (vg_bone vp6 t1032-1037: three crabs arriving
    -- at once in the north-east corner, every tile in a blast; vp8 t990-1000
    -- on the east edge). A blast is decided one tick ahead, so the planner
    -- sees it one tick before it must be out of gap 3; it cannot see the
    -- corner closing. Two soft terms while a crab is up:
    --  - an arrival a tick LATER, from the owner's step: an owner within 4
    --    of the crab's next tile can be within 2 after its own run; the blast
    --    band at now+2 is priced (CRAB_EARLY), so seats leave early;
    --  - the edge: a seat ends ticks EDGE_CRAB_MARGIN off the walls, so a
    --    two-tile run out of any band always has open floor behind it.
    local live = false
    for _, c in pairs(S.vz.crab or {}) do
        if c.seen == now and not c.blasted then
            live = true
            if c.read_lo == nil and now + 2 - c.born_lo > 3 then
                local pos = c.path[1]
                local soon = false
                for _, rd in ipairs(F.raiders) do
                    if (c.owner == nil or rd.pid == c.owner) and rd.pid ~= F.pid
                        and QD.raid._tob_gap(rd.x, rd.z, pos.x, pos.z, n) <= X.CRAB_EARLY_REACH then soon = true end
                end
                if soon then
                    local r2 = math.max(c.born_lo + 6, now + 2)
                    if r2 <= hz then
                        B.zone{ name = "crab-early", x0 = pos.x - X.BLAST_FAR, z0 = pos.z - X.BLAST_FAR,
                            x1 = pos.x + n - 1 + X.BLAST_FAR, z1 = pos.z + n - 1 + X.BLAST_FAR, lo = 0, hi = 0,
                            t0 = r2, t1 = r2, tier = "soft", cost = X.CRAB_EARLY }
                    end
                end
            end
        end
    end
    if live then
        B.edge{ x0 = ax0, z0 = az0, x1 = ax1, z1 = az1, margin = X.EDGE_CRAB_MARGIN, weight = X.EDGE_CRAB_WEIGHT }
    end

    -- WEBS: seen ones (v1 S.web_hazards) forbidden with the run's middle; my
    -- own tick-end tiles in the throws' window: MARKS ahead, hits behind
    for _, h in ipairs(S.web_hazards) do
        if h.to >= now + 1 then
            B.hit{ name = "web", x = h.x, z = h.z, t0 = math.max(h.from, now + 1), t1 = h.to, sample = "pass",
                dmg_exp = X.WEB_DMG, dmg_max = X.WEB_MAX, forbid = true }
        end
    end
    local th0, th1 = VZ.throws(S)
    if th0 then
        for e0 = th0, now - 1 do
            local at = QD.T2.tile_at(S, e0)
            if at and e0 + X.WEB_STAY + X.WEB_LIFE + 1 >= now + 1 then
                B.hit{ name = "own-web", x = at.x, z = at.z, t0 = math.max(e0 + X.WEB_STAY, now + 1),
                    t1 = e0 + X.WEB_STAY + X.WEB_LIFE + 1, sample = "pass", dmg_exp = X.WEB_DMG, dmg_max = X.WEB_MAX,
                    forbid = true }
            end
        end
        for tm = math.max(now, th0), math.min(now + X.MARK_AHEAD, th1) do
            B.mark{ name = "own-web-mark", t_mark = tm, r = 0, t0 = tm + X.WEB_STAY,
                t1 = tm + X.WEB_STAY + X.WEB_LIFE + 1, sample = "pass", dmg_exp = X.WEB_DMG, dmg_max = X.WEB_MAX,
                forbid = true }
        end
    end
    -- the webs' arrival: nobody under the centre from the slot to the spin
    -- (the knockback, v1); her footprint during the spin is a hard npc's
    local webs_due = S.webs_at
    if webs_due == nil and S.attacks_left == 0 and V.ROTATION[S.special] == "webs" and S.next_slot then
        webs_due = S.next_slot
    end
    if webs_due and S.spin_at == nil and webs_due + 12 >= now + 1 then
        local cx, cz = base.x + V.CENTRE.x - 3, base.z + V.CENTRE.z - 3
        B.hit{ name = "knockback", x = cx, z = cz, size = V.SIZE, t0 = math.max(webs_due, now + 1),
            t1 = webs_due + 12, sample = "pass", dmg_exp = 10, dmg_max = 10, forbid = true }
    end
    if S.webs_at and S.spin_at then
        B.hit{ name = "her-webs", x = b.x, z = b.z, size = V.SIZE, t0 = now + 1, t1 = hz, sample = "pass",
            dmg_exp = 0, dmg_max = 0, forbid = true }
    end

    -- THE YELLOWS: every pool but mine[k] at strike k's read
    local Y = S.hm_y
    if Y and Y.mine then
        for k = 1, 3 do
            local read = Y.strikes[k] - 1
            local mine = Y.mine[k]
            if read >= now + 1 and read <= hz then
                for _, p in ipairs(Y.pools) do
                    if p.x ~= mine.x or p.z ~= mine.z then
                        B.hit{ name = "other-pool" .. k, x = p.x, z = p.z, t0 = read, t1 = read,
                            dmg_exp = 80, dmg_max = 80, forbid = true }
                    end
                end
            end
        end
    end

    -- MY TORNADOES (v1 owners): chasers, the v1 rule in the planner; a touch
    -- is half my hp and she heals three times that
    local mine = {}
    for _, tn in ipairs(F.tornadoes) do
        if tn.mine then mine[#mine + 1] = { x = tn.x, z = tn.z, slot = tn.slot, d = cheb(tn.x, tn.z, F.me.x, F.me.z) } end
    end
    table.sort(mine, function(p, q) if p.d ~= q.d then return p.d < q.d end return p.slot < q.slot end)
    -- (the tornado view, into the decision log: slot@tile and m = mine, o<pid> = owner, - = not mine)
    if #F.tornadoes > 0 and api_drive.ticklog_mark ~= nil then
        local parts = {}
        for _, tn in ipairs(F.tornadoes) do
            parts[#parts + 1] = tn.slot .. "@" .. tn.x .. "," .. tn.z .. (tn.mine and "m" or "-") .. (tn.owner and ("o" .. tn.owner) or "")
        end
        api_drive.ticklog_mark("vz p" .. tostring(F.pid) .. " t" .. now .. " @" .. F.me.x .. "," .. F.me.z .. " tn " .. table.concat(parts, " "))
    end
    local half = F.hp / 2
    for i, tn in ipairs(mine) do
        if i > QD.T2.V.CHASERS_MAX then break end
        -- (ZERO AVOIDABLE, owner 2026-10-12: a touch is a RULE, not priced)
        B.chaser{ name = "tornado " .. tn.slot, x = tn.x, z = tn.z, forbid = true,
            dmg_exp = half * (1 + X.TORNADO_HEAL * X.HP_PER_BOSS_HP), dmg_max = half }
    end
end

-- =================================================================== STRATEGY

-- Her as a target, over the ticks she takes a swing: not in her yellows'
-- charge (from the last strike), not on her webs walk, not from the slot of
-- a webs or yellows special (lesson 21: a refused press stands me still),
-- not in my ball hold (lesson 47: a hold is a walk; an attack follows her off
-- the tile). -> the index, or nil.
function VZ.her_target(S, F, B, I, hold_read, hold_hi)
    local X, V = QD.VZP3_V2, QD.VZP3
    local b = F.boss
    if b == nil then return nil end
    local now, hz = F.tick, F.tick + X.H
    local ok0, ok1 = now + 1, now + QD.T2.V.T_OK_FOREVER
    if F.charging and S.blast then ok0 = math.max(ok0, S.blast + 1) end
    if S.webs_at and S.spin_at == nil then return nil end
    local special = V.ROTATION[S.special]
    if S.attacks_left == 0 and (special == "webs" or special == "yellows") and S.next_slot and not F.charging then
        ok1 = math.min(ok1, S.next_slot - 1)
    end
    if hold_read then
        if now + 1 >= hold_read - 1 and now + 1 <= (hold_hi or hold_read) + 1 then return nil end
        if hold_read - 1 <= hz then ok1 = math.min(ok1, hold_read - 2) end
    end
    if ok0 > hz or ok1 < now + 1 then return nil end
    -- MY TORNADO AT MY HEELS: no attack at step 1 that is not a swing from
    -- where I stand. A press that needs a chase step leaves the step to her
    -- (she moves first; vp_v2a: 16 of 16 touches were a predicted chase step
    -- the server never took, the seat stood, the tornado stepped on it), and
    -- with it adjacent any stand is a touch.
    local near = 99
    for _, tn in ipairs(F.tornadoes) do
        if tn.mine then near = math.min(near, cheb(tn.x, tn.z, F.me.x, F.me.z)) end
    end
    -- (only adjacent: a step-1-only ban at 2 made every plan put its swing
    -- at step 2 and never take it - vp_v2c: the kite 11.7 ticks a swing.
    -- Adjacent, any stand is a touch, so the right step 1 is a run anyway.)
    -- (no ban. With it ADJACENT the kite's swings are chase presses: banned
    -- outright the kite fell from 4.7 to 14.8 ticks a swing (vp_v2a_now,
    -- vp_v2f); banned unless her known tank stood beside her, 3/8 (vp_v2h).
    -- The touches that remain are presses judged before her npc step: a GAP.)
    if ok0 > hz then return nil end
    local sp = S.vz.spec
    -- THE SWING AT MY CURRENT LEVELS (TOB2_DESIGN.md RESOURCE layer, item 4):
    -- SWING_EXP was measured at about +10 boosted; the hit's max scales with
    -- effective Strength and the hit chance roughly with effective Attack
    -- (PROBE: a linear model of both, normalised at base + 10)
    local lf = 1
    local la, ls = F.levels and F.levels.attack, F.levels and F.levels.strength
    if la and ls and la.base > 0 and ls.base > 0 then
        lf = ((la.level + 8) / (la.base + 18)) * ((ls.level + 8) / (ls.base + 18))
    end
    local value = X.SWING_EXP * lf * X.HP_PER_BOSS_HP
    if sp and sp.state == "armed" then value = sp.exp * lf * X.HP_PER_BOSS_HP end
    local path, alt, alts = VZ.her_path(S, F)
    local i = B.target{ name = "her", npc = b, size = V.SIZE, range = 1, speed = VZ.speed(S, I), value = value,
        path = path, t_ok0 = ok0, t_ok1 = ok1 }
    -- HER OTHER STEP (C batch 5's path_alt: the chase scored as the WORSE of
    -- the two): a step TOWARD ME. vp_v2j: of 76 touches, 66 followed a chase
    -- press, and in most of them she stepped toward the seat - it stayed in
    -- reach, stood, and its tornado touched. (B.target does not forward
    -- path_alt: set on the spec it built - a GAP in tob2_plan.lua.)
    if alt then B.spec.targets[i].path_alt = alt end
    if alts then B.spec.targets[i].path_alts = alts end
    return i
end

-- HER FOOTPRINT AHEAD (T1, v1 header): she follows her known tank a step a
-- tick (the naive step, each axis toward it) while it is 2+ off her
-- footprint, stands while it is beside her; under her she steps at random
-- (held). The webs and the yellows suspend the follow. The tank's tile is
-- its tile now (her step reads the end of the tick before).
function VZ.her_path(S, F)
    local X, V = QD.VZP3_V2, QD.VZP3
    local b = F.boss
    local x, z = b.x, b.z
    local path = { { x = x, z = z } }
    local tank = nil
    if VZ.tank_known(S) and S.webs_at == nil and not F.charging then
        for _, rd in ipairs(F.raiders) do if rd.pid == S.tank_pid then tank = rd end end
    end
    -- (her NEXT step only, then held: followed over the whole horizon the
    -- kite waited for her to come, vp_v2e 16 ticks a swing; one step is the
    -- one a chase press meets, vp_v2a's touches)
    -- TWO OUTCOMES, scored as the worse (C batch 5's path_alt): she HOLDS
    -- (path), or she STEPS (path_alt): toward her known tank while it is
    -- 2+ off her, else toward me on each axis I am off her footprint. vp_v2k
    -- with path = the follow step and alt = toward me: 58 touches, and half
    -- of them were her standing still where the follow was predicted (vp1
    -- t1071: four seats at once) - the hold was in neither outcome.
    local ax, az = x, z
    -- SOMEONE UNDER HER: she steps one random cardinal tile (oracle T1).
    -- vp_v2l: 26 of 37 touches had her step with a seat under her. The
    -- outcome that hurts is the step that leaves ME beside her (in reach: I
    -- stand, and my tornado touches): that is the alternative.
    local under = false
    for _, rd in ipairs(F.raiders) do
        if QD.raid._tob_gap(rd.x, rd.z, x, z, V.SIZE) == 0 then under = true end
    end
    if under and S.webs_at == nil and not F.charging then
        for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
            if ax == x and az == z and QD.raid._tob_beside(F.me.x, F.me.z, x + d[1], z + d[2], V.SIZE)
                and not QD.raid._tob_beside(F.me.x, F.me.z, x, z, V.SIZE) then
                ax, az = x + d[1], z + d[2]
            end
        end
        if ax == x and az == z then
            -- (beside her already: the step that takes me OUT of reach)
            for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
                if ax == x and az == z and not QD.raid._tob_beside(F.me.x, F.me.z, x + d[1], z + d[2], V.SIZE) then
                    ax, az = x + d[1], z + d[2]
                end
            end
        end
    elseif tank and QD.raid._tob_gap(tank.x, tank.z, x, z, V.SIZE) >= 2 then
        if tank.x < x then ax = x - 1 elseif tank.x > x + V.SIZE - 1 then ax = x + 1 end
        if tank.z < z then az = z - 1 elseif tank.z > z + V.SIZE - 1 then az = z + 1 end
    else
        if F.me.x < x then ax = x - 1 elseif F.me.x > x + V.SIZE - 1 then ax = x + 1 end
        if F.me.z < z then az = z - 1 elseif F.me.z > z + V.SIZE - 1 then az = z + 1 end
    end
    -- MY TORNADO WITHIN 2: the outcome that costs me is ANY step of hers that
    -- puts my own tile in reach - the press then stands me (C batch 5) and
    -- the tornado steps on. vp_v2p: 20 of 21 touches were a chase step onto
    -- the tornado's side with her real step (a random one, or toward
    -- another seat) leaving the seat in reach. Her possible steps: the 8
    -- (a follow is diagonal-capable, a random step cardinal).
    local chased = false
    for _, tn in ipairs(F.tornadoes) do
        -- (within 3: a chase press runs 2 tiles, and toward a tornado 3 off
        -- it ends beside it - vq_bone vp6 t1123, t1126)
        if tn.mine and cheb(tn.x, tn.z, F.me.x, F.me.z) <= QD.VZP3_V2.ALTS_GATE then chased = true end
    end
    -- (a live crab near me gates them too: a chase press whose end tile
    -- moved with her step ended within 1 of an arriving crab, bm3_all vp5
    -- t913 - the crab's arrival band is then a rule the plan must see on
    -- every one of her steps)
    for _, c in pairs(S.vz.crab or {}) do
        if c.seen == F.tick and not c.blasted and QD.raid._tob_gap(F.me.x, F.me.z, c.x, c.z, 2) <= QD.VZP3_V2.ALTS_GATE + 1 then
            chased = true
        end
    end
    -- (and BESIDE her, the step that takes me OUT of reach: an attack
    -- under way then follows her, off the tile the plan held, toward the
    -- tornado - vp_v2q vp2 t1001)
    -- ALL NINE (C batch 7's path_alts, the worst chase scored): with my
    -- tornado within 2, hold plus her 8 steps - 15 of 17 batch-8 touches
    -- were a step that neither the hold nor one alternative named
    -- (herwhy.py: most match no follow toward any seat)
    for _ = 1, X.H do path[#path + 1] = { x = x, z = z } end
    if chased and S.webs_at == nil and not F.charging then
        local alts = {}
        for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } }) do
            local a = { { x = x, z = z } }
            for _ = 1, X.H do a[#a + 1] = { x = x + d[1], z = z + d[2] } end
            alts[#alts + 1] = a
        end
        S.vz.alts_n = (S.vz.alts_n or 0) + 1
        return path, nil, alts
    end
    local alt = nil
    if ax ~= x or az ~= z then
        alt = { { x = x, z = z } }
        for _ = 1, X.H do alt[#alt + 1] = { x = ax, z = az } end
    end
    return path, alt
end

-- THE STACK (v1 web run's tile, S.wr) as soft require-zones: the tile now
-- until its first own web is due (WEB_STAY tick-ends after it is first read
-- in the throws' window), then a ring tile clockwise every WEB_STAY.
function VZ.stack(S, F, B)
    local X, V = QD.VZP3_V2, QD.VZP3
    local W = S.wr
    if W == nil or W.state == "done" or W.tile == nil then return false end
    local now, hz = F.tick, F.tick + X.H
    local ring = QD.raid._vzp3_ring(S.base.x + V.CENTRE.x - 3, S.base.z + V.CENTRE.z - 3)
    local L = #ring
    local th0, th1 = VZ.throws(S)
    if th0 == nil then
        B.zone{ name = "stack-gather", x = W.tile.x, z = W.tile.z, size = 1, lo = 0, hi = 0, require = true,
            t0 = now + 1, t1 = hz, tier = "soft", cost = X.STACK_COST }
        return true
    end
    local arrived = (S.vz.wr_moved and (S.vz.wr_moved + 1)) or th0
    local exp = math.max(arrived, th0) + X.WEB_STAY
    local idx, t0 = W.idx, now + 1
    while t0 <= hz do
        local t1 = math.min(exp - 1, hz)
        if t1 >= t0 then
            local p = ring[((idx - 1) % L) + 1]
            B.zone{ name = "stack", x = p.x, z = p.z, size = 1, lo = 0, hi = 0, require = true, t0 = t0, t1 = t1,
                tier = "soft", cost = X.STACK_COST }
            t0 = t1 + 1
        end
        idx, exp = idx - 1, exp + X.WEB_STAY
        if exp > th1 + X.WEB_STAY + 1 then break end
    end
    B.pull{ name = "stack-pull", x = W.tile.x, z = W.tile.z, size = 1, weight = X.STACK_PULL, t0 = now + 1, t1 = hz }
    return true
end

-- THE BALL'S CHAIN (v1 _vzp3_spec's terms). -> the role, my read tick.
function VZ.ball(S, F, B)
    local X = QD.VZP3_V2
    if S.ball == nil then return nil end
    local role, ttile, next_pid, ntile = VZ.ball_chain(S, F)
    if role == nil then return nil end
    local now, land = F.tick, S.ball.land
    local land_hi = math.max(land, S.ball.land_hi or land)
    local follow = X.BALL_DESIGN == "follow"
    if X.BALL_DESIGN == "meet" then
        -- like the yellows: rules ONLY at the read ticks, no hold, no pull
        if role == "target" or role == "next" then
            local read, read_hi = VZ.ball_read(S, F, role), VZ.ball_read(S, F, role, land_hi)
            if role == "target" and #S.ball.order >= 4 and land >= now + 1 then
                local A, base = QD.VZP3.ARENA, S.base
                B.hit{ name = "ball-last", x0 = base.x + A.x0, z0 = base.z + A.z0, x1 = base.x + A.x1, z1 = base.z + A.z1,
                    t0 = land - 1, t1 = land - 1, dmg_exp = 49, dmg_max = 49 }
            end
            B.zone{ name = "ball-" .. role, x = ttile.x, z = ttile.z, size = 1, lo = 0, hi = (role == "target") and 0 or 1,
                require = true, t0 = read, t1 = read_hi, tier = "lethal" }
            return role, read, read_hi
        end
        B.zone{ name = "ball-clear", x = ttile.x, z = ttile.z, size = 1, lo = 0, hi = 1, t0 = land - 1, t1 = land_hi,
            tier = "lethal" }
        return role, nil
    end
    -- THE LAST HOLDER EATS IT (~tob_verzik_ball_land: Hard Mode's hops end at
    -- 3, the next holder takes 49): unavoidable, priced for the rescue
    if role == "target" and #S.ball.order >= 4 and land >= now + 1 then
        local A, base = QD.VZP3.ARENA, S.base
        B.hit{ name = "ball-last", x0 = base.x + A.x0, z0 = base.z + A.z0, x1 = base.x + A.x1, z1 = base.z + A.z1,
            t0 = land - 1, t1 = land - 1, dmg_exp = 49, dmg_max = 49 }
    end
    if role == "target" then
        local read, read_hi = VZ.ball_read(S, F, role), VZ.ball_read(S, F, role, land_hi)
        if follow then
            -- the target kites freely, EXCEPT across its read: from the tick
            -- before it, it holds the tile it stands on (the next follows a
            -- tick behind; a target that kited off on its read tick left the
            -- next 2 away, bf_vp vp1 t937). A hold is a walk (lesson 47).
            if now + 1 >= read - 1 and now + 1 <= read_hi then
                B.zone{ name = "ball-hold", x = F.me.x, z = F.me.z, size = 1, lo = 0, hi = 0, require = true,
                    t0 = now + 1, t1 = read_hi, tier = "lethal" }
                return role, read, read_hi
            end
            return role, nil, nil
        end
        -- fixed: hold the tile from first sight to the read (a walk, lesson 47)
        B.zone{ name = "ball-hold", x = ttile.x, z = ttile.z, size = 1, lo = 0, hi = 0, require = true,
            t0 = now + 1, t1 = read_hi, tier = "lethal" }
        return role, read, read_hi
    end
    if role == "next" then
        local read, read_hi = VZ.ball_read(S, F, role), VZ.ball_read(S, F, role, land_hi)
        if follow then
            -- near the target every tick (soft, above a swing), within 1 of it
            -- at the read (a rule)
            if read - 1 >= now + 1 then
                B.zone{ name = "ball-near", x = ttile.x, z = ttile.z, size = 1, lo = 0, hi = 1, require = true,
                    t0 = now + 1, t1 = read - 1, tier = "soft", cost = X.BALL_NEAR_COST }
            end
            B.zone{ name = "ball-next", x = ttile.x, z = ttile.z, size = 1, lo = 0, hi = 1, require = true,
                t0 = read, t1 = read_hi, tier = "lethal" }
            return role, read, read_hi
        end
        local t = ntile or ttile
        local hi = ntile and 0 or 1
        B.zone{ name = "ball-next", x = t.x, z = t.z, size = 1, lo = 0, hi = hi, require = true,
            t0 = read, t1 = read_hi, tier = "lethal" }
        if read - 1 >= now + 1 then
            B.pull{ name = "ball-pull", x = t.x, z = t.z, size = 1, weight = X.BALL_PULL, t0 = now + 1, t1 = read - 1 }
        end
        return role, read, read_hi
    end
    -- EVERYONE ELSE: not within 1 of the target at the read ticks (a lower
    -- pid is read at the landing, a higher one a tick before); 2 priced
    B.zone{ name = "ball-clear", x = ttile.x, z = ttile.z, size = 1, lo = 0, hi = 1, t0 = land - 1, t1 = land_hi,
        tier = "lethal" }
    B.zone{ name = "ball-clear2", x = ttile.x, z = ttile.z, size = 1, lo = 2, hi = 2, t0 = land - 1, t1 = land_hi,
        tier = "soft", cost = X.BALL_CLEAR2_COST }
    return role, nil
end

-- THE YELLOWS (the HM split): on mine[k] at strike k's read, pulled to the next.
function VZ.pools(S, F, B)
    local X = QD.VZP3_V2
    local Y = S.hm_y
    if Y == nil or Y.mine == nil then return false end
    local next_k = nil
    for k = 1, 3 do
        local read = Y.strikes[k] - 1
        local mine = Y.mine[k]
        if read >= F.tick + 1 then
            if next_k == nil then next_k = k end
            B.zone{ name = "pool" .. k, x = mine.x, z = mine.z, size = 1, lo = 0, hi = 0, require = true,
                t0 = read, t1 = read, tier = "lethal" }
        end
    end
    if next_k then
        local m = Y.mine[next_k]
        B.pull{ name = "pool-pull", x = m.x, z = m.z, size = 1, weight = X.POOL_PULL, t0 = F.tick + 1,
            t1 = Y.strikes[next_k] - 1 }
    end
    return true
end

-- A seat's side of her ring, so the five do not stand in one clump (the ball
-- explodes on two or more within 1 of its target).
function VZ.station(S, F, B, weight)
    local X = QD.VZP3_V2
    local b = F.boss
    local ring = QD.raid._vzp3_ring(b.x, b.z)
    local seat = QD.raid._tob_seat(F) or S.role
    local p = ring[((seat - 1) * 6 + 3) % #ring + 1]
    B.pull{ name = "station", x = p.x, z = p.z, size = 1, weight = weight or X.STATION_PULL, t0 = F.tick + 1, t1 = F.tick + X.H }
end

function VZ.strategy(S, F, B)
    VZ.observe(S, F)
    local X, P = QD.VZP3_V2, S.vz
    local I = {}
    local want = S.t2.want
    -- PRAYER: the overhead for her last style (v1 S.prot), the boost on the
    -- v1 budget (QD.VZ_PRAYER: Piety while the spare covers the overheads)
    local prot = (S.prot == "magic") and "protectfrommagic" or "protectfrommissiles"
    if F.prayer > 0 and not F.lit[prot] then I.pray = prot end
    local remaining = math.max(60, QD.VZ_PRAYER.P3_TICKS - (S.entered and (F.tick - S.entered) or 0))
    local spare = QD.raid.vz_prayer_spare(S.ids.inv, F.prayer, remaining)
    -- (the boost POTIONS are the room's: VZ.boosts below; the shared
    -- supplies keep food, the brew floor, the prayer restore and Piety)
    want.boost, want.boost_stat = (spare > 0) and "piety" or nil, nil
    -- (v1: Piety OFF once the spare no longer funds the overheads to the end)
    if spare <= 0 and F.lit.piety then I.pray_off = { "piety" } end
    want.eat_below, want.brew_below = X.EAT_FLOOR, X.BREW_FLOOR
    VZ.boosts(S, F, I)
    VZ.hp_safety(S, F, I)
    local mode
    if F.boss == nil then
        -- the change of form: nothing attacks, top up (v1 HP_TOP)
        want.eat_below, want.brew_below = X.TOP_UP, X.TOP_UP
        mode = "wait"
    else
        if VZ.special(S, F, I) == nil and F.weapon_held then I.gear = { S.ids.weapon } end
        if P.spec and P.spec.state == "idle" and F.weapon_held then I.gear = { S.ids.weapon } end
        local role, read, read_hi = VZ.ball(S, F, B)
        local hold = (role == "target" or role == "next") and read or nil
        -- THE BALL'S TARGET AND NEXT SWING ONLY FROM WHERE THEY STAND: an
        -- attack that needs a chase follows her, and her steps dragged the
        -- target 7 off its meeting tile (relay_r15_128 f71 t3121-3125, plan
        -- risk from k3, the un-bounced 74). Beside her: swing; else no her.
        if (role == "target" or role == "next") and F.boss
            and not QD.raid._tob_beside(F.me.x, F.me.z, F.boss.x, F.boss.z, QD.VZP3.SIZE) then
            hold = F.tick - 1
            read_hi = F.tick + X.H + 2
        end
        local tgt = VZ.her_target(S, F, B, I, hold, read_hi)
        if F.charging and VZ.pools(S, F, B) then
            mode = "yellows"
        elseif F.charging then
            mode = "yellows-wait"
        elseif role then
            mode = "ball-" .. role
        elseif not F.enraged and S.webs_at and VZ.stack(S, F, B) then
            mode = "web-" .. tostring(S.wr.state)
        elseif F.enraged then
            mode = "kite"
            -- (spread while a crab is up: seats sharing a tile and a view plan
            -- the same path, so four kited in one clump to the west edge and
            -- one crab's arrival caught all four, bm3_all vp5 t1138-1148; the
            -- seat's own station side breaks the tie)
            for _, c in pairs(P.crab or {}) do
                if c.seen == F.tick and not c.blasted then P.crab_up = F.tick end
            end
            if P.crab_up == F.tick then VZ.station(S, F, B, X.CRAB_SPREAD_PULL) end
        elseif VZ.exempt(S, F) then
            mode = "dps"
            VZ.station(S, F, B)
        elseif VZ.tank_known(S) then
            mode = "tank"
        else
            mode = "away"
        end
        -- UNDER HER sends her a random step a tick and breaks every seat's
        -- reach (vp_v2l): a soft price on ending a tick under her, for every
        -- mode but the tank's (its scans need the inner 5x5; the rule wins)
        if mode ~= "tank" and S.webs_at == nil then
            B.zone{ name = "under-her", x = F.boss.x, z = F.boss.z, size = QD.VZP3.SIZE, lo = 0, hi = 0,
                t0 = F.tick + 1, t1 = F.tick + X.H, tier = "soft", cost = X.UNDER_COST }
        end
        if tgt == nil and mode ~= "yellows" and mode ~= "yellows-wait" and not mode:find("^web") then
            -- (no swing to plan: stay near her side)
            B.pull{ name = "near-her", x = F.boss.x - 1, z = F.boss.z - 1, size = QD.VZP3.SIZE + 2, weight = 0.1,
                t0 = F.tick + 1, t1 = F.tick + X.H }
        end
    end
    P.modes[mode] = (P.modes[mode] or 0) + 1
    if mode ~= P.mode then
        QD.raid._vzp3_trace(S, F.tick, "mode " .. tostring(P.mode) .. " -> " .. mode)
        P.mode = mode
    end
    I.mode = mode
    return I
end

-- ======================================================================= ROOM

QD.T2.rooms.verzik_p3 = {
    init = function(S, opts)
        assert(opts.base, "verzik_p3_v2_solve: opts.base (the room origin, verzik_p1_prepare's) is required")
        S.base = opts.base
        S.who = "verzik_p3_v2"
        local v1 = QD.raid._vzp3_ids(opts.weapon)
        for _, k in ipairs({ "p3", "transition", "bat", "web", "tornado", "death_seqs", "crabs", "seq_melee",
                             "seq_magic", "seq_ranged", "seq_summon", "seq_webspin", "seq_powerblast",
                             "proj_ranged", "proj_magic", "pool", "ball", "web_proj", "weapon" }) do
            S.ids[k] = v1[k]
        end
        S.weapon_ticks_opt = opts.weapon_ticks
        S.hm = { web_run = true, kite = true }
        S.seen, S.web_seen, S.web_hazards, S.torn, S.crab_seen, S.blasts = {}, {}, {}, {}, {}, {}
        S.autos, S.melees, S.prot = 0, 0, "magic"
        S.t2.plan_opts = { h = QD.VZP3_V2.H, beam = QD.VZP3_V2.BEAM }
        S.vz = { modes = {}, mode = nil, taken = 0, last_hp = nil, auto_at = nil, spec = nil, boost_n = {},
                 main_ticks = opts.weapon_ticks or QD.VZP3.WEAPON_TICKS[opts.weapon or ""] or QD.VZP3.DEFAULT_TICKS }
        -- the spec weapon: opts.spec_weapon (the relay's), carried
        local name = opts.spec_weapon
        if name then
            local def = QD.VZP3_V2.SPEC_WEAPONS[name]
            assert(def, "verzik_p3_v2_solve: no special weapon named " .. tostring(name))
            local r, id = api_drive.symbol("obj", name)
            assert(r == "ok", "verzik_p3_v2_solve: no obj " .. name)
            local ro, off = api_drive.symbol("obj", "dragon_parryingdagger")
            assert(ro == "ok", "verzik_p3_v2_solve: no obj dragon_parryingdagger")
            S.vz.spec = { id = id, cost = def.cost, name = name, state = "idle", fired = 0, two_handed = def.two_handed,
                          ticks = def.ticks, exp = def.exp, offhand = off,
                          carried_at_start = QD.raid._tob_count(S, id) + (QD.raid._tob_worn(S, id) and 1 or 0) }
        end
    end,

    enter = function(S) end,
    predict = function(S, F, B) VZ.predict(S, F, B) end,
    strategy = function(S, F, B) return VZ.strategy(S, F, B) end,

    -- v1's end: the bat, or her form gone once seen
    done = function(S, F)
        VZ.observe(S, F)
        if F.bat or (S.entered and F.boss == nil and F.transition == nil) then return "ok" end
        return nil
    end,

    summary = function(S)
        local P = S.vz
        local bn = string.format("boosts restore %d combat %d (ticks left at the end %s); hp-safety brews %d",
            P.boost_n.restore or 0, P.boost_n.combat or 0, tostring(P.ticks_left and math.floor(P.ticks_left)),
            P.safety_n or 0)
        local modes = {}
        for k, n in pairs(P.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        local seen = {}
        for _, k in ipairs({ "crabs", "webs", "yellows", "ball" }) do seen[#seen + 1] = k .. " " .. tostring(S.seen[k] or 0) end
        return string.format("verzik_p3 v2: modes %s; taken %d; specials %s; autos %d (melee %d); tornado spawns %d; spec %s (carried %s) fired %d energy left %s; %s",
            table.concat(modes, " "), P.taken, table.concat(seen, ", "), S.autos, S.melees, S.tornado_spawns or 0,
            P.spec and P.spec.name or "-", P.spec and tostring(P.spec.carried_at_start) or "-", P.spec and P.spec.fired or 0, tostring(P.energy_last), bn)
    end,
}

function QD.raid.verzik_p3_v2_solve(opts)
    assert(QD.raid.tob_mode == "hard", "verzik_p3_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("verzik_p3", opts or {})
end
