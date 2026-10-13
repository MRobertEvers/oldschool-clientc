-- quest-driver / tob2_verzik_p3: VERZIK VITUR PHASE 3, HARD MODE, five raiders,
-- on the v2 layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md; the plan:
-- docs/minigames/theater_of_blood/tob2_rooms/verzik_p3.md).
--
--   QD.raid.verzik_p3_v2_solve(opts) -> result, detail, S
--   opts = the relay's v1 opts: { max_ticks, base (the room origin), weapon,
--   spec_weapon (default: the dragon warhammer, when carried) }
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
    SPEC_VALUE = 30,            -- a warhammer special: 1.5x the hit and her defence -30% for every later swing (PROBE)
    -- HER AUTOS: on every raider, halved by the matching overhead at landing
    -- (oracle P4); v1 measured 6.9 a hit; HM max 61 (QD.VZHM)
    AUTO_EXP = 7, AUTO_MAX = 61, AUTO_LAND = 3, AUTO_SOON = 3,
    PRAYED = 0.5,
    -- the tornado's touch: half my hp, and she heals three times that
    TORNADO_HEAL = 3,
    -- crab blasts: within 2 of its 2x2 63/26 (forbidden), at 3 a flat 7-8 (priced)
    BLAST_NEAR = 2, BLAST_FAR = 3, BLAST_FAR_DMG = 8, CRAB_SIZE = 2,
    -- webs: on one from a tick before it lands to its explosion, the middle
    -- of a run too (forbidden); my own tick-end tiles in the throws' window
    -- draw a web WEB_STAY tick-ends on (v1 kite's own_web)
    WEB_DMG = 20, WEB_MAX = 40, WEB_STAY = 4, WEB_LIFE = 12, MARK_AHEAD = 7,
    -- the stack (v1 web run) as soft costs: off its tile a tick costs this
    STACK_COST = 2.0, STACK_PULL = 0.5,
    -- the ball and the yellows' approach pulls (v1 1.5)
    BALL_PULL = 1.5, BALL_MARGIN = 30, POOL_PULL = 1.5,
    STATION_PULL = 0.05,        -- a seat's side of her ring (the ball explodes on 2+ round its target)
    EDGE = 3, EDGE_WEIGHT = 0.3,
    -- FLOORS (QD.VZHM 75/70 is over an UNPRAYED 61; every auto is prayed,
    -- oracle P4, and QD.T2.rescue eats on a predicted death)
    EAT_FLOOR = 65, BREW_FLOOR = 65, TOP_UP = 90,
    SPEC_RETRY = 2,
    SPEC_WEAPONS = { dragon_warhammer = { cost = 500 } },
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
    for _, rd in ipairs(F.raiders) do
        local lx, lz = rd.x - base.x, rd.z - base.z
        if lx >= A.x0 and lx <= A.x1 and lz >= A.z0 and lz <= A.z1 then
            alive[#alive + 1] = rd
            if not rd.me then mates[#mates + 1] = rd end
        end
    end
    F.raiders, F.mates = alive, mates
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
    QD.raid._vzp3_clock(S, F)
    if S.autos ~= autos and not F.ball_thrown then P.auto_at = F.tick end
    QD.raid._vzp2_crabs(S, F)
    QD.raid._vzp3_tornadoes(S, F)
    VZ.yellows(S, F)
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

-- ================================================================== HELPERS

-- Her melee is off me at the scans? (v1: her known tank is someone else)
function VZ.exempt(S, F)
    return S.tank_ok and S.tank_pid ~= nil and F.pid ~= nil and S.tank_pid ~= F.pid
end

function VZ.tank_known(S)
    return S.tank_ok == true and S.tank_pid ~= nil
end

-- The ball's read tick for my role (v1 _vzp3_decide's `holding`).
function VZ.ball_read(S, F, role)
    local land = S.ball.land
    if role == "target" then return land - 1 end
    return (F.pid < S.ball.cur) and land or (land - 1)
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
        if sp and I.gear[1] == sp.id then return QD.VZP3.WEAPON_TICKS[sp.name] or QD.VZP3.DEFAULT_TICKS end
        if I.gear[1] == S.ids.weapon then return S.vz.main_ticks end
    end
    return QD.raid._vzp3_weapon_ticks(S)
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
    local last = S.t2.last
    local attacking = last ~= nil and last.path[1].act == "attack"
    if sp.state == "idle" then
        if attacking and F.energy >= sp.cost and QD.raid._tob_count(S, sp.id) > 0 and not F.enraged then
            enter("wield", "energy " .. F.energy)
        end
    end
    if sp.state == "wield" then
        if QD.raid._tob_worn(S, sp.id) then
            sp.energy = F.energy
            enter("armed", sp.name .. " in hand")
        else
            I.gear = { sp.id }
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
        if sp.state == "armed" then I.spec = true end
    end
    if sp.state == "back" then
        if not QD.raid._tob_worn(S, S.ids.weapon) then
            I.gear = { S.ids.weapon }
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

    -- CRAB BLASTS (v1 _vzp2_crabs: S.blasts, the 2x2 SW, the window of the
    -- read): within 2 out (63, 26), at 3 a flat 7-8 priced
    for _, bl in ipairs(S.blasts) do
        local n = X.CRAB_SIZE
        B.hit{ name = "blast", x0 = bl.x - X.BLAST_NEAR, z0 = bl.z - X.BLAST_NEAR, x1 = bl.x + n - 1 + X.BLAST_NEAR,
            z1 = bl.z + n - 1 + X.BLAST_NEAR, t0 = bl.from, t1 = bl.to, dmg_exp = 26, dmg_max = 63, forbid = true }
        B.hit{ name = "blast-far", x0 = bl.x - X.BLAST_FAR, z0 = bl.z - X.BLAST_FAR, x1 = bl.x + n - 1 + X.BLAST_FAR,
            z1 = bl.z + n - 1 + X.BLAST_FAR, t0 = bl.from, t1 = bl.to, dmg_exp = X.BLAST_FAR_DMG, dmg_max = X.BLAST_FAR_DMG }
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
        B.chaser{ name = "tornado " .. tn.slot, x = tn.x, z = tn.z,
            dmg_exp = half * (1 + X.TORNADO_HEAL * X.HP_PER_BOSS_HP), dmg_max = half }
    end
end

-- =================================================================== STRATEGY

-- Her as a target, over the ticks she takes a swing: not in her yellows'
-- charge (from the last strike), not on her webs walk, not from the slot of
-- a webs or yellows special (lesson 21: a refused press stands me still),
-- not in my ball hold (lesson 47: a hold is a walk; an attack follows her off
-- the tile). -> the index, or nil.
function VZ.her_target(S, F, B, I, hold_read)
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
        if now + 1 >= hold_read - 1 and now + 1 <= hold_read + 1 then return nil end
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
    local value = X.SWING_EXP * X.HP_PER_BOSS_HP
    if sp and sp.state == "armed" then value = X.SPEC_VALUE end
    local path, alt = VZ.her_path(S, F)
    local i = B.target{ name = "her", npc = b, size = V.SIZE, range = 1, speed = VZ.speed(S, I), value = value,
        path = path, t_ok0 = ok0, t_ok1 = ok1 }
    -- HER OTHER STEP (C batch 5's path_alt: the chase scored as the WORSE of
    -- the two): a step TOWARD ME. vp_v2j: of 76 touches, 66 followed a chase
    -- press, and in most of them she stepped toward the seat - it stayed in
    -- reach, stood, and its tornado touched. (B.target does not forward
    -- path_alt: set on the spec it built - a GAP in tob2_plan.lua.)
    if alt then B.spec.targets[i].path_alt = alt end
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
    -- her other possible step: one tile toward me on each axis I am off her
    -- footprint (under her she steps at random: none modelled), then held
    local alt = nil
    do
        local ax, az = x, z
        if F.me.x < x then ax = x - 1 elseif F.me.x > x + V.SIZE - 1 then ax = x + 1 end
        if F.me.z < z then az = z - 1 elseif F.me.z > z + V.SIZE - 1 then az = z + 1 end
        if ax ~= x or az ~= z then
            alt = { { x = x, z = z } }
            for _ = 1, X.H do alt[#alt + 1] = { x = ax, z = az } end
        end
    end
    for k = 1, 1 do
        if tank and QD.raid._tob_gap(tank.x, tank.z, x, z, V.SIZE) >= 2 then
            if tank.x < x then x = x - 1 elseif tank.x > x + V.SIZE - 1 then x = x + 1 end
            if tank.z < z then z = z - 1 elseif tank.z > z + V.SIZE - 1 then z = z + 1 end
        end
        path[#path + 1] = { x = x, z = z }
    end
    -- (the rest held, as before; the alternative only when it differs)
    for _ = 2, X.H do path[#path + 1] = { x = x, z = z } end
    if alt and alt[2].x == path[2].x and alt[2].z == path[2].z then alt = nil end
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
    local role, anchor, nxt = QD.raid._vzp3_ball_role(S, F)
    if role == nil then return nil end
    local now, land = F.tick, S.ball.land
    if role == "target" or role == "next" then
        local read = VZ.ball_read(S, F, role)
        local hi = (role == "target") and 0 or 1
        -- A FAILED PASS lands its 75% of my hp level (74) or an explosion
        -- (relay_v2a_48: 49s and 71-74s, the burst deaths): its max in
        -- hp_worst once the landing is AUTO_SOON+1 away, so the rescue eats
        -- for it with an auto landing beside it (priced 10% expected)
        if land >= now + 1 and land <= now + X.AUTO_SOON + 1 then
            local A, base = QD.VZP3.ARENA, S.base
            B.hit{ name = "ball-fail", x0 = base.x + A.x0, z0 = base.z + A.z0, x1 = base.x + A.x1, z1 = base.z + A.z1,
                t0 = land - 1, t1 = land - 1, dmg_exp = 7, dmg_max = math.floor((F.hp_base or 99) * 3 / 4) }
        end
        B.zone{ name = "ball-" .. role, x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = hi, require = true,
            t0 = read, t1 = read, tier = "lethal" }
        B.zone{ name = "ball-margin", x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = hi, require = true,
            t0 = read - 1, t1 = read + 1, tier = "damage", cost = X.BALL_MARGIN }
        if read - 2 >= now + 1 then
            B.pull{ name = "ball-pull", x = anchor.x, z = anchor.z, size = 1, weight = X.BALL_PULL, t0 = now + 1, t1 = read - 2 }
        end
        return role, read
    end
    -- clear: 3+ from the target for the whole flight (v1 rl76)
    B.zone{ name = "ball-clear", x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = 2, t0 = now + 1, t1 = land + 1,
        tier = "lethal" }
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
function VZ.station(S, F, B)
    local X = QD.VZP3_V2
    local b = F.boss
    local ring = QD.raid._vzp3_ring(b.x, b.z)
    local seat = QD.raid._tob_seat(F) or S.role
    local p = ring[((seat - 1) * 6 + 3) % #ring + 1]
    B.pull{ name = "station", x = p.x, z = p.z, size = 1, weight = X.STATION_PULL, t0 = F.tick + 1, t1 = F.tick + X.H }
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
    want.boost, want.boost_stat = (spare > 0) and "piety" or nil, "attack"
    -- (v1: Piety OFF once the spare no longer funds the overheads to the end)
    if spare <= 0 and F.lit.piety then I.pray_off = { "piety" } end
    want.eat_below, want.brew_below = X.EAT_FLOOR, X.BREW_FLOOR
    local mode
    if F.boss == nil then
        -- the change of form: nothing attacks, top up (v1 HP_TOP)
        want.eat_below, want.brew_below = X.TOP_UP, X.TOP_UP
        mode = "wait"
    else
        if VZ.special(S, F, I) == nil and F.weapon_held then I.gear = { S.ids.weapon } end
        if P.spec and P.spec.state == "idle" and F.weapon_held then I.gear = { S.ids.weapon } end
        local role, read = VZ.ball(S, F, B)
        local hold = (role == "target" or role == "next") and read or nil
        local tgt = VZ.her_target(S, F, B, I, hold)
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
        elseif VZ.exempt(S, F) then
            mode = "dps"
            VZ.station(S, F, B)
        elseif VZ.tank_known(S) then
            mode = "tank"
        else
            mode = "away"
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
        S.vz = { modes = {}, mode = nil, taken = 0, last_hp = nil, auto_at = nil, spec = nil,
                 main_ticks = opts.weapon_ticks or QD.VZP3.WEAPON_TICKS[opts.weapon or ""] or QD.VZP3.DEFAULT_TICKS }
        -- the spec weapon (opts.spec_weapon; else the kit's warhammer)
        local name = opts.spec_weapon
        if name == nil then
            local r, id = api_drive.symbol("obj", "dragon_warhammer")
            if r == "ok" and QD.raid._tob_count(S, id) > 0 then name = "dragon_warhammer" end
        end
        if name then
            local def = QD.VZP3_V2.SPEC_WEAPONS[name]
            assert(def, "verzik_p3_v2_solve: no special weapon named " .. tostring(name))
            local r, id = api_drive.symbol("obj", name)
            assert(r == "ok", "verzik_p3_v2_solve: no obj " .. name)
            S.vz.spec = { id = id, cost = def.cost, name = name, state = "idle", fired = 0 }
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
        local modes = {}
        for k, n in pairs(P.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        local seen = {}
        for _, k in ipairs({ "crabs", "webs", "yellows", "ball" }) do seen[#seen + 1] = k .. " " .. tostring(S.seen[k] or 0) end
        return string.format("verzik_p3 v2: modes %s; taken %d; specials %s; autos %d (melee %d); tornado spawns %d; spec %s fired %d",
            table.concat(modes, " "), P.taken, table.concat(seen, ", "), S.autos, S.melees, S.tornado_spawns or 0,
            P.spec and P.spec.name or "-", P.spec and P.spec.fired or 0)
    end,
}

function QD.raid.verzik_p3_v2_solve(opts)
    assert(QD.raid.tob_mode == "hard", "verzik_p3_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("verzik_p3", opts or {})
end
