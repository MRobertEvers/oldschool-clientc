-- raid_agent / verzik: the Normal Verzik trio, as a policy (raid seam55).
--
-- step(world, me, mem, seats, mems) -> intent | "quit" | nil, once per bot
-- per tick.  The setup is the _vzslow kit (test/raids/_vzslow.lua) spelled as
-- cheats; the fight is Measure (world.lua) -> Decide (here) -> Act (act.lua).

local World = require("world")
local Move = require("raid_move")
local V = {}

local VERZIK = {
    verzik_initial = true, verzik_phase1 = true, verzik_phase1_to2_transition = true,
    verzik_phase2 = true, verzik_phase2_to3_transition = true, verzik_phase3 = true,
}
local ATTACKABLE = { verzik_phase1 = true, verzik_phase2 = true, verzik_phase3 = true }

-- THE ROOM.  Everything is relative to Verzik's spawn: the instance moves the
-- room, not its shape.  Verzik P1 sits with her south-west tile at (30, 35);
-- the pillars' south-west tiles are x 25 (west) and 37 (east) at z 18, 24, 30
-- (NR SupportingPillar, tob_verzik.rs2 [proc,tob_verzik_behind_pillar]); the
-- floor is x 22..41, z 15..34 (the old plan's P.floor).
local P1_WINDUP, P1_SHOT_AFTER, P1_CADENCE = 8109, 3, 14
local PILLAR = { verzik_pillar_npc = true }
local COLLAPSING = { verzik_collapsing_pillar_npc = true }

local function room(world, m)
    if m.O ~= nil then return m.O end
    for _, n in pairs(world.npcs) do
        if n.name == "verzik_initial" or n.name == "verzik_phase1" then
            m.O = { x = n.x - 30, z = n.z - 35 }
            return m.O
        end
    end
    return nil
end

local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end

-- Is (x, z) on the floor and outside every live npc's footprint?  The floor is
-- the server's collision once asked for (Verzik's pools land as far out as
-- x 44, past the old plan's P.floor), the old plan's box until then.
local function walkable(world, O, x, z)
    local rx, rz = x - O.x, z - O.z
    if world.blocked ~= nil then
        local b = world.blocked[x * 100000 + z]
        if b == nil or b then return false end
    elseif rx < 22 or rx > 41 or rz < 15 or rz > 34 then
        return false
    end
    for _, n in pairs(world.npcs) do
        if n.alive and n.x ~= nil and (PILLAR[n.name] or COLLAPSING[n.name] or VERZIK[n.name]) then
            local s = n.size or 1
            if x >= n.x and x < n.x + s and z >= n.z and z < n.z + s then return false end
        end
    end
    return true
end

-- Content's cover for one pillar (its south-west tile), west or east column.
local function behind(px, pz, west, x, z)
    local dx, dz = x - px, z - pz
    if west then
        return (dx >= -3 and dx <= 0 and dz >= -3 and dz <= 0) or (dx == 1 and dz == -1) or (dx == -1 and dz == 1)
    end
    return (dx >= 2 and dx <= 4 and dz >= -2 and dz <= 0) or (dx == 1 and dz == -1) or (dx == 3 and dz == 1)
end

-- THE PARTY'S PILLAR.  A pillar takes a hit for every shot it covers, however
-- many hide behind it, and falls after about four (40-60 a hit, tob.constant
-- ^tob_verzik_pillar_hit_*), so the party hides behind ONE pillar at a time
-- (vzb4 t41: seat 1 behind one and seats 2-3 behind another cost both a hit a
-- shot and dropped two at once).  It must be the same pillar on every
-- raider's machine, so it is chosen from what every raider sees alike -- the
-- shots aimed at each pillar -- never from a raider's own tile: the first in
-- PILLAR_ORDER (north first, nearest her) with fewer than PILLAR_SPENT hits,
-- else the least hit.
local PILLAR_ORDER = { { 25, 30 }, { 37, 30 }, { 25, 24 }, { 37, 24 }, { 25, 18 }, { 37, 18 } }
local PILLAR_SPENT = 3

local function p1_count_pillar_hits(world, O, m)
    m.pillar_hits = m.pillar_hits or {}
    for _, p in ipairs(world.projectiles) do
        if p.launch == world.tick and p.target == 0 and p.spotanim == 1580 then
            -- aimed at a pillar's centre: its south-west tile is one less
            local key = (p.dx - 1 - O.x) .. "," .. (p.dz - 1 - O.z)
            m.pillar_hits[key] = (m.pillar_hits[key] or 0) + 1
        end
    end
end

local function p1_party_pillar(world, O, m)
    local live = {}
    for _, n in pairs(world.npcs) do
        if n.alive and PILLAR[n.name] then live[(n.x - O.x) .. "," .. (n.z - O.z)] = n end
    end
    local best, best_hits = nil, nil
    for _, rel in ipairs(PILLAR_ORDER) do
        local key = rel[1] .. "," .. rel[2]
        local n = live[key]
        if n ~= nil then
            local hits = m.pillar_hits[key] or 0
            if hits < PILLAR_SPENT then return n end
            if best == nil or hits < best_hits then best, best_hits = n, hits end
        end
    end
    return best
end

-- My tile behind the party's pillar: walkable, nearest me, never within 3 of a
-- collapsing pillar's edge (the fall reaches 3 from the edge), and never
-- behind a second pillar (that would cost it a hit too).
local function p1_cover(world, O, me, m)
    local best = nil
    local chosen = p1_party_pillar(world, O, m)
    for _, n in pairs(world.npcs) do
        if n == chosen then
            local west = (n.x - O.x) == 25
            for x = n.x - 3, n.x + 4 do
                for z = n.z - 3, n.z + 1 do
                    if behind(n.x, n.z, west, x, z) and walkable(world, O, x, z) then
                        local safe = true
                        for _, c in pairs(world.npcs) do
                            if c.alive and COLLAPSING[c.name] then
                                local s = c.size or 3
                                local ex = math.max(c.x - x, 0, x - (c.x + s - 1))
                                local ez = math.max(c.z - z, 0, z - (c.z + s - 1))
                                if math.max(ex, ez) <= 3 then safe = false end
                            end
                        end
                        for _, o in pairs(world.npcs) do
                            if o ~= n and o.alive and PILLAR[o.name] and behind(o.x, o.z, (o.x - O.x) == 25, x, z) then
                                safe = false
                            end
                        end
                        if safe then
                            local score = cheb(me.x, me.z, x, z)
                            if best == nil or score < best.score then
                                best = { x = x, z = z, score = score, pillar = n.slot }
                            end
                        end
                    end
                end
            end
        end
    end
    return best
end

-- THE DAWNBRINGER (P1).  Its special ignores the cap (75-150, tob.constant
-- ^tob_verzik_dawn_*) where every other hit on P1 is capped at 10, and every
-- raider brings their own energy, so it goes round: the holder specs from
-- cover, and when it can no longer spec while a teammate can, it drops it on
-- its cover tile (owner: "drop the Dawnbringer at the column safe spot") and
-- the next seat with the energy takes it.  A hit of 75+ on Verzik is the spec
-- landing: nothing else gets past the cap.
local DAWN, MAIN, SPEC_COST = "verzik_special_weapon", "scythe_of_vitur", 350

local function dawn_obj(world)
    for id, name in pairs(world.names.obj) do if name == DAWN then return id end end
end


-- The Dawnbringer's tick for the holder: returns true when it owns the tick.
local function p1_dawn(world, me, m, mems, seats, vz, intent, covered)
    local t = world.tick
    m.dawn_id = m.dawn_id or dawn_obj(world)
    local inv_slot, it = World.inv_slot(me, DAWN)
    local wielded = me.weapon == m.dawn_id
    -- not the holder: take it off the ground if I am next
    if inv_slot == nil and not wielded then
        local g = world:on_ground(DAWN)
        if g ~= nil and (me.spec or 0) >= SPEC_COST then
            for _, pid in ipairs(seats) do
                local o = world.players[pid]
                if mems[pid].died == nil and (o.spec or 0) >= SPEC_COST and pid ~= me.pid
                    and mems[pid].seat < m.seat and o.weapon ~= m.dawn_id then
                    return false
                end
            end
            intent.take = { x = g.x, z = g.z, obj = g.obj }
            intent.why = intent.why .. "take dawn "
            return true
        end
        return false
    end
    m.dawn = m.dawn or "READY"
    local main_slot, main = World.inv_slot(me, MAIN)
    if m.dawn == "SPECCING" then
        if (vz.big_hit_tick or -1) > m.dawn_at or t > m.dawn_at + 8 then
            m.dawn = "AFTER"
        else
            intent.why = intent.why .. "speccing "
            return true
        end
    end
    if m.dawn == "AFTER" then
        if wielded and main_slot ~= nil then intent.op = { { 2, main.obj, main_slot } } end
        m.dawn = "READY"
        intent.why = intent.why .. "rearm "
        return false
    end
    if (me.spec or 0) >= SPEC_COST then
        if not covered then return false end
        intent.op = intent.op or {}
        if not wielded then intent.op[#intent.op + 1] = { 2, it.obj, inv_slot } end
        intent.spec = true
        intent.attack = vz.slot
        m.dawn, m.dawn_at = "SPECCING", t
        intent.why = intent.why .. "dawn spec "
        return true
    end
    -- spent: pass it to a teammate who can still spec
    local other = false
    for _, pid in ipairs(seats) do
        local o = world.players[pid]
        if pid ~= me.pid and mems[pid].died == nil and (o.spec or 0) >= SPEC_COST then other = true end
    end
    if other and covered then
        if wielded then
            if main_slot ~= nil then intent.op = { { 2, main.obj, main_slot } } end
        elseif inv_slot ~= nil then
            intent.op = { { 5, it.obj, inv_slot } }
            intent.why = intent.why .. "drop dawn "
        end
        return true
    elseif wielded and main_slot ~= nil then
        intent.op = { { 2, main.obj, main_slot } }
    end
    return false
end

-- THE P1 ENDGAME.  Every pillar still standing falls when P1 dies, 32-65 to
-- whoever is within 3 of its edge (vzb4 t196: two falls killed all three on
-- their cover tiles).  Her server hitpoints are one pool over the phases, P1
-- the top 1500 of a trio's (tob.constant ^tob_verzik_p1_hp_3), so once what is
-- left of P1 is a spec's worth the last blows come from the three melee tiles
-- more than 3 from every pillar: x 31..33 on z 34.  The transition stages the
-- party on the old plan's lanes, (28, 22) and (35, 22): clear of the falls and
-- two out from where P2 lands (her 5x5 at x 30..34, z 25..29).
local P1_HP, P1_ENDGAME = 1500, 160
local SAFE_MELEE = { { 32, 34 }, { 31, 34 }, { 33, 34 } }
local T12_STAGE = { { 28, 22 }, { 35, 22 }, { 28, 21 } }

local function p1_left(vz)
    if vz.hp == nil or vz.hpmax == nil then return nil end
    return vz.hp - (vz.hpmax - P1_HP)
end

-- P2 (Normal trio, halberds).  Her 5x5 attacks every 4 ticks; anyone UNDER
-- her draws the stomp and anyone NEXT to her the body slam (tob_verzik.rs2
-- [proc,tob_verzik_body_slam]), so every raider holds a home two out -- the
-- halberd's reach -- one west, one east, one south (the old plan's
-- QD.raid._verzik_p2_home; W:904).  Urn bombs land on a tile 2-3 ticks after
-- the cast: never stand on one when it lands.  The adds: the Athanatos heals
-- her until a poisoned hit removes it (owner ruling), so the seat nearest it
-- at its spawn wears the serpentine helm and hits it; the Matomenos (reds) are
-- killed on sight; a dying nylocas blasts everything within 3 (W:925), and
-- that is the only time to run from one (owner: "only run away when they are
-- in the pop zone").  No swing on her for 5 ticks after a summon: it heals her
-- (Entry_Mode.wikitext:231).
local P2_SUMMON, P2_ABSORB, P2_SUMMON_EVERY = 8117, 5, 44
local BOMB = 1583
local ATHANATOS = { tob_verzik_phase2_armourednylocas = true }
local REDS = { tob_verzik_phase2_bloodnylocas = true }
local NYLO = { verzik_nylocas_melee = true, verzik_nylocas_ranged = true, verzik_nylocas_magic = true }
local HALBERD, SERP = "noxious_halberd", "serpentine_helm_charged"
-- THE SCYTHE STEP-OUT.  The halberd's one hit a swing (13.7 a hit) left P2 at
-- 430-850 ticks and the long fights died of empty bags.  Her P2 attacks come
-- every 4 ticks exactly (12 after a summon) and the slam reads who stood next
-- to her at the end of the tick before, so a scythe can be in for three ticks
-- of every four: step in and swing after her attack, step out on the tick
-- before the next.  RAID_AGENT_P2_WEAPON=noxious_halberd keeps the old way.
local P2_WEAPON = os.getenv("RAID_AGENT_P2_WEAPON") or "scythe_of_vitur"
local P2_REACH = (P2_WEAPON == HALBERD) and 2 or 1
local P2_ATTACKS = { [8114] = true, [8116] = true }

local function foot_dist(n, x, z)
    local s = n.size or 1
    local dx = math.max(n.x - x, 0, x - (n.x + s - 1))
    local dz = math.max(n.z - z, 0, z - (n.z + s - 1))
    return math.max(dx, dz)
end

local function p2_home(vz, seat)
    local n, mid = vz.size or 5, 2
    if seat == 2 then return vz.x + n + 1, vz.z + mid end
    if seat == 3 then return vz.x + mid, vz.z - 2 end
    return vz.x - 2, vz.z + mid
end

local function p2(world, me, m, mems, seats, vz, O, intent)
    local t = world.tick
    -- gear for the phase: the halberd in hand, the serpentine helm on
    intent.op = intent.op or {}
    local hs, h = World.inv_slot(me, P2_WEAPON)
    if hs ~= nil and (m.claws_at or -9) + 3 < t then intent.op[#intent.op + 1] = { 2, h.obj, hs } end
    local ss, s = World.inv_slot(me, SERP)
    if ss ~= nil then intent.op[#intent.op + 1] = { 2, s.obj, ss } end
    for _, ev in ipairs(world.events) do
        if ev.kind == "npc_anim" and ev.npc.slot == vz.slot and ev.anim == P2_SUMMON then
            m.summon = t
            m.p2_next = t + 12
        end
        if ev.kind == "npc_anim" and ev.npc.slot == vz.slot and P2_ATTACKS[ev.anim] then m.p2_next = t + 4 end
        if ev.kind == "npc_spawn" and ATHANATOS[ev.npc.name] then
            -- the poisoner: the living seat nearest it now, ties to the lower seat
            local best, bd = nil, nil
            for _, pid in ipairs(seats) do
                local o = world.players[pid]
                if mems[pid].died == nil and o.x ~= nil then
                    local d = cheb(o.x, o.z, ev.npc.x, ev.npc.z)
                    if bd == nil or d < bd or (d == bd and mems[pid].seat < mems[best].seat) then best, bd = pid, d end
                end
            end
            m.poisoner = best
        end
    end
    -- the target
    local target = nil
    local ath = world:find(ATHANATOS, me.x, me.z)[1]
    if ath ~= nil and m.poisoner == me.pid then target = ath end
    if target == nil then target = world:find(REDS, me.x, me.z)[1] end
    -- the summon's absorb: a swing that lands in it heals her, and the swing
    -- the server repeats on its own is already out when the anim is seen
    -- (vz04 t384 summon, absorbed hits t386); the summons come every 44, so
    -- the next is predicted and her own swings stop the tick before
    local next_summon = m.summon and (m.summon + P2_SUMMON_EVERY) or nil
    local absorbing = (m.summon ~= nil and t - m.summon <= P2_ABSORB)
        or (next_summon ~= nil and next_summon - t <= 1)
    if target == nil and not absorbing then target = vz end
    -- the claws on a red: their spec's two hits on the add that heals her
    if target ~= nil and REDS[target.name] and (me.spec or 0) >= 500 then
        local cs, c = World.inv_slot(me, "dragon_claws")
        if cs ~= nil then
            intent.op[#intent.op + 1] = { 2, c.obj, cs }
            intent.spec = true
            m.claws_at = t
            intent.why = intent.why .. "claws "
        elseif me.weapon ~= nil and world.names.obj[me.weapon] == "dragon_claws" then
            intent.spec = true
            m.claws_at = t
        end
    elseif me.weapon ~= nil and world.names.obj[me.weapon] == "dragon_claws" and (m.claws_at or -9) + 3 < t then
        local hs2, h2 = World.inv_slot(me, P2_WEAPON)
        if hs2 ~= nil then intent.op[#intent.op + 1] = { 2, h2.obj, hs2 } end
    end
    -- the tile: hazards hard, home soft
    local hx, hz = p2_home(vz, m.seat)
    local bombs = {}
    for _, pr in ipairs(world.projectiles) do
        if pr.spotanim == BOMB and pr.land >= t then bombs[#bombs + 1] = pr end
    end
    local dying = {}
    for _, n in pairs(world.npcs) do
        if n.alive and n.dying and NYLO[n.name] then dying[#dying + 1] = n end
    end
    local q = {
        me = me, step = 2, stay_w = 2,
        ok = function(x, z) return walkable(world, O, x, z) end,
        hard = {
            -- under her is never right; beside her only when her next attack
            -- is not the tick after my step lands
            { name = "under", pen = 1000, bad = function(x, z) return foot_dist(vz, x, z) < 1 end },
            { name = "slam", pen = 600, bad = function(x, z)
                if P2_REACH >= 2 then return foot_dist(vz, x, z) <= 1 end
                return foot_dist(vz, x, z) <= 1 and (m.p2_next == nil or m.p2_next <= t + 2)
            end },
            { name = "bomb", pen = 500, bad = function(x, z)
                for _, b in ipairs(bombs) do if b.dx == x and b.dz == z and b.land <= t + 3 then return true end end
                return false
            end },
            { name = "pop", pen = 700, bad = function(x, z)
                for _, n in ipairs(dying) do if foot_dist(n, x, z) <= 3 then return true end end
                return false
            end },
        },
        soft = {},
    }
    -- the target is hit only from a tile already in the halberd's reach: an
    -- attack click leaves the path to the server, and the server's path to a
    -- red beside her runs under her (vzb4 t629: seat 2 stomped for 53 chasing
    -- a red); so the walk is mine, to a tile the hazards allow
    if target == vz or target == nil then
        q.soft[#q.soft + 1] = { name = "home", w = 6, cost = function(x, z) return cheb(x, z, hx, hz) end }
    end
    local reach = P2_REACH
    if target ~= nil and target ~= vz then reach = (P2_REACH >= 2) and 2 or 1 end
    local stepping_out = target == vz and P2_REACH < 2 and (m.p2_next == nil or m.p2_next <= t + 2)
    if target ~= nil then
        -- stepping out: wait two out, ready to step back in
        local want = stepping_out and 2 or reach
        q.soft[#q.soft + 1] = { name = "reach", w = 10, cost = function(x, z)
            local d = foot_dist(target, x, z)
            if stepping_out then return math.abs(d - 2) end
            return math.max(0, d - want)
        end }
    end
    local r = Move.solve(q)
    local here, here_broke = Move.cost_at(q, me.x, me.z)
    local fdt = target and foot_dist(target, me.x, me.z) or 99
    local in_reach = target ~= nil and fdt >= 1 and fdt <= reach
    -- a scythe swing on her starts from two out: the click walks the one tile
    if target == vz and P2_REACH < 2 and not stepping_out and fdt == 2 then in_reach = true end
    if stepping_out then
        -- no click on her now: the swing the server repeats would walk me in
        if me.target == vz.slot or foot_dist(vz, me.x, me.z) <= 1 then
            local r2 = Move.solve(q)
            intent.walk = { x = r2.x, z = r2.z }
            intent.why = intent.why .. "p2 out " .. (r2.x - O.x) .. "," .. (r2.z - O.z) .. " "
            return intent
        end
        in_reach = false
    end
    if r.moved and (here_broke ~= nil or here > r.cost + 4 or (target ~= nil and not in_reach)) then
        intent.walk = { x = r.x, z = r.z }
        intent.why = intent.why .. "p2 move " .. (r.x - O.x) .. "," .. (r.z - O.z) .. (here_broke and ("!" .. here_broke) or "") .. " "
        return intent
    end
    if target ~= nil and not in_reach then
        return intent
    end
    if target ~= nil and (me.target ~= target.slot) then
        intent.attack = target.slot
        intent.why = intent.why .. "p2 hit " .. target.name .. " "
    elseif (target == nil or intent.spec) and me.target ~= nil and me.target >= 0 and not (target and me.target == target.slot) then
        intent.walk = { x = me.x, z = me.z }
        intent.why = intent.why .. "p2 hold (absorb) "
    end
    return intent
end

-- CONSUMABLES.  A potion is "br_<doses>dose<name>"; the fewest doses first, so
-- a slot empties before another is opened.  Prayer is restored under 25 (vzb4
-- t205: "You have run out of Prayer points" -- no Protect from Magic, no Piety
-- for the rest of P2), the super combat is drunk as each fighting phase opens,
-- food first and the brew once the food is gone.
local function potion(me, name)
    for doses = 1, 4 do
        local slot, it = World.inv_slot(me, "br_" .. doses .. "dose" .. name)
        if slot ~= nil then return slot, it end
    end
    return nil
end

local function consume(world, me, m, vz, intent, eat_below)
    local t = world.tick
    intent.op = intent.op or {}
    if (m.drank or -9) + 2 > t then return end
    if me.prayer ~= nil and me.prayer < 25 then
        local slot, it = potion(me, "2restore")
        if slot ~= nil then
            intent.op[#intent.op + 1] = { 1, it.obj, slot }
            m.drank = t
            intent.why = intent.why .. "restore "
            return
        end
    end
    if vz ~= nil and m.boosted_for ~= vz.name and (vz.name == "verzik_phase1" or vz.name == "verzik_phase2" or vz.name == "verzik_phase3") then
        local slot, it = potion(me, "2combat")
        if slot ~= nil then
            intent.op[#intent.op + 1] = { 1, it.obj, slot }
            m.drank, m.boosted_for = t, vz.name
            intent.why = intent.why .. "combat "
            return
        end
    end
    if me.hp ~= nil and me.hp < eat_below and (m.ate or -9) + 3 <= t then
        local slot, it = World.inv_slot(me, "anglerfish")
        if slot == nil then slot, it = potion(me, "potionofsaradomin") end
        if slot ~= nil then
            intent.op[#intent.op + 1] = { 1, it.obj, slot }
            m.ate = t
            intent.why = intent.why .. "eat "
        end
    end
end

-- P3 (Normal trio, halberds).  Her melee is only ever thrown at a raider in
-- melee range and cannot be prayed (tob_verzik.rs2 tob_verzik_p3_regular), so
-- everyone hits from two out and she has nobody to melee.  Her ranged (1593)
-- and magic (1594) autos are halved by the matching overhead read when they
-- LAND ([queue,tob_verzik_p3_auto_land]): the overhead follows what is flying
-- at me.  The tile: a tornado's reach (raid_move.tornado_reaches: its touch
-- heals her three times the damage -- vzb4 636 healed), a web or a landing web
-- or bomb, a dying nylocas's 3, her melee range -- hard; reach and my side
-- soft.  The green ball (owner rulings): orb order -- the holder stands, the
-- next in seat order joins its 3x3 for the landing, everyone else keeps out of
-- it; once passed, a raider is free.  The yellows: one pool a seat, by order.
local P3_RANGED_PROJ, P3_MAGIC_PROJ, BALL_PROJ, WEB_PROJ, POOL_GFX = 1593, 1594, 1598, 1601, 1595
local TORNADO = { tob_verzik_creeper = true }
local WEBS = { verzik_web_npc = true }

-- Walking distance from (sx, sz) over the floor, round her body: a straight
-- line through a 7x7 is not a path (vz01 t733-741: the joiner went the long
-- way round her and landed seven tiles from the holder).
local function bfs(world, O, sx, sz, limit)
    local d = { [sx * 100000 + sz] = 0 }
    local q, head = { { sx, sz } }, 1
    while head <= #q do
        local x, z = q[head][1], q[head][2]
        head = head + 1
        local k = d[x * 100000 + z]
        if k < limit then
            for dx = -1, 1 do
                for dz = -1, 1 do
                    local nx, nz = x + dx, z + dz
                    local key = nx * 100000 + nz
                    if d[key] == nil and walkable(world, O, nx, nz) then
                        d[key] = k + 1
                        q[#q + 1] = { nx, nz }
                    end
                end
            end
        end
    end
    return function(x, z) return d[x * 100000 + z] or (limit + cheb(x, z, sx, sz)) end
end

local function p3_ball(world, me, m, seats, mems)
    local t = world.tick
    local proj = nil
    for _, pr in ipairs(world.projectiles) do
        if pr.spotanim == BALL_PROJ and pr.land >= t then proj = pr end
    end
    local b = m.ball
    if proj == nil then
        if b ~= nil and t > (b.last or t) + 6 then m.ball = nil end
        return m.ball
    end
    local key = proj.launch .. ":" .. tostring(proj.target)
    if b == nil then
        b = { visited = {}, key = key }
        m.ball = b
    elseif b.key ~= key then
        if b.holder ~= nil then b.visited[b.holder] = true end
        b.key = key
    end
    b.holder, b.land, b.last = proj.target_pid, proj.land, t
    -- the next in seat order after the holder that has not had it
    local hseat = b.holder and mems[b.holder] and mems[b.holder].seat
    b.next = nil
    if hseat ~= nil then
        for k = 1, #seats - 1 do
            local s = ((hseat - 1 + k) % #seats) + 1
            for _, pid in ipairs(seats) do
                if mems[pid].seat == s and not b.visited[pid] and mems[pid].died == nil and b.next == nil then b.next = pid end
            end
        end
    end
    return b
end

-- THE TANK.  She picks one raider at random and keeps them for the phase,
-- re-picking only after ten seconds out of melee distance ([proc,
-- tob_verzik_pick_tank]); she walks at whoever that is.  So the raiders who are
-- not her tank kiting her at two out walks the party into a corner (vzb4
-- t834-856: all three stacked on (22, 15), and the ball exploded on the
-- stack).  Her autos are aimed at the tank, so the target of the last one is
-- the tank, read alike by everyone; the tank holds her in melee range and
-- drags her toward the middle of the floor, the rest hold two out around her.
local P3_CENTRE = { 31, 25 }

local function p3_tank(world, m)
    for _, pr in ipairs(world.projectiles) do
        if pr.launch == world.tick and (pr.spotanim == P3_RANGED_PROJ or pr.spotanim == P3_MAGIC_PROJ) and pr.target_pid ~= nil then
            m.tank = pr.target_pid
        end
    end
    return m.tank
end

local function p3(world, me, m, mems, seats, vz, O, intent)
    local t = world.tick
    local tank = p3_tank(world, m)
    local i_tank = tank == me.pid
    local cx, cz = O.x + P3_CENTRE[1], O.z + P3_CENTRE[2]
    -- the overhead: what is flying at me, else Protect from Magic
    local want = "protectfrommagic"
    local soonest = nil
    for _, pr in ipairs(world.projectiles) do
        if pr.target_pid == me.pid and pr.land >= t + 1 and (pr.spotanim == P3_RANGED_PROJ or pr.spotanim == P3_MAGIC_PROJ) then
            if soonest == nil or pr.land < soonest.land then soonest = pr end
        end
    end
    if soonest ~= nil and soonest.spotanim == P3_RANGED_PROJ then want = "protectfrommissiles" end
    if m.overhead ~= want and (m.overhead_at or -9) < t then
        intent.pray = intent.pray or {}
        intent.pray[#intent.pray + 1] = want
        m.overhead, m.overhead_at = want, t
    end
    -- the hazards
    local tors = {}
    for _, n in pairs(world.npcs) do
        if n.alive and TORNADO[n.name] then tors[#tors + 1] = { x = n.x, z = n.z } end
    end
    local landing = {}
    for _, pr in ipairs(world.projectiles) do
        if (pr.spotanim == WEB_PROJ or pr.spotanim == BOMB) and pr.land >= t then landing[pr.dx * 100000 + pr.dz] = true end
    end
    for _, n in pairs(world.npcs) do
        if n.alive and WEBS[n.name] and n.x ~= nil then landing[n.x * 100000 + n.z] = true end
    end
    local dying, crabs = {}, {}
    for _, n in pairs(world.npcs) do
        if n.alive and n.dying and NYLO[n.name] then dying[#dying + 1] = n end
        if n.alive and NYLO[n.name] and n.x ~= nil then crabs[#crabs + 1] = n end
    end
    local hard = {
        { name = "under", pen = 1000, bad = function(x, z) return foot_dist(vz, x, z) < 1 end },
        { name = "melee", pen = i_tank and 0 or 150, bad = function(x, z) return foot_dist(vz, x, z) <= 1 end },
        { name = "tor", pen = 450, bad = function(x, z) return Move.tornado_reaches(tors, me, me, x, z) end },
        { name = "web", pen = 300, bad = function(x, z)
            if landing[x * 100000 + z] then return true end
            if cheb(me.x, me.z, x, z) < 2 then return false end
            local mx, mz = Move.toward(me.x, me.z, x, z, 1)
            return landing[mx * 100000 + mz] == true
        end },
        { name = "pop", pen = 700, bad = function(x, z)
            for _, n in ipairs(dying) do if foot_dist(n, x, z) <= 3 then return true end end
            return false
        end },
        -- a P3 crab walks at its raider and goes off ON ARRIVAL, no death row
        -- first (tob.constant ^tob_verzik_p2_nylo_*: 63 within 1 of its 2x2,
        -- 26 at 2, 8 at 3; vy08 t684-688: two arrived, two raiders dead)
        { name = "crab", pen = 500, bad = function(x, z)
            for _, n in ipairs(crabs) do if foot_dist(n, x, z) <= 2 then return true end end
            return false
        end },
    }
    local soft = {
        { name = "reach", w = 10, cost = function(x, z) return math.max(0, foot_dist(vz, x, z) - (i_tank and 1 or 2)) end },
        { name = "crab3", w = 15, cost = function(x, z)
            local c = 0
            for _, n in ipairs(crabs) do if foot_dist(n, x, z) <= 3 then c = c + 1 end end
            return c
        end },
        -- away from the walls: a corner has no tile left to step to
        { name = "middle", w = 2, cost = function(x, z) return math.max(0, cheb(x, z, cx, cz) - 6) end },
    }
    -- a tornado walks one tile a tick and I run two: stepping just off its
    -- reach lets it catch up at once (vzb4 t1300-1306: every tick a move, no
    -- swing for 200 ticks at 67 left), so the step goes FAR from it, and the
    -- gap that buys is the swing
    if #tors > 0 then
        soft[#soft + 1] = { name = "gap", w = 8, cost = function(x, z)
            local near = 99
            for _, e in ipairs(tors) do near = math.min(near, cheb(e.x, e.z, x, z)) end
            return math.max(0, 5 - near)
        end }
    end
    if i_tank then
        -- drag her toward the middle: the side of her nearer the centre
        soft[#soft + 1] = { name = "drag", w = 4, cost = function(x, z) return cheb(x, z, cx, cz) end }
    end
    local stay_w = 2
    -- never stacked: a stack is a crowd for the ball and the blood alike
    soft[#soft + 1] = { name = "spread", w = 6, cost = function(x, z)
        local n = 0
        for _, pid in ipairs(seats) do
            local o = world.players[pid]
            if pid ~= me.pid and mems[pid].died == nil and o.x ~= nil and cheb(o.x, o.z, x, z) <= 1 then n = n + 1 end
        end
        return n
    end }
    -- the ball
    local b = p3_ball(world, me, m, seats, mems)
    if b ~= nil and b.holder ~= nil then
        local H = world.players[b.holder]
        local left = (b.land or t) - t
        -- content (tob_verzik.rs2 [queue,tob_verzik_ball_land]): at each
        -- landing exactly ONE raider who has not had it may stand in the 3x3
        -- round the target -- it hops to them and hurts nobody; two such
        -- explode on everyone near, none hits the target (and a previous
        -- holder beside it).  The third landing with three alive dissipates.
        -- So the holder stands (the meeting point: one mover), the next in
        -- seat order joins, and everyone else stays out of it.
        if b.holder == me.pid then
            stay_w = 30
            -- forced off by a tornado, the holder steps TOWARD the joiner:
            -- the bot runner's raiders read one world, so the pair meet
            local N = b.next and world.players[b.next]
            if N ~= nil and N.x ~= nil then
                local nx, nz = N.x, N.z
                local np = mems[b.next] and mems[b.next].planned
                if np ~= nil and np.t >= t - 1 then nx, nz = np.x, np.z end
                local toN = bfs(world, O, nx, nz, 30)
                soft[#soft + 1] = { name = "meet", w = 12, cost = function(x, z) return toN(x, z) end }
            end
        elseif b.next == me.pid and H ~= nil then
            -- where the holder is GOING, not where it stands: in the enrage it
            -- dodges every tick (vy00 t627-635: two to four apart all flight).
            -- The policy decides every bot in one process, so the holder's
            -- plan of this tick (or the last, if I am decided first) is known
            local hx2, hz2 = H.x, H.z
            local hp = mems[b.holder] and mems[b.holder].planned
            if hp ~= nil and hp.t >= t - 1 then hx2, hz2 = hp.x, hp.z end
            local toH = bfs(world, O, hx2, hz2, 30)
            soft[#soft + 1] = { name = "join", w = 40, cost = function(x, z) return math.max(0, toH(x, z) - 1) end }
            if left <= 1 then
                hard[#hard + 1] = { name = "pair", pen = 1000, bad = function(x, z) return cheb(x, z, H.x, H.z) > 1 end }
            end
        elseif H ~= nil then
            hard[#hard + 1] = { name = "crowd", pen = (left <= 2) and 1000 or 300, bad = function(x, z) return cheb(x, z, H.x, H.z) <= 2 end }
            -- a flat penalty has no way out from the holder's own tile (vz08
            -- t936-944: a run of two cannot leave a radius of two, so every
            -- tile cost the same and the third stood on the holder): the
            -- gradient walks it out over the ticks the flight gives
            soft[#soft + 1] = { name = "out", w = 25, cost = function(x, z) return math.max(0, 3 - cheb(x, z, H.x, H.z)) end }
        end
    end
    -- the yellows: seat k stands on the k-th pool
    -- one row per pool per player who saw it: by tile, once each (vz11 t783:
    -- nine rows for three pools sorted all three seats onto one)
    local pools, seen_pool = {}, {}
    for _, s in ipairs(world.spotanims) do
        local k = s.x * 100000 + s.z
        if s.spotanim == POOL_GFX and t - s.tick <= 14 and not seen_pool[k] then
            pools[#pools + 1] = s
            seen_pool[k] = true
        end
    end
    if #pools > 0 then
        table.sort(pools, function(p1, p2) return p1.x < p2.x or (p1.x == p2.x and p1.z < p2.z) end)
        -- the set's assignment, once, when it lands: every ordering of the
        -- living raiders over the pools, the least worst walk, then the least
        -- total (by seat order the far pool could be across her body: vx02
        -- t766 and vx15 t643 blasted unshared on the walk)
        local first = pools[1].tick
        for _, pl in ipairs(pools) do first = math.min(first, pl.tick) end
        if m.pool_set ~= first then
            m.pool_set = first
            local live = {}
            for _, pid in ipairs(seats) do
                if mems[pid].died == nil and world.players[pid].x ~= nil then live[#live + 1] = pid end
            end
            local dist = {}
            for i, pl in ipairs(pools) do
                local f = bfs(world, O, pl.x, pl.z, 40)
                dist[i] = {}
                for _, pid in ipairs(live) do dist[i][pid] = f(world.players[pid].x, world.players[pid].z) end
            end
            local best, best_worst, best_sum = nil, nil, nil
            local function permute(k, used, chosen)
                if k > #live then
                    local worst, sum = 0, 0
                    for j, pid in ipairs(live) do
                        local dd = dist[chosen[j]][pid]
                        worst, sum = math.max(worst, dd), sum + dd
                    end
                    if best == nil or worst < best_worst or (worst == best_worst and sum < best_sum) then
                        best, best_worst, best_sum = {}, worst, sum
                        for j, pid in ipairs(live) do best[pid] = chosen[j] end
                    end
                    return
                end
                for i = 1, #pools do
                    if not used[i] then
                        used[i] = true
                        chosen[k] = i
                        permute(k + 1, used, chosen)
                        used[i] = false
                    end
                end
            end
            if #live <= #pools then permute(1, {}, {}) end
            m.pool_of = best or {}
        end
        local idx = (m.pool_of or {})[me.pid] or (((m.seat - 1) % #pools) + 1)
        local mine = pools[idx]
        local toPool = bfs(world, O, mine.x, mine.z, 40)
        soft[#soft + 1] = { name = "pool", w = 60, cost = function(x, z) return toPool(x, z) end }
        if first + 14 - t <= 2 then
            hard[#hard + 1] = { name = "unpooled", pen = 900, bad = function(x, z) return x ~= mine.x or z ~= mine.z end }
        end
        stay_w = 1
    end
    -- in a flight the pair's meeting outranks fleeing a tornado for a gap and
    -- standing in reach (vz01 t733-741: the holder ran from its tornado, the
    -- joiner came round her, two apart at the landing); the tornado's touch
    -- itself stays hard, and the landing's pair rule outranks it
    if b ~= nil and b.holder ~= nil and (b.holder == me.pid or b.next == me.pid) then
        local kept = {}
        for _, s in ipairs(soft) do
            if s.name ~= "gap" then
                if s.name == "reach" then s.w = 2 end
                if s.name == "meet" then s.w = 30 end
                kept[#kept + 1] = s
            end
        end
        soft = kept
    end
    local q = { me = me, step = 2, hard = hard, soft = soft, stay_w = stay_w,
        ok = function(x, z) return walkable(world, O, x, z) end }
    local r = Move.solve(q)
    local here, here_broke = Move.cost_at(q, me.x, me.z)
    local fd = foot_dist(vz, me.x, me.z)
    local in_reach = fd == 2 or (i_tank and fd == 1)
    m.planned = { x = me.x, z = me.z, t = t }
    if r.moved and (here_broke ~= nil or here > r.cost + 4 or not in_reach) then m.planned = { x = r.x, z = r.z, t = t } end
    if r.moved and (here_broke ~= nil or here > r.cost + 4 or not in_reach) then
        intent.walk = { x = r.x, z = r.z }
        intent.why = intent.why .. "p3 move " .. (r.x - O.x) .. "," .. (r.z - O.z) .. (here_broke and ("!" .. here_broke) or "") .. " "
        return intent
    end
    if in_reach and #pools == 0 and me.target ~= vz.slot then
        intent.attack = vz.slot
        intent.why = intent.why .. "p3 hit "
    end
    return intent
end

local function kit(seat)
    local k = { "clearinv", "tobkit" }
    local worn = {
        [2] = { "oathplate_helm", "oathplate_chest", "oathplate_legs" },
        [3] = { "neitiznot_faceguard", "tzhaar_cape_fire", "bandos_chestplate", "bandos_skirt" },
    }
    for _, item in ipairs(worn[seat] or {}) do
        k[#k + 1] = "give " .. item .. " 1"
        k[#k + 1] = "wield " .. item
    end
    for _, c in ipairs({
        "setlevel slayer 37", "give slayer_boots 1", "wield slayer_boots", "clearinv",
        "setlevel attack 99", "setlevel strength 99", "setlevel prayer 99", "setlevel magic 99",
        "setlevel agility 99", "give serpentine_helm_charged 1",
        "give br_4dosepotionofsaradomin 4", "give br_4dose2restore 4", "give br_4dose2combat 2",
        "give dragon_claws 1",
    }) do k[#k + 1] = c end
    if seat == 1 then k[#k + 1] = "give verzik_special_weapon 1" end
    k[#k + 1] = "give noxious_halberd 1"
    k[#k + 1] = "give anglerfish " .. ((seat == 1) and 16 or 14)
    return k
end

-- The run's report line, once, to stderr.
local function report(world, mems, verdict)
    local p = {}
    for pid, m in pairs(mems) do
        local pl = world.players[pid]
        p[#p + 1] = "s" .. m.seat .. " hp" .. tostring(pl and pl.hp) .. (m.died and (" died t" .. m.died) or "")
    end
    table.sort(p)
    io.stderr:write("verzik: ", verdict, " at t", world.tick, " | ", table.concat(p, " | "), "\n")
end

-- In a client run (the leader's embedded drive) the three clients log in on
-- different ticks: setup waits until the whole party is in the world.
local PARTY = tonumber(os.getenv("RAID_AGENT_PARTY") or "0")

function V.step(world, me, m, seats, mems)
    local t = world.tick
    if PARTY > 0 and #seats < PARTY then return nil end
    m.t0 = m.t0 or t
    local age = t - m.t0
    -- SETUP: kit on tick 1, the leader enters on 3, members join on 5, the leader starts on 7
    if age == 1 then return { cheat = kit(m.seat), why = "kit" } end
    if age == 3 and m.seat == 1 then return { cheat = { "tobmode 6 1" }, why = "enter" } end
    if age == 5 and m.seat > 1 then return { cheat = { "tobjoinroom 1" }, why = "join" } end
    if age == 7 and m.seat == 1 then return { cheat = { "tobgo" }, why = "start" } end
    if age < 9 then return nil end

    -- the run ends when every raider is dead, or Verzik is gone after being seen
    if me.died_tick ~= nil and not m.died then m.died = me.died_tick end
    local vz = world:find(VERZIK, me.x, me.z)[1]
    -- the run's end is what content says, not what a raider sees: a caged
    -- raider's client stops tracking her (vz05: seat 1 caged at t203 read
    -- "gone" and ended a fight still going)
    for _, msg in ipairs(world.messages) do
        if msg.text:find("Verzik Vitur has fallen", 1, true) then m.fallen = true end
        if msg.text:find("Your party has failed", 1, true) then m.failed = true end
    end
    if m.fallen then report(world, mems, "VERZIK GONE") return "quit" end
    if m.failed then report(world, mems, "WIPE") return "quit" end
    if m.seat == 1 and age > 2400 then report(world, mems, "TIMEOUT") return "quit" end
    if m.died ~= nil then return nil end

    local intent = { why = "" }
    -- a modal blocks every normal queue (the P1 bolt's landing is one): close
    -- it, as a player dismisses the kit's message box
    if (me.mainmodal or 0) > 0 or (me.chatmodal or 0) > 0 then
        intent.close = true
        intent.why = intent.why .. "close "
    end
    -- prayers once: protect from magic and piety (a press toggles)
    if not m.prayed then
        intent.pray = { "protectfrommagic", "piety" }
        m.prayed = true
    end
    -- eat under half
    -- from P2 on a single hit reaches 82 (the stomp): eat under 65
    local eat_below = (me.hpmax or 99) / 2
    if vz ~= nil and (vz.name == "verzik_phase2" or vz.name == "verzik_phase3") then eat_below = 65 end
    if vz ~= nil and vz.name == "verzik_phase1" then
        local left = p1_left(vz)
        if left ~= nil and left <= P1_ENDGAME then eat_below = 80 end
    end
    consume(world, me, m, vz, intent, eat_below)
    if vz == nil then return intent end
    local O = room(world, m)
    if O ~= nil and m.seat == 1 and world.blocked ~= nil and not m.dumped and os.getenv("RAID_AGENT_DUMP_FLOOR") then
        m.dumped = true
        for rz = 40, 10, -1 do
            local row = {}
            for rx = 15, 50 do
                local b = world.blocked[(O.x + rx) * 100000 + (O.z + rz)]
                row[#row + 1] = (b == nil) and "?" or (b and "#" or ".")
            end
            io.stderr:write(string.format("floor %2d ", rz), table.concat(row), "\n")
        end
    end
    -- the floor changes with the phase: the pillars are collision in P1 and
    -- gone after it, and P3's pools land where they stood (vz05 t855: a pool
    -- on the east pillar's tile, unreachable on P1's grid -- 71 unshared)
    if O ~= nil and m.seat == 1 and m.asked_floor ~= vz.name then
        intent.query = { x0 = O.x + 10, z0 = O.z + 5, w = 45, h = 40 }
        m.asked_floor = vz.name
    end

    -- P1: hide behind a pillar for every shot, attack between them
    if vz.name == "verzik_phase1" and O ~= nil then
        for _, ev in ipairs(world.events) do
            if ev.kind == "npc_anim" and ev.npc.slot == vz.slot and ev.anim == P1_WINDUP then m.windup = t end
        end
        -- the next shot: a seen windup's, else the cadence's guess from the last
        local shot = nil
        if m.windup ~= nil then
            shot = m.windup + P1_SHOT_AFTER
            while shot < t do shot = shot + P1_CADENCE end
        end
        p1_count_pillar_hits(world, O, m)
        for _, h in ipairs(world.npc_hits or {}) do
            if h.slot == vz.slot and h.damage >= 75 then vz.big_hit_tick = t end
        end
        local cover = p1_cover(world, O, me, m)
        local covered = cover ~= nil and cover.x == me.x and cover.z == me.z
        -- the shot reads my tile at the end of shot - 1; a walk sent now lands
        -- next tick, two tiles a tick
        local need = cover and math.ceil(cheb(me.x, me.z, cover.x, cover.z) / 2) or 0
        local hide = shot == nil or (shot - 1 - t) <= need + 1
        -- the endgame does not hide: the walk to cover and back eats the whole
        -- window (vzb4 t175: nine tiles each way), and a prayed bolt is at most
        -- 68 (137 halved), so what is left of P1 is meleed through one or two,
        -- eating above it
        local left = p1_left(vz)
        local endgame = left ~= nil and left <= P1_ENDGAME
        if endgame then hide = false end
        local safe = SAFE_MELEE[m.seat]
        local at_safe = me.x == O.x + safe[1] and me.z == O.z + safe[2]
        -- the Dawnbringer is cast from cover: it owns the tick while it acts.
        -- In the endgame it is cast only from my safe melee tile.
        local spec_ok = (covered or not hide)
        if endgame then spec_ok = at_safe end
        if p1_dawn(world, me, m, mems, seats, vz, intent, spec_ok) then return intent end
        if hide and cover ~= nil then
            if not covered or me.target ~= nil and me.target >= 0 then intent.walk = { x = cover.x, z = cover.z } end
            intent.why = intent.why .. "hide->" .. (cover.x - O.x) .. "," .. (cover.z - O.z) .. " shot" .. tostring(shot)
            return intent
        end
        if endgame and not at_safe then
            intent.walk = { x = O.x + safe[1], z = O.z + safe[2] }
            intent.why = intent.why .. "endgame->safe "
            return intent
        end
    end

    -- P1 -> P2: off every pillar and out of where she lands
    if vz.name == "verzik_phase1_to2_transition" and O ~= nil then
        local s = T12_STAGE[m.seat]
        if me.x ~= O.x + s[1] or me.z ~= O.z + s[2] then intent.walk = { x = O.x + s[1], z = O.z + s[2] } end
        intent.why = intent.why .. "stage"
        return intent
    end

    if vz.name == "verzik_phase2" and O ~= nil then
        return p2(world, me, m, mems, seats, vz, O, intent)
    end

    if vz.name == "verzik_phase3" and O ~= nil then
        return p3(world, me, m, mems, seats, vz, O, intent)
    end

    -- attack whatever form is attackable
    if ATTACKABLE[vz.name] and (me.target ~= vz.slot or intent.spec) then
        intent.attack = vz.slot
        intent.why = intent.why .. "attack " .. vz.name
    end
    return intent
end

return V
