-- quest-driver / cox2_shamans: THE LIZARDMAN SHAMANS, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/shamans_solver.md is the plan; rooms/shamans_spec.md
-- the mechanics (rows S-*); docs/CONTENT_SOLVER_MANUAL.md the method. A QD.T2
-- room (tob2_run.lua): PREDICT and STRATEGY are here; the planner
-- (api_drive.aplan), the executive, the monitor and the supplies are shared.
--
-- THE IDEA (Synq 0:34:38-0:41:59; wiki CoX/Strategies): Protect from
-- Missiles all room, range one shaman at a time, and BE MOVING on every tick a
-- shaman's swing is due -- its acid is aimed at where I stood at the end of
-- the tick before and lands 2 + floor(d / 2) ticks later, a 5x5. A jump
-- (anim 7152) slams a 3x3 round where I stood, six ticks on; spawns burst
-- 3x3 round themselves (all RECORDED, docs/rsprox-analysis/cox/shamans.md). The spirit tendrils at the door and the exit
-- spawns are the room's own: Pass the tendrils, wait for the blockers to burst.
--
-- Perception: npc rows (tiles, animations and when each was FIRST seen),
-- projectiles (the acid's destination), locs (the tendrils), varp 102 (the
-- poison orb). Nothing the server keeps to itself.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}
QD.COXSH = QD.COXSH or {}
local CS = QD.COXSH

