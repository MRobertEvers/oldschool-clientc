-- raid_agent / verzik: the Normal Verzik trio, as a policy (raid seam55).
--
-- step(world, me, mem, seats, mems) -> intent | "quit" | nil, once per bot
-- per tick.  The setup is the _vzslow kit (test/raids/_vzslow.lua) spelled as
-- cheats; the fight is Measure (world.lua) -> Decide (here) -> Act (act.lua).

local World = require("world")
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

-- Is (x, z) on the floor and outside every live npc's footprint?
local function walkable(world, O, x, z)
    local rx, rz = x - O.x, z - O.z
    if rx < 22 or rx > 41 or rz < 15 or rz > 34 then return false end
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
    if seat >= 2 then k[#k + 1] = "give noxious_halberd 1" end
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

function V.step(world, me, m, seats, mems)
    local t = world.tick
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
    if vz ~= nil then m.saw_verzik = true end
    if m.seat == 1 then
        local alive = 0
        for _, pid in ipairs(seats) do if mems[pid].died == nil then alive = alive + 1 end end
        if alive == 0 then report(world, mems, "WIPE") return "quit" end
        if m.saw_verzik and vz == nil then report(world, mems, "VERZIK GONE") return "quit" end
        if age > 2400 then report(world, mems, "TIMEOUT") return "quit" end
    end
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
    local eat_below = (me.hpmax or 99) / 2
    if vz ~= nil and vz.name == "verzik_phase1" then
        local left = p1_left(vz)
        if left ~= nil and left <= P1_ENDGAME then eat_below = 80 end
    end
    if me.hp ~= nil and me.hpmax ~= nil and me.hp < eat_below and (m.ate or -9) + 3 <= t then
        local slot, it = World.inv_slot(me, "anglerfish")
        if slot ~= nil then
            intent.op = { { 1, it.obj, slot } }
            m.ate = t
            intent.why = intent.why .. "eat "
        end
    end
    if vz == nil then return intent end
    local O = room(world, m)

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

    -- attack whatever form is attackable
    if ATTACKABLE[vz.name] and (me.target ~= vz.slot or intent.spec) then
        intent.attack = vz.slot
        intent.why = intent.why .. "attack " .. vz.name
    end
    return intent
end

return V