CS.V = {
    H = 10, BEAM = 32,
    -- the shamans' clock (4; 9 after a jump, RECORDED)
    SWING = 4, SWING_JUMP = 9,
    -- a shaman is "on me" this many ticks after its last swing
    ENGAGED = 12,
    -- the acid (S-ACID): 5x5, 20-39, read at the end of T+L-1
    ACID_R = 2, ACID_EXP = 30, ACID_MAX = 39,
    -- a due swing, before it is seen: 2/7 of the acid's 30 plus its poison,
    -- priced on my tile at the end of the tick before (Synq: "move on the
    -- tick it attacks"); weighted up because the method is to always move
    PRE_ACID = 20, ACID_EDGE = 6,
    -- keep an engaged shaman this far off (d <= CLOSE costs CLOSE_COST a tick):
    -- from gap 2 its acid takes 3+ ticks, dodged once its swing shows
    CLOSE = 2, CLOSE_COST = 10,
    -- within this gap the acid lands next tick but one (+2): be moving on its
    -- due swing, before the swing shows
    PRE_MOVE = 1,
    -- ticks after the gate with no target
    ENTRY = 6,
    -- ticks with no shot in the plan before walking closer for sight
    BLIND = 8,
    -- an idle shaman's hunt range (NR aggressionDistance 8) + 1, and its price
    HUNT = 9, AGGRO_COST = 2,
    -- the jump (S-JUMP-L): 3x3 round my tile at the end of T-1, landing T+6,
    -- read at the end of T+5
    JUMP_READ = 5, JUMP_EXP = 22, JUMP_MAX = 25,
    -- the spawns (S-SPAWN-X): fuses 8, 10, 12 from their first sight, the
    -- 3x3, 5-10; the hands-to-head 3 ticks before the burst
    SPAWN_R = 1, SPAWN_FUSE_MIN = 8, SPAWN_WARN = 3, SPAWN_EXP = 8, SPAWN_MAX = 10,
    -- my crossbow on rapid (the kit's): range 7, 5 ticks
    RANGE = 7, SPEED = 5, SWING_VALUE = 4,
    -- supplies (Xeric's aid is the healing)
    EAT_BELOW = 50, BREW_BELOW = 60,
    -- prayer: rich enough for a steady overhead; Rigour only from here up
    PRAYER_RICH = 40, RIGOUR_AT = 60,
}

-- ======================================================================== IDS

function CS.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_shamans", opts)
    local room = QD.raid._tob_symbols("cox_shamans", {
        { "sh_a", "npc", "raids_lizardshaman_a" }, { "sh_b", "npc", "raids_lizardshaman_b" },
        { "blocker", "npc", "raids_lizardshaman_blocker" }, { "spawn", "npc", "zeah_lizardshaman_spawn" },
        { "tendril", "loc", "raids_shaman_tendrils_entrance" },
        { "acid", "spotanim", "lizardshaman_spit_acid" },
        { "seq_ranged", "seq", "shay_lizard_warrior_attack_ranged" },
        { "seq_melee", "seq", "shayzien_lizard_boss_tail_swipe" },
        { "seq_jump", "seq", "shayzien_lizard_boss_jump" },
        { "seq_land", "seq", "shayzien_lizard_boss_land" },
        { "seq_summon", "seq", "shayzien_lizard_boss_minion_summon" },
        { "seq_warn", "seq", "shayzien_lizard_boss_hands_to_head" },
        { "seq_death", "seq", "shay_lizard_warrior_death" },
        { "xbow", "obj", "xbows_crossbow_dragon" }, { "fury", "obj", "enchanted_onyx_amulet" },
        { "assembler", "obj", QD.COX.ava_name() }, { "dhide_body", "obj", "black_dragonhide_body" },
        { "chaps", "obj", "black_dragonhide_chaps" }, { "defender", "obj", "dragon_parryingdagger" },
        { "poison", "varp", "varp102_poison" },
        { "style_slot_1", "component", "combat_interface:style_slot_1" },
        { "com_mode", "varp", "varp43_com_mode" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_shamans: no combat tab")
    ids.combat_tab = combat_tab
    ids.sets = { ranged = { ids.xbow, ids.fury, ids.assembler, ids.dhide_body, ids.chaps, ids.defender } }
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_shamans: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    ids.brews = QD.COX.supply_ids("heal")
    ids.restores = QD.COX.supply_ids("restore")
    ids.stat_restores = ids.restores
    ids.combats, ids.rangings = {}, {}
    -- any anti-poison carried, the first held drunk at the first poison:
    -- sanfew serum (the wiki's pick), antidote++, the raid's own if any
    ids.antidotes = {}
    for _, stem in ipairs({ "sanfew_salve_%d_dose", "br_sanfew_salve_%d_dose", "antidote++%d", "%ddoseantipoison" }) do
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", string.format(stem, n))
            if r == "ok" then ids.antidotes[#ids.antidotes + 1] = id end
        end
    end
    ids.staminas = {}
    for n = 1, 4 do
        local r, id = api_drive.symbol("obj", n .. "dosestamina")
        assert(r == "ok", "cox_shamans: no obj " .. n .. "dosestamina")
        ids.staminas[n] = id
    end
    return ids
end

-- ================================================================== OBSERVE

function CS.cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- Chebyshev distance from (x, z) to an npc's footprint (0 under it).
function CS.range(row, x, z)
    local size = row.size or 1
    local dx, dz = 0, 0
    if x < row.x then dx = row.x - x elseif x > row.x + size - 1 then dx = x - (row.x + size - 1) end
    if z < row.z then dz = row.z - z elseif z > row.z + size - 1 then dz = z - (row.z + size - 1) end
    return math.max(dx, dz)
end

-- The acid's flight (S-ACID-T): lands L ticks after the swing.
function CS.acid_l(d)
    return 2 + d // 2
end

-- One tick's look: the shamans, their swings first seen, the acid in the
-- air, the spawns, the tendrils and the blockers.
function CS.observe(S, F)
    local s, ids, V = S.sh, S.ids, CS.V
    if s.room == nil then s.room = CS.find_room() end
    s.shamans, s.spawns, s.blockers, s.tendrils = {}, {}, {}, {}
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if id == ids.sh_a or id == ids.sh_b then
            local key = QD.T2.npc_key(row)
            local m = s.mem[key]
            if m == nil then
                m = { key = key, first = F.tick }
                s.mem[key] = m
            end
            m.row = row
            -- a new animation: (seq, its stamp) changed; keyed on FIRST SEEN
            local sig = tostring(row.seq_id) .. ":" .. tostring(row.seq_tick)
            if row.seq_id ~= nil and row.seq_id >= 0 and sig ~= m.sig then
                m.sig = sig
                local q = row.seq_id
                -- (the summon is a swing too: the clock runs through it)
                if q == ids.seq_ranged or q == ids.seq_melee or q == ids.seq_jump or q == ids.seq_summon then
                    m.swing, m.swing_kind = F.tick, q
                    s.swings = s.swings + 1
                    if q == ids.seq_jump then
                        local at = QD.T2.tile_at(S, F.tick - 1) or F.me
                        m.jump = { t = F.tick, x = at.x, z = at.z }
                        s.jumps = s.jumps + 1
                    elseif q == ids.seq_summon then
                        s.summons = s.summons + 1
                    end
                end
            end
            if (row.seq_id == ids.seq_death or row.health_ratio == 0) and not m.dead then
                m.dead = true
                s.deaths = s.deaths + 1
            end
            if not m.dead then s.shamans[#s.shamans + 1] = m end
            s.seen_shaman = true
        elseif id == ids.spawn then
            local key = QD.T2.npc_key(row)
            local sp = s.spawn_mem[key]
            -- a key back after a gap, or past its own burst, is a new spawn:
            -- a summon every ~30 ticks reuses the dead ones' slots
            if sp ~= nil and (F.tick - (sp.seen or F.tick) > 2 or (sp.warn ~= nil and F.tick > sp.warn + V.SPAWN_WARN)) then
                sp = nil
            end
            if sp == nil then
                sp = { first = F.tick }
                s.spawn_mem[key] = sp
            end
            sp.seen = F.tick
            if row.seq_id == ids.seq_warn and sp.warn == nil then sp.warn = F.tick end
            sp.x, sp.z = row.x, row.z
            s.spawns[#s.spawns + 1] = sp
        elseif id == ids.blocker then
            s.blockers[#s.blockers + 1] = row
            s.seen_blocker = true
        end
    end
    -- the acid: a spit at a tile, from a shaman's swing; the landing from
    -- the swing it left with (first seen with the projectile)
    local pr, projs = api_drive.projectiles(0)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == ids.acid and (p.target or -1) <= 0 and (p.cycles_left or 0) > 0 then
                -- (not `launched`: it flips 61 cycles in, which made every spit two)
                local key = p.dst_x .. ":" .. p.dst_z .. ":" .. tostring(p.src_x) .. ":" .. tostring(p.src_z)
                if s.acid_seen[key] == nil then
                    -- L from the CASTER's body to the target tile (S-ACID-T):
                    -- the shaman whose middle the spit left from (the
                    -- projectile's source); the nearest body if none matches
                    local caster, best = nil, nil
                    for _, m in ipairs(s.shamans) do
                        local cx, cz = m.row.x + 1, m.row.z + 1
                        local dd = CS.cheb(cx, cz, p.src_x or cx, p.src_z or cz)
                        if best == nil or dd < best then caster, best = m, dd end
                    end
                    local d = caster and CS.range(caster.row, p.dst_x, p.dst_z) or 4
                    local L = CS.acid_l(d)
                    -- the flight as sent: the content times its arc to end
                    -- on the landing tick, 30 cycles a tick (cox_minions.rs2
                    -- ~cox_shaman_attack_poison); the caster's distance when
                    -- a row carries none
                    if (p.duration or 0) >= 30 then L = p.duration // 30 end
                    -- from the caster's swing when it was seen (the spit
                    -- leaves 61 cycles in, so it can be seen a tick or two
                    -- after the swing it belongs to)
                    local t0 = F.tick
                    if caster and caster.swing and F.tick - caster.swing <= 3 then t0 = caster.swing end
                    s.acid_seen[key] = { x = p.dst_x, z = p.dst_z, land = t0 + L }
                    s.acids = s.acids + 1
                    QD.raid._tob_trace(S, F.tick, string.format("acid at %d,%d seen, duration %s left %s, caster swing %s, lands t%d",
                        p.dst_x, p.dst_z, tostring(p.duration), tostring(p.cycles_left), tostring(caster and caster.swing), t0 + L))
                end
            end
        end
    end
    for key, a in pairs(s.acid_seen) do
        if a.land < F.tick - 2 then s.acid_seen[key] = nil end
    end
    local lr, rows = api_drive.locs(24)
    if lr == "ok" then
        for _, row in ipairs(rows) do
            if row.loc_id == ids.tendril then s.tendrils[#s.tendrils + 1] = { x = row.x, z = row.z } end
        end
    end
    local vr, poison = api_drive.varp(ids.poison)
    s.poison = (vr == "ok" and poison) or 0
end

-- ================================================================== PREDICT

function CS.predict(S, F, B)
    local s, V = S.sh, CS.V
    CS.observe(S, F)
    CS.pass_check(S, F)
    local now, h = F.tick, B.h
    -- the target first: the idle-aggro term below spares it
    if #s.shamans > 0 then CS.pick(S, F) end
    -- THE ACID SEEN: its 5x5 at the read (forbid)
    -- (the read tick and the one after it: first sight can lag the cast)
    for _, a in pairs(s.acid_seen) do
        local t0, t1 = math.max(now + 1, a.land - 1), a.land
        if t1 >= now + 1 then
            B.hit({ name = "acid", x0 = a.x - V.ACID_R, z0 = a.z - V.ACID_R, x1 = a.x + V.ACID_R, z1 = a.z + V.ACID_R,
                    t0 = t0, t1 = t1, dmg_exp = V.ACID_EXP, dmg_max = V.ACID_MAX, forbid = true })
            -- (a tile of margin: a planned 2-tile step that comes out as 1
            -- left the dodge a tile short)
            B.hit({ name = "acid-edge", x0 = a.x - V.ACID_R - 1, z0 = a.z - V.ACID_R - 1, x1 = a.x + V.ACID_R + 1,
                    z1 = a.z + V.ACID_R + 1, t0 = t0, t1 = t1, dmg_exp = V.ACID_EDGE, dmg_max = 0 })
        end
    end
    for _, m in ipairs(s.shamans) do
        local row = m.row
        -- THE JUMP SEEN: the 3x3 it lands on (forbid)
        if m.jump and m.jump.t + V.JUMP_READ >= now + 1 then
            local t = m.jump.t + V.JUMP_READ
            B.hit({ name = "jump", x0 = m.jump.x - 1, z0 = m.jump.z - 1, x1 = m.jump.x + 1, z1 = m.jump.z + 1,
                    t0 = t, t1 = t, dmg_exp = V.JUMP_EXP, dmg_max = V.JUMP_MAX, forbid = true })
        end
        -- TOO CLOSE: an acid aimed from d <= 1 lands in 2 ticks, 2-3 in 3
        -- (S-ACID-T): in reach, too soon to step 3 tiles once seen; further
        -- out it is dodged after its swing shows. So a shaman on me is kept
        -- 3 off -- priced (2 in 7 of its swings are acid), and its melee reach
        -- forbidden on a due swing. An idle one is kept out of its hunt
        -- range (8, S-AGGRO): one at a time.
        local sz = row.size or 3
        local engaged = (m.swing and now - m.swing <= V.ENGAGED)
            or CS.range(row, F.me.x, F.me.z) <= V.HUNT - 1
        if engaged then
            B.hit({ name = "close", x0 = row.x - V.CLOSE, z0 = row.z - V.CLOSE, x1 = row.x + sz - 1 + V.CLOSE,
                    z1 = row.z + sz - 1 + V.CLOSE, t0 = now + 1, t1 = now + h, dmg_exp = V.CLOSE_COST, dmg_max = 0 })
            local gap = (m.swing_kind == S.ids.seq_jump) and V.SWING_JUMP or V.SWING
            local D = m.swing and (m.swing + gap) or (now + h + 1)
            -- (its swing not seen yet: it may come any tick -- its reach is
            -- forbidden throughout; from there an acid lands next tick)
            if m.swing == nil or now - m.swing > V.ENGAGED then
                B.hit({ name = "reach?", x0 = row.x - 1, z0 = row.z - 1, x1 = row.x + sz, z1 = row.z + sz,
                        t0 = now + 1, t1 = now + h, dmg_exp = 12, dmg_max = 25, forbid = true })
            end
            -- in reach the acid comes in 2 ticks: be moving on its swing
            -- (Synq's method), the mark on my tile at the end of D-1
            local near = CS.range(row, F.me.x, F.me.z) <= V.PRE_MOVE
            while D <= now + h do
                if near and D - 1 >= now and #B.spec.marks < 6 then
                    B.mark({ name = "pre-acid", t_mark = D - 1, r = V.ACID_R, t0 = D + 1, t1 = D + 1,
                             dmg_exp = V.PRE_ACID, dmg_max = 0 })
                end
                if D - 1 >= now + 1 then
                    B.hit({ name = "reach", x0 = row.x - 1, z0 = row.z - 1, x1 = row.x + sz, z1 = row.z + sz,
                            t0 = D - 1, t1 = D - 1, dmg_exp = 12, dmg_max = 25, forbid = true })
                end
                D = D + V.SWING
            end
        elseif S.sh.target_key ~= m.key then
            B.hit({ name = "aggro", x0 = row.x - V.HUNT, z0 = row.z - V.HUNT, x1 = row.x + sz - 1 + V.HUNT,
                    z1 = row.z + sz - 1 + V.HUNT, t0 = now + 1, t1 = now + h, dmg_exp = V.AGGRO_COST, dmg_max = 0 })
        end
    end
    -- THE EXIT SPAWNS: blockwalk all, which the plan's map does not hold
    for _, b in ipairs(s.blockers) do
        B.hit({ name = "blocker", x = b.x, z = b.z, t0 = now + 1, t1 = now + h, sample = "pass",
                dmg_exp = 0, dmg_max = 0, forbid = true })
    end
    -- THE SPAWNS: their 5x5 from the earliest fuse; to the tick once one
    -- clutches its head
    for _, sp in ipairs(s.spawns) do
        local t0, t1
        if sp.warn then
            t0 = sp.warn + V.SPAWN_WARN - 1
            t1 = t0
        else
            t0 = math.max(now + 1, sp.first + V.SPAWN_FUSE_MIN - 1)
            t1 = now + h
        end
        if t1 >= now + 1 and t0 <= t1 then
            -- (a warned spawn stands still for its last 3 ticks: its 3x3 at
            -- the read is certain, and two run steps clear it -- forbidden)
            B.hit({ name = "spawn", x0 = sp.x - V.SPAWN_R, z0 = sp.z - V.SPAWN_R, x1 = sp.x + V.SPAWN_R, z1 = sp.z + V.SPAWN_R,
                    t0 = math.max(t0, now + 1), t1 = t1, dmg_exp = V.SPAWN_EXP, dmg_max = V.SPAWN_MAX,
                    forbid = sp.warn ~= nil })
        end
    end
end

-- ================================================================= STRATEGY

function CS.gear(S)
    local out = {}
    for _, obj in ipairs(S.ids.sets.ranged) do
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = false }
        end
    end
    return out
end

-- Is (x, z) reached exactly by the client's route from where I stand?
function CS.reaches(x, z)
    local r, route = api_drive.route(x, z)
    if r ~= "ok" or type(route) ~= "table" then return false end
    return route.nearest == nil or route.nearest == false or route.nearest == 0
end

-- The target: the shaman already on me (its last swing the latest), else
-- the nearest -- one at a time (the wiki, Tubby, the CA page).
function CS.pick(S, F)
    local s = S.sh
    local best, bscore = nil, nil
    for _, m in ipairs(s.shamans) do
        local d = CS.range(m.row, F.me.x, F.me.z)
        local score = d
        if m.swing and F.tick - m.swing <= CS.V.ENGAGED then score = score - 100 end
        if s.target_key == m.key then score = score - 50 end
        if bscore == nil or score < bscore then best, bscore = m, score end
    end
    if best then s.target_key = best.key end
    return best
end

function CS.strategy(S, F, B)
    local s, ids, V = S.sh, S.ids, CS.V
    local intents = { mode = "-" }
    if s.arrival == nil then CS.arrival(S, F) end
    -- THE PRAYERS (S-RNG: the auto's prayer is read at its swing). With
    -- prayer to spare, Protect from Missiles all room; short of it (a
    -- drained arrival: raid16 came in at 27 and no restore in the pack, ran
    -- dry in 110 ticks and every auto landed), only from the tick before a
    -- due swing to the swing (half the drain), and Rigour only when rich.
    s.flick = F.prayer < V.PRAYER_RICH and CS.restore_doses(S) < 2
    if F.prayer > 0 then
        local want = (not s.flick) or CS.swing_soon(S, F)
        if want and not F.lit.protectfrommissiles then intents.pray = "protectfrommissiles" end
        if not want and F.lit.protectfrommissiles then intents.pray_off = { "protectfrommissiles" } end
    end
    if F.lit.rigour and not (F.prayer >= V.RIGOUR_AT and CS.restore_doses(S) >= 2) then
        intents.pray_off = intents.pray_off or {}
        intents.pray_off[#intents.pray_off + 1] = "rigour"
    end
    -- the heal floor: lower once no restore is held (a brew drains Ranged)
    S.t2.want.brew_below = QD.COX.brew_floor(S, V.BREW_BELOW)
    intents.gear = CS.gear(S)
    -- the poison: an anti-poison, if one is carried, once poisoned (varp
    -- 102 > 0; negative while immune)
    if s.poison > 0 and F.tick - (S.last_drink or -10) >= 3 then
        local a = QD.raid._tob_first(S, ids.antidotes)
        if a then
            intents.drink = a
            S.last_drink = F.tick
        end
    end
    -- the stamina under 30% run
    if intents.drink == nil and (F.run_energy or 0) < 3000 and F.tick - (S.last_drink or -10) >= 3
        and (s.stam_until or -1) < F.tick then
        local st = QD.raid._tob_first(S, ids.staminas)
        if st then
            intents.drink = st
            S.last_drink = F.tick
            s.stam_until = F.tick + 200
        end
    end
    -- a restore once Ranged is drained (brews), the gap by the doses held
    local rd = CS.restore_doses(S)
    QD.COX.restore_intent(S, F, intents, { "ranged" }, rd >= 8 and 8 or (rd >= 4 and 12 or 20))
    if s.deaths >= s.count then
        -- CLEARED: to the exit, where the spawns blocking it burst (S-CLEAR,
        -- clear + 8); not into them
        intents.mode = "exit"
        S.t2.want.boost = nil
        local p = s.room and QD.COX.exit_passage(s.room)
        if p then
            B.pull({ name = "exit", x = p.x + 1, z = p.z + 1, size = 5, weight = 2, t0 = F.tick + 1, t1 = F.tick + B.h })
        end
        -- (not beside the spawns that block it: they burst 1 within 2)
        for _, b in ipairs(s.blockers) do
            B.hit({ name = "burst", x0 = b.x - 2, z0 = b.z - 2, x1 = b.x + 2, z1 = b.z + 2, t0 = F.tick + 1,
                    t1 = F.tick + B.h, dmg_exp = 3, dmg_max = 1 })
        end
        return intents
    end
    if #s.shamans == 0 or not CS.inside(S, F) then
        intents.mode = "enter"
        CS.enter(S, F, B, intents)
        return intents
    end
    local m = CS.pick(S, F)
    -- THE GATE between me and it: the entrance tendrils beside me, the
    -- shaman not walkable to (S-PASS)
    local tb, td = CS.unpassed(S, F)
    if tb and td <= 3 then
        local r = m.row
        local walk = CS.reaches(r.x + 1, r.z - 1) or CS.reaches(r.x - 1, r.z + 1)
            or CS.reaches(r.x + 3, r.z + 1) or CS.reaches(r.x + 1, r.z + 3)
        if not walk then
            CS.pass(S, F, intents, tb)
            return intents
        end
    end
    -- THE ENTRY: just through the gate, the shamans waking -- clear of the
    -- door before the first shot (no target: the plan spends these ticks
    -- getting 5 off every one of them)
    if s.entered_at and F.tick - s.entered_at < V.ENTRY then
        intents.mode = "entry"
        return intents
    end
    intents.mode = "fight"
    S.t2.want.boost = (F.prayer >= V.RIGOUR_AT and CS.restore_doses(S) >= 2) and "rigour" or nil
    -- out of reach, or no shot for a while (no line of sight): walk at it
    -- (a swing beyond the horizon is worth nothing)
    local last = S.t2.last
    if last and (last.value or 0) > 0 then s.shot_ok = F.tick end
    local blind = F.tick - (s.shot_ok or F.tick) > V.BLIND
    if blind then
        local sz = m.row.size or 3
        B.pull({ name = "to-sight", x = m.row.x - 2, z = m.row.z - 2, size = sz + 4, weight = 1,
                 t0 = F.tick + 1, t1 = F.tick + B.h })
    elseif CS.range(m.row, F.me.x, F.me.z) > V.RANGE then
        local sz = m.row.size or 3
        B.pull({ name = "to-target", x = m.row.x - 5, z = m.row.z - 5, size = sz + 10, weight = 1,
                 t0 = F.tick + 1, t1 = F.tick + B.h })
    end
    B.target({ name = "shaman", npc = m.row, size = m.row.size or 3, range = V.RANGE, speed = V.SPEED,
               value = V.SWING_VALUE })
    return intents
end

-- The nearest tendril I have not passed yet (each is passed once: the hop
-- goes through, and a second press would hop back for 15% hp).
function CS.unpassed(S, F)
    local tb, td = nil, nil
    for _, tl in ipairs(S.sh.tendrils) do
        if not S.sh.passed[tl.x .. ":" .. tl.z] then
            local d = CS.cheb(tl.x, tl.z, F.me.x, F.me.z)
            if td == nil or d < td then tb, td = tl, d end
        end
    end
    return tb, td
end

-- Pass tendril tl (the server walks me beside it and hops me through).
function CS.pass(S, F, intents, tl)
    intents.world_op = { kind = "loc", id = S.ids.tendril, op = 1, x = tl.x, z = tl.z }
    intents.mode = "tendril"
    local s = S.sh
    if s.pass_at == nil or s.pass_at.x ~= tl.x or s.pass_at.z ~= tl.z then
        s.pass_at = { x = tl.x, z = tl.z, t = F.tick }
        if CS.cheb(F.me.x, F.me.z, tl.x, tl.z) <= 1 then s.pass_at.from = { x = F.me.x, z = F.me.z } end
        s.passes = s.passes + 1
    end
end

-- The hop seen: I stood beside the tendril, and now stand 2 tiles from
-- there and beside it still (the other side) -- that set is passed (a set is
-- two tiles side by side).
function CS.pass_check(S, F)
    local s = S.sh
    local p = s.pass_at
    if p == nil then return end
    local beside = CS.cheb(F.me.x, F.me.z, p.x, p.z) <= 1
    if p.from == nil then
        if beside then p.from = { x = F.me.x, z = F.me.z } end
    elseif beside and CS.cheb(F.me.x, F.me.z, p.from.x, p.from.z) >= 2 then
        for _, tl in ipairs(s.tendrils) do
            if CS.cheb(tl.x, tl.z, p.x, p.z) <= 1 then s.passed[tl.x .. ":" .. tl.z] = true end
        end
        s.passed[p.x .. ":" .. p.z] = true
        s.pass_at = nil
        s.entered_at = F.tick
        return
    end
    if F.tick - p.t > 30 then s.pass_at = nil end
end

-- Doses held of a family's ids (each listed 1..4 doses, per stem).
function CS.doses(S, list)
    local n = 0
    for i, obj in ipairs(list or {}) do n = n + QD.raid._tob_count(S, obj) * (((i - 1) % 4) + 1) end
    return n
end

function CS.restore_doses(S)
    return CS.doses(S, S.ids.stat_restores)
end

-- THE ARRIVAL: what the room starts from, so its cost can be told from it.
function CS.arrival(S, F)
    local lv = F.levels and F.levels.ranged or {}
    local s = S.sh
    local antis = 0
    for _, obj in ipairs(S.ids.antidotes) do antis = antis + QD.raid._tob_count(S, obj) end
    s.arrival = string.format("arrival t%d hp %d prayer %d ranged %s/%s heal %d restore %d antipoison %d",
        F.tick, F.hp, F.prayer, tostring(lv.level), tostring(lv.base), CS.doses(S, S.ids.brews), CS.restore_doses(S), antis)
    s.arr_hp, s.arr_heal, s.arr_restore, s.arr_tick = F.hp, CS.doses(S, S.ids.brews), CS.restore_doses(S), F.tick
    if api_drive.ticklog_mark ~= nil then api_drive.ticklog_mark("cox_shamans " .. s.arrival) end
end

-- Is a shaman's swing due within 2 ticks (or one awake with no swing seen)?
function CS.swing_soon(S, F)
    for _, m in ipairs(S.sh.shamans) do
        local d = CS.range(m.row, F.me.x, F.me.z)
        if m.swing == nil then
            if d <= CS.V.HUNT then return true end
        elseif F.tick - m.swing <= CS.V.ENGAGED then
            local gap = (m.swing_kind == S.ids.seq_jump) and CS.V.SWING_JUMP or CS.V.SWING
            -- on from two ticks before it is due (a press now is in force
            -- next tick, and the swing reads the tick before its own) until
            -- it is SEEN: a shaman that steps into range swings late (seed 3:
            -- due 290, swung 292), and rolling the due tick on dropped the
            -- prayer under it
            if m.swing + gap - F.tick <= 2 then return true end
        elseif d <= CS.V.HUNT then
            return true
        end
    end
    return false
end

-- The shamans' room: where I stand, or the cell my room's way out leads to.
function CS.find_room()
    local here = QD.COX.here()
    if here == nil then return nil end
    if here.name == "shamans" then return here end
    local _, _, cx, cz = QD.COX.next_origin(here)
    local ahead = QD.COX.room_at(cx, cz, here.level)
    if ahead and ahead.name == "shamans" then return ahead end
    return nil
end

-- Am I in the shamans' room (its 32x32)?
function CS.inside(S, F)
    local room = S.sh.room
    if room == nil then return true end
    return F.me.x >= room.ox and F.me.x < room.ox + 32 and F.me.z >= room.oz and F.me.z < room.oz + 32
end

-- Before the room: walk toward the shamans (the tendrils latch as I near).
function CS.enter(S, F, B, intents)
    local room = S.sh.room
    if room == nil then return end
    local x, z = room.ox + 16, room.oz + 16
    B.pull({ name = "in", x = x, z = z, size = 3, weight = 2, t0 = F.tick + 1, t1 = F.tick + B.h })
    local tb, td = CS.unpassed(S, F)
    if tb and td <= 6 then CS.pass(S, F, intents, tb) end
end

-- ===================================================================== ROOM

QD.T2.register("cox_shamans", {
    ids = function(opts) return CS.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_shamans_solve"
        S.t2.plan_opts = { h = opts.h or CS.V.H, beam = opts.beam or CS.V.BEAM }
        S.t2.want.eat_below = CS.V.EAT_BELOW
        S.t2.want.brew_below = CS.V.BREW_BELOW
        S.sh = { mem = {}, spawn_mem = {}, acid_seen = {}, shamans = {}, spawns = {}, blockers = {}, tendrils = {},
                 passed = {}, swings = 0, jumps = 0, summons = 0, acids = 0, passes = 0, poison = 0, deaths = 0,
                 -- solo: two (wiki CoX/Strategies table, party 1-4)
                 count = opts.count or 2 }
        -- THE ROOM: the one I stand in, else the one ahead (the whole raid
        -- calls with nothing from past the room before's Passage); a test's
        -- centre only when given
        if opts.centre then
            local r, me = api_drive.player_tile()
            S.sh.room = QD.COX.room_at(opts.centre.x, opts.centre.z, (r == "ok" and me.level) or 0)
        end
        if S.sh.room == nil or S.sh.room.name ~= "shamans" then S.sh.room = CS.find_room() end
    end,

    -- Rapid (style slot 1) on the crossbow before the fight.
    enter = function(S)
        local ids = S.ids
        for press = 1, 4 do
            local mr, mode = api_drive.varp(ids.com_mode)
            assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
            if mode == 1 then break end
            assert(press <= 3, S.who .. ": three style presses and varp43_com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.style_slot_1, 1)
            assert(pr == "ok", S.who .. ": the style press answered " .. tostring(pr) .. " " .. tostring(pd))
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": style" }, 3)
            end
        end
    end,

    predict = function(S, F, B) CS.predict(S, F, B) end,

    strategy = function(S, F, B) return CS.strategy(S, F, B) end,

    -- DONE: every shaman down and the exit spawns gone (the way out open).
    done = function(S, F)
        local s = S.sh
        if s.deaths >= s.count and s.seen_blocker and #s.blockers == 0 then return "ok" end
        return nil
    end,

    summary = function(S)
        local s = S.sh
        local cost = ""
        if s.arrival then
            local hl = CS.doses(S, S.ids.brews)
            cost = string.format("; %s; cost heal sips %d restore sips %d", s.arrival, s.arr_heal - hl, s.arr_restore - CS.restore_doses(S))
        end
        return cost .. string.format("; shamans: swings seen %d, jumps %d, summons %d, acids %d, tendril passes %d, deaths %d/%d, blockers %d (seen %s)",
            s.swings, s.jumps, s.summons, s.acids, s.passes, s.deaths, s.count, #s.blockers, tostring(s.seen_blocker))
    end,
})
