-- quest-driver / cox2_vasa: VASA NISTIRIO, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/vasa_solver.md is the plan, vasa_spec.md the
-- mechanics (rows V1..V27 cited below). A QD.T2 room (tob2_run.lua): PREDICT
-- and STRATEGY here; the planner, the executive, the monitor and the supplies
-- are the shared layers.
--
-- THE IDEA (Synq 0:55:16-1:07:56, Tubby, edb0ys). Walk in low, take the
-- special (it costs what I had at the teleport, minus 5), heal only after the
-- teleport. While he walks: blowpipe him and never be where a boulder lands
-- (a 3x3 at the tile I stood on the tick before the throw, landing 2 ticks
-- after it: two tiles of running clear it) nor under his 5x5. While he
-- siphons: the dragon sword on the crystal's open face. The 67 siphon ticks
-- between two specials are counted, so a crystal that cannot be broken in
-- what is left is not started.
--
-- Perception: npc rows (Vasa 7565/7566/7567, the crystal 7568, their seqs and
-- splats), my own animation (the special's teleport), the boulder projectile
-- 1329, the crystals' locs; the room's template (QD.COX.room_at) for his
-- routes (vasa_spec.md "Stand tiles and routes").
-- ==========================================================================

QD.COXNI = QD.COXNI or {}

QD.COXNI.V = {
    H = 12, BEAM = 64,
    SIZE = 5, CSIZE = 4,
    -- V15 the shared siphon timer; V20 the healing form on the 3rd siphon tick
    WINDOW = 67, TRANSFORM = 2,
    -- V8-V11 from the group tick G: teleport G+3 (hp read at the end of G+2),
    -- stun to G+10, explosion G+11 (protection back then), hittable G+12
    TELEPORT = 3, STUN = 10, EXPLODE = 11, HITTABLE = 12,
    -- V21 boulders: every 3 ticks while he walks, a 3x3, landing 2 after
    THROW = 3, BOULDER_EXP = 7, MARK_EXP = 12, BOULDER_MAX = 18, PRAY_FACTOR = 0.33, BOULDER_RANGE = 8,
    -- V22 stomp
    STOMP_EXP = 4, STOMP_MAX = 8, WAY_COST = 0.5,
    -- the weapons: the blowpipe on Rapid (2 ticks, 5 tiles), the dragon sword (4)
    -- (Rapid reaches 5; planned at 4: his walk is a guess at a turn)
    PIPE_RANGE = 4, PIPE_SPEED = 2, SWORD_SPEED = 4,
    SWING_VALUE = 3, CRYSTAL_VALUE = 4,
    CRYSTAL_HP = 120, CRYSTAL_DPS = 2.5, CRYSTAL_DPS_SLASH = 2.0,   -- hp a tick on the crystal (probe S2)
    -- hp floors: the supplies brew under these (no food in the kit)
    -- (35: every special dodged, every boulder and stomp dodgeable; the fire's
    -- burn is 22.5% of what I have, so low is cheap -- Synq 1:02:08 "try and
    -- stay low HP")
    -- (THE ROOM'S COST, round 4b: low. The special takes hp - 5 whatever I
    -- hold, so hp above what keeps me alive is a sip given to him -- Synq
    -- 1:02:08 "it's usually recommended to try and stay low HP. What's the
    -- point of using supplies just for it to all be deleted for 5 HP?"; the
    -- wiki: "Solo players will have to take the special attack's full
    -- effect ... they can start the room with lower health". Boulders and the
    -- stomp are dodged; a prayed boulder is 6 at most.)
    BREW = 20, BREW_STUN = 22, BREW_PRE = 18,
    -- a dodge out through the fire costs two burns of 22.5% of what I hold
    -- (rounded down: 9 at 22 hp, 0 under 5); the special hp - 5 (17 at 22).
    -- Held at the brew floor, an eaten special is a heal sip each time and a
    -- dodge under half of one -- round 4b gated the dodge at 40 hp and the
    -- prep relay's raid5 ate 36 specials at 22 hp, a sip each, its brews
    -- draining Attack and Strength to 0 with no restore left: always dodge
    DODGE_HP = 0,
    -- a restore once Attack, Strength or Ranged is this far under base
    -- (QD.COX.restore_intent)
    RESTORE_GAP = 20,
}

-- vasa_spec.md "Stand tiles and routes": template SW tiles of his 5x5, by
-- variant (0 CCW, 1 THRU, 2 CW).
QD.COXNI.SPAWN = { [0] = { 14, 13 }, [1] = { 13, 13 }, [2] = { 13, 14 } }
QD.COXNI.CRYSTAL = {
    [0] = { { 6, 5 }, { 6, 23 }, { 23, 23 }, { 23, 5 } },
    [1] = { { 5, 5 }, { 5, 22 }, { 23, 22 }, { 23, 5 } },
    [2] = { { 5, 5 }, { 5, 23 }, { 22, 23 }, { 22, 5 } },
}
QD.COXNI.LEGS = {
    [0] = { { { 13, 13 }, { 12, 14 }, { 5, 14 }, { 5, 9 } }, { { 14, 23 }, { 10, 23 } },
            { { 23, 13 }, { 23, 18 } }, { { 14, 12 }, { 13, 11 }, { 13, 3 }, { 18, 3 } } },
    [1] = { { { 12, 13 }, { 11, 14 }, { 3, 14 }, { 3, 9 } }, { { 13, 14 }, { 14, 15 }, { 14, 22 }, { 9, 22 } },
            { { 23, 13 }, { 23, 17 } }, { { 13, 4 }, { 18, 4 } } },
    [2] = { { { 4, 14 }, { 4, 9 } }, { { 13, 15 }, { 14, 16 }, { 14, 24 }, { 9, 24 } },
            { { 14, 14 }, { 15, 13 }, { 22, 13 }, { 22, 18 } }, { { 13, 4 }, { 17, 4 } } },
}

function QD.COXNI.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_vasa", opts)
    local room = QD.raid._tob_symbols("cox_vasa", {
        { "dormant", "npc", "raids_vasanistirio_dormant" }, { "walking", "npc", "raids_vasanistirio_walking" },
        { "healing", "npc", "raids_vasanistirio_healing" }, { "crystal", "npc", "raids_vasanistirio_crystal" },
        { "seq_heal_spawn", "seq", "vasa_heal_spawn" }, { "seq_healed", "seq", "vasa_healed" },
        { "seq_death", "seq", "vasa_death" }, { "seq_teleport", "seq", "teleport_rune_human" },
        { "seq_spawn", "seq", "vasa_spawn" }, { "fire", "loc", "raids_vasanistirio_fire" },
        { "boulder", "spotanim", "raids_vasanistirio_range_travel" },
        { "exit", "loc", "raids_blockage_purple_small" },
        { "pipe", "obj", "toxic_blowpipe_loaded" }, { "sword", "obj", "dragon_shortsword" },
        { "defender", "obj", "dragon_parryingdagger" }, { "torso", "obj", "barbassault_penance_fighter_torso" },
        { "fire_cape", "obj", "tzhaar_cape_fire" }, { "fury", "obj", "enchanted_onyx_amulet" },
        { "dhide_body", "obj", "black_dragonhide_body" }, { "chaps", "obj", "black_dragonhide_chaps" },
        { "assembler", "obj", "avas_assembler" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "style_slot_1", "component", "combat_interface:style_slot_1" },
        { "com_mode", "varp", "varp43_com_mode" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_vasa: no combat tab")
    ids.combat_tab = combat_tab
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_vasa: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    ids.brews = QD.COX.supply_ids("heal")
    ids.stat_restores = QD.COX.supply_ids("restore")
    ids.overloads = QD.COX.supply_ids("overload")
    -- (Prayer from a prayer-only drink: the family ends in the super restore,
    -- and prep6 raid4 drank both of its restores at the door for Prayer 31,
    -- then fought on brews with Attack and Strength at 0 for 3,000 ticks.
    -- `ids.restores` is what the panel channel sips for Prayer under 25
    -- (tob.lua _tob_supplies): the same prayer-only list -- r1 raid4 drank
    -- its five super restore sips there, none of them for a drained stat)
    ids.enhances = {}
    do
        local stat = {}
        for _, id in ipairs(ids.stat_restores) do stat[id] = true end
        for _, id in ipairs(QD.COX.supply_ids("prayer")) do
            if not stat[id] then ids.enhances[#ids.enhances + 1] = id end
        end
    end
    ids.restores = ids.enhances
    ids.combats, ids.rangings = {}, {}
    ids.sets = {
        ranged = { { obj = ids.pipe, two_hand = true }, ids.dhide_body, ids.chaps, ids.assembler, ids.fury },
        melee = { { obj = ids.sword, two_hand = false }, ids.defender, ids.torso, ids.fire_cape, ids.fury, ids.chaps },
        -- (the whole raid's kit has no stab weapon: the tentacle, slash, into
        -- the crystal's +180 -- a raid relay should bring the dragon sword)
        melee_slash = { ids.tentacle, ids.defender, ids.torso, ids.fire_cape, ids.fury, ids.chaps },
    }
    return ids
end

-- ================================================================ GEOMETRY

function QD.COXNI.sign(n)
    if n > 0 then return 1 end
    if n < 0 then return -1 end
    return 0
end

-- A template 5x5 SW -> world SW in this room.
function QD.COXNI.vasa_world(room, t)
    local x, z = QD.COX.world_sized(room, t[1], t[2], 5, 5)
    return { x = x, z = z }
end

-- Which crystal (1..4) a world 4x4 SW is.
function QD.COXNI.crystal_index(room, x, z)
    for i, c in ipairs(QD.COXNI.CRYSTAL[room.variant]) do
        local wx, wz = QD.COX.world_sized(room, c[1], c[2], 4, 4)
        if wx == x and wz == z then return i end
    end
    return nil
end

-- His route as world SW tiles: out to crystal i, or back from it (the legs
-- reversed, then his spawn); and where it starts (his spawn / the stand).
function QD.COXNI.route(room, i, back)
    local out = {}
    local legs = QD.COXNI.LEGS[room.variant][i]
    local spawn = QD.COXNI.vasa_world(room, QD.COXNI.SPAWN[room.variant])
    if not back then
        for _, t in ipairs(legs) do out[#out + 1] = QD.COXNI.vasa_world(room, t) end
        return out, spawn
    end
    for k = #legs - 1, 1, -1 do out[#out + 1] = QD.COXNI.vasa_world(room, legs[k]) end
    out[#out + 1] = spawn
    return out, QD.COXNI.vasa_world(room, legs[#legs])
end

-- Is p on the straight (or diagonal) leg from a to b, a excluded?
function QD.COXNI.on_leg(a, b, p)
    local sx, sz = QD.COXNI.sign(b.x - a.x), QD.COXNI.sign(b.z - a.z)
    local n = math.max(math.abs(b.x - a.x), math.abs(b.z - a.z))
    for k = 1, n do
        -- (the npc walk: both axes while both differ, then the one left)
        local x = a.x + sx * math.min(k, math.abs(b.x - a.x))
        local z = a.z + sz * math.min(k, math.abs(b.z - a.z))
        if x == p.x and z == p.z then return true end
    end
    return false
end

-- The index of the leg he is walking now (the first whose span holds his
-- tile; his start tile is leg 1's), or nil when he is on none of them.
function QD.COXNI.leg_of(route, start, pos)
    if pos.x == start.x and pos.z == start.z then return 1 end
    local a = start
    for i, b in ipairs(route) do
        if QD.COXNI.on_leg(a, b, pos) then
            if i < #route and pos.x == b.x and pos.z == b.z then return i + 1 end
            return i
        end
        a = b
    end
    return nil
end

-- His SW at each of the next n tick ends, stepping a tile a tick along the
-- route's remaining waypoints from `pos` (the npc walk: both axes at once,
-- then the one left). -> list (k = 1..n), the tick-offset he arrives (or nil)
function QD.COXNI.walk_sim(pos, route, from, n)
    local out = {}
    local x, z = pos.x, pos.z
    local wi = from
    local arrive = nil
    for k = 1, n do
        while wi <= #route and route[wi].x == x and route[wi].z == z do wi = wi + 1 end
        if wi <= #route then
            x = x + QD.COXNI.sign(route[wi].x - x)
            z = z + QD.COXNI.sign(route[wi].z - z)
            if wi == #route and route[wi].x == x and route[wi].z == z and arrive == nil then arrive = k end
        end
        out[k] = { x = x, z = z }
    end
    return out, arrive
end

-- Chebyshev distance from (x, z) to an n-square at (sx, sz).
function QD.COXNI.dist(sx, sz, n, x, z)
    local dx, dz = 0, 0
    if x < sx then dx = sx - x elseif x > sx + n - 1 then dx = x - (sx + n - 1) end
    if z < sz then dz = sz - z elseif z > sz + n - 1 then dz = z - (sz + n - 1) end
    return math.max(dx, dz)
end

-- ================================================================ OBSERVE

function QD.COXNI.observe(S, F)
    local v, ids = S.v, S.ids
    local d = F.tick
    v.vasa, v.crystal = nil, nil
    for _, row in ipairs(F.npcs) do
        if row.npc_id == ids.dormant or row.npc_id == ids.walking or row.npc_id == ids.healing then v.vasa = row end
        if row.npc_id == ids.crystal then v.crystal = row end
    end
    v.hp[d] = F.hp
    v.hp[d - 80] = nil
    if v.vasa and v.room == nil then
        local _, me = api_drive.player_tile()
        v.room = QD.COX.room_at(v.vasa.x, v.vasa.z, me.level)
    end
    local row = v.vasa
    if row then
        v.seen_vasa = d
        v.form = (row.npc_id == ids.dormant and "dormant") or (row.npc_id == ids.healing and "healing") or "walking"
        if v.form ~= "dormant" and v.woke == nil then v.woke = d end
        if row.health_ratio and row.health_scale and row.health_scale > 0 then
            v.health = row.health_ratio / row.health_scale
        end
        -- his animations, first seen
        local seq, stick = row.seq_id, row.seq_tick
        if seq ~= nil and stick ~= nil and stick ~= v.seq_tick then
            v.seq_tick = stick
            if seq == ids.seq_spawn then
                v.rose = d
            elseif seq == ids.seq_heal_spawn then
                v.arrived = d
                v.on_crystal = true
            elseif seq == ids.seq_healed then
                if v.on_crystal and v.arrived then v.used = v.used + (d - v.arrived) end
                v.on_crystal = false
                v.left = d
                v.leaving = true
                v.leg_i = 1
            elseif seq == ids.seq_death then
                v.dying = d
            end
        end
        -- his splats: the store (cyan, 11) and the heal on a timeout (6)
        for _, sp in ipairs(row.splats or {}) do
            local key = tostring(sp.start) .. ":" .. tostring(sp.type)
            if not v.splat_seen[key] then
                v.splat_seen[key] = true
                if sp.type == 11 then v.stores = v.stores + 1 end
                if sp.type == 6 then
                    v.heals = v.heals + 1
                    v.healed_hp = v.healed_hp + (sp.damage or 0)
                    v.timeout = d
                end
            end
        end
        -- THE WALK CHECK: last tick's prediction of this tick's tile
        if v.pred_next and v.pred_tick == d - 1 then
            v.walk_checked = v.walk_checked + 1
            if v.pred_next.x ~= row.x or v.pred_next.z ~= row.z then
                v.walk_bad = v.walk_bad + 1
                if v.walk_bad <= 12 then
                    QD.raid._tob_trace(S, d, string.format("vasa walk: predicted %d,%d got %d,%d (%s ci %s left %s leaving %s)",
                        v.pred_next.x, v.pred_next.z, row.x, row.z, v.form, tostring(v.ci), tostring(v.ci_left), tostring(v.leaving)))
                end
            end
        end
        -- his tile history (the route position)
        if v.last_pos and (v.last_pos.x ~= row.x or v.last_pos.z ~= row.z) then
            v.moved = d
            v.prev_pos = v.last_pos
        end
        v.last_pos = { x = row.x, z = row.z }
    elseif v.seen_vasa and v.dead == nil and d - v.seen_vasa >= 2 and (v.dying or (v.health or 1) <= 0.05) then
        v.dead = d
    end
    -- a crystal lit: its npc appears (the same corner may light twice after
    -- a special, so the appearance is the pick, not the corner changing)
    if v.crystal and not v.crystal_was then v.lit_at = d end
    v.crystal_was = v.crystal ~= nil
    -- which crystal is lit: the npc on it
    if v.crystal and v.room then
        local ci = QD.COXNI.crystal_index(v.room, v.crystal.x, v.crystal.z)
        if ci and ci ~= v.ci then
            v.ci = ci
            v.leaving = false
            v.picked = d
            v.leg_i = 1
            v.crystals_seen = v.crystals_seen + 1
        end
    end
    -- my special: my own teleport animation (V8)
    local pr, players = api_drive.players()
    if pr == "ok" then
        for _, p in ipairs(players) do
            if p.me and p.anim == ids.seq_teleport and (v.G == nil or d - v.G > 20) then
                v.G = d
                v.specials = v.specials + 1
                v.used = 0
                v.on_crystal = false
                v.leaving = false
                v.ci_before = v.ci
                v.ci = nil
                QD.raid._tob_trace(S, d, string.format("vasa special %d at t%d, hp %d", v.specials, d, F.hp))
            end
        end
    end
    -- the explosion's check (V9/V11): what it took is what I had at the end
    -- of G+2, less 5
    if v.G and d == v.G + QD.COXNI.V.EXPLODE then
        -- (a stomp in the teleport's own tick lands before it reads)
        local had = v.hp[v.G + 2]
        if had and v.stomp_hp[v.G + 3] then had = had - v.stomp_hp[v.G + 3] end
        local before = v.hp[d - 1]
        if had and before then
            local took = before - F.hp
            local want = math.max(0, had - 5)
            local R = S.mon.rules.vasa_mech
            R.checked = R.checked + 1
            if took ~= want then
                QD.T2.monitor.note(S, "vasa_mech", d, string.format("explosion took %d, hp at G+2 was %d (want %d)", took, had, want))
            end
            v.explosions[#v.explosions + 1] = string.format("t%d %d/%d", d, took, want)
        end
    end
    -- the boulders: where and when (V21); first seen on d -> lands d+2,
    -- reading my tile at the end of d+1
    local qr, projs = api_drive.projectiles(0)
    if qr == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == ids.boulder then
                local key = p.dst_x .. ":" .. p.dst_z
                if v.rock_seen[key] == nil or d - v.rock_seen[key] > 2 then
                    v.rocks[#v.rocks + 1] = { x = p.dst_x, z = p.dst_z, land = d + 2, seen = d }
                    v.last_throw = d
                    v.throws = v.throws + 1
                end
                v.rock_seen[key] = v.rock_seen[key] or d
                if d - v.rock_seen[key] > 2 then v.rock_seen[key] = d end
            end
        end
    end
    -- my hp lost on a landing tick inside its 3x3 / under his 5x5 (the zeros)
    local lost = (v.hp[d - 1] or F.hp) - F.hp
    if lost > 0 then
        local was = QD.T2.tile_at(S, d - 1)
        for _, r in ipairs(v.rocks) do
            if r.land == d and was and math.abs(was.x - r.x) <= 1 and math.abs(was.z - r.z) <= 1 then
                v.rock_hits = v.rock_hits + 1
                v.rock_dmg = v.rock_dmg + lost
            end
        end
        if v.under_at and v.under_at == d - 1 then
            v.stomps = v.stomps + 1
            v.stomp_hp[d] = lost
        end
    end
    v.under_at = nil
    if row and row.npc_id ~= ids.dormant and QD.COXNI.dist(row.x, row.z, 5, F.me.x, F.me.z) == 0 then v.under_at = d end
    -- old rocks out
    local keep = {}
    for _, r in ipairs(v.rocks) do if r.land >= d - 1 then keep[#keep + 1] = r end end
    v.rocks = keep
end

-- The crystal he walks to when its npc is out of my sight (15 tiles; the
-- door after a dodge): the one route out whose legs hold his tile, off his
-- spawn. Two routes can share their first tiles: then nil until they part.
function QD.COXNI.guess_ci(S)
    local v = S.v
    local row = v.vasa
    if v.room == nil or row == nil then return nil end
    local here = { x = row.x, z = row.z }
    local found = nil
    for i = 1, 4 do
        local route, start = QD.COXNI.route(v.room, i, false)
        if not (here.x == start.x and here.z == start.z) and QD.COXNI.leg_of(route, start, here) ~= nil then
            if found ~= nil then return nil end
            found = i
        end
    end
    return found
end

-- His predicted SW tiles for k = 0..h (k = 0 now), and the tick he arrives.
function QD.COXNI.path(S, F, h)
    local v = S.v
    local row = v.vasa
    local here = { x = row.x, z = row.z }
    local route, start = nil, nil
    v.ci_guess = nil
    if v.room and v.form == "walking" then
        if v.leaving and v.ci_left then
            route, start = QD.COXNI.route(v.room, v.ci_left, true)
        elseif v.ci and not v.on_crystal then
            route, start = QD.COXNI.route(v.room, v.ci, false)
        elseif v.ci == nil and not v.on_crystal and not v.leaving then
            local g = QD.COXNI.guess_ci(S)
            if g then route, start = QD.COXNI.route(v.room, g, false) end
            v.ci_guess = g
        end
    end
    local out = { here }
    local wi = route and QD.COXNI.leg_of(route, start, here) or nil
    if route == nil or wi == nil then
        -- (off every leg: carry on as he last stepped, if he is walking)
        local dx, dz = 0, 0
        if route and v.prev_pos and v.moved == F.tick then
            dx, dz = here.x - v.prev_pos.x, here.z - v.prev_pos.z
        end
        for k = 1, h do out[k + 1] = { x = here.x + dx * k, z = here.z + dz * k } end
        return out, nil
    end
    local sim, arrive = QD.COXNI.walk_sim(here, route, wi, h)
    for k = 1, h do out[k + 1] = sim[k] end
    return out, arrive and (F.tick + arrive) or nil
end

-- ================================================================ PREDICT

function QD.COXNI.predict(S, F, B)
    local v, V = S.v, QD.COXNI.V
    local d = F.tick
    -- the boulders in the air: forbidden (fully dodgeable: two tiles)
    for i, r in ipairs(v.rocks) do
        if r.land >= d + 1 then
            B.hit({ name = "rock" .. i, x0 = r.x - 1, z0 = r.z - 1, x1 = r.x + 1, z1 = r.z + 1,
                    t0 = r.land - 1, t1 = r.land - 1, dmg_exp = V.BOULDER_EXP, dmg_max = V.BOULDER_MAX,
                    style = "ranged", factor = V.PRAY_FACTOR, forbid = true })
        end
    end
    if v.vasa == nil then return end
    if v.form == "dormant" then
        -- never under the pile (or beside it) when he wakes: the stomp starts
        -- at the group, and the group can catch me there
        local row = v.vasa
        B.hit({ name = "pile", x0 = row.x - 1, z0 = row.z - 1, x1 = row.x + 5, z1 = row.z + 5, t0 = d + 1, t1 = d + B.h,
                dmg_exp = V.STOMP_EXP, dmg_max = V.STOMP_MAX, forbid = true })
        return
    end
    local path, arrive = QD.COXNI.path(S, F, B.h)
    v.pred, v.arrive = path, arrive
    v.pred_next, v.pred_tick = path[2], d
    -- the boulders to come: the next throws while he walks (every 3 ticks
    -- from the last one seen), each at the tile I end the tick before it on
    if v.form == "walking" and v.last_throw and (arrive == nil or arrive > d + 1)
        and not (v.G and d < v.G + V.HITTABLE) then
        local T = v.last_throw + V.THROW
        while T <= d do T = T + V.THROW end
        local n = 0
        while T + 1 <= d + B.h and n < 3 and (arrive == nil or T < arrive) do
            -- (FORBIDDEN, as a seen one is: priced, the plan stood under it
            -- for five swings' worth -- s48 vasa12 t89-93; two tiles of
            -- running in the two ticks between aim and landing always clear it)
            B.mark({ name = "throw@" .. T, t_mark = T - 1, r = 1, t0 = T + 1, t1 = T + 1,
                     dmg_exp = V.MARK_EXP, dmg_max = V.BOULDER_MAX, forbid = true })
            T = T + V.THROW
            n = n + 1
        end
    end
    -- where he will stand to siphon the lit crystal: not mine (I would be
    -- under him, or cornered beside him, when he arrives)
    local sci = v.ci or v.ci_guess
    if v.form == "walking" and sci and not v.leaving and v.room then
        local legs = QD.COXNI.LEGS[v.room.variant][sci]
        local st = QD.COXNI.vasa_world(v.room, legs[#legs])
        B.hit({ name = "stand", x0 = st.x - 1, z0 = st.z - 1, x1 = st.x + 5, z1 = st.z + 5, t0 = d + 1, t1 = d + B.h,
                dmg_exp = V.STOMP_EXP, dmg_max = V.STOMP_MAX, forbid = true })
    end
    -- the stomp: never end a tick under him (and a tile of room while he
    -- walks: his next step is a guess at a turn)
    -- THE WAY AHEAD, priced (not forbidden): every tile within 2 of where
    -- he will stand at tick j costs a little for each tick before he gets
    -- there, so a nook on his way is left while the way out is still open,
    -- and the deeper in his way (the more of his footprints cover it) the
    -- dearer (s48c vasa29/34/39: caught in a dead end he walked into)
    if v.form == "walking" and v.arrive ~= nil or (v.form == "walking" and v.leaving) then
        for j = 2, B.h do
            local p = path[j + 1]
            local q = path[j]
            if p.x ~= q.x or p.z ~= q.z then
                B.hit({ name = "way" .. j, x0 = p.x - 2, z0 = p.z - 2, x1 = p.x + 6, z1 = p.z + 6,
                        t0 = d + 1, t1 = d + j - 1, dmg_exp = V.WAY_COST, dmg_max = 0 })
            end
        end
    end
    -- His 5x5 at each tick end, and a tile round it while he walks (his
    -- next step is a guess at a turn). (Forbidding his whole way ahead, or
    -- two tiles round him, left no tile at all in the room's nooks: s48b.)
    if v.form ~= "dormant" then
        for k = 1, B.h do
            local p = path[k + 1]
            local m = 0
            if v.form == "walking" then m = 1 end
            local t0 = d + k
            B.hit({ name = "stomp" .. k, x0 = p.x - m, z0 = p.z - m, x1 = p.x + 4 + m, z1 = p.z + 4 + m,
                    t0 = t0, t1 = d + k, dmg_exp = V.STOMP_EXP, dmg_max = V.STOMP_MAX, style = "melee",
                    factor = 0, forbid = true })
        end
    end
end

-- ================================================================ STRATEGY

function QD.COXNI.gear(S, style)
    local out = {}
    for _, e in ipairs(S.ids.sets[style]) do
        local obj, two = e, false
        if type(e) == "table" then obj, two = e.obj, e.two_hand end
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = two }
        end
    end
    return out
end

-- THE OPEN FACE: the tile beside the lit crystal (4x4 at c) that is not
-- under his stand (5x5 at st) and that the client's route reaches soonest
-- from where I am (asked once a crystal; api_drive.route).
function QD.COXNI.face(S, F, c, st)
    local v = S.v
    local key = c.x .. ":" .. c.z
    if v.face_key == key then return v.face end
    local best, bn = nil, nil
    for i = 0, 3 do
        for _, t in ipairs({ { c.x + i, c.z - 1 }, { c.x + i, c.z + 4 }, { c.x - 1, c.z + i }, { c.x + 4, c.z + i } }) do
            if st == nil or QD.COXNI.dist(st.x, st.z, 5, t[1], t[2]) >= 1 then
                local r, route = api_drive.route(t[1], t[2])
                if r == "ok" and route and route.nearest ~= true and route.tiles then
                    if bn == nil or #route.tiles < bn then best, bn = { x = t[1], z = t[2] }, #route.tiles end
                end
            end
        end
    end
    v.face_key, v.face = key, best
    QD.raid._tob_trace(S, F.tick, string.format("vasa crystal %s face %s (%s tiles)", key,
        best and (best.x .. "," .. best.z) or "none", tostring(bn)))
    return best
end

-- His stand tile (world SW of the 5x5) for the lit crystal, or nil.
function QD.COXNI.stand(S)
    local v = S.v
    if v.room == nil or v.ci == nil then return nil end
    local legs = QD.COXNI.LEGS[v.room.variant][v.ci]
    return QD.COXNI.vasa_world(v.room, legs[#legs])
end

-- THE SPECIAL, DODGED (Synq 1:08:07 and PecanBread 0:32:51, the "Vasa skip";
-- Beleti 0:27:23: "This is going to trigger the start of the room and skips
-- the teleport damage"): the special takes only raiders in the room (or
-- beside the entrance fire) at its group tick, and with none it is not cast
-- at all -- he goes straight to a crystal. So: wake him, and be out past the
-- fire (op Cross, 22.5% of my hitpoints each way, V6) before the group 13
-- ticks later; again whenever a crystal times out and he walks back to the
-- centre for another (V18). Back in, across the fire, once a crystal lights.
-- Two burns cost under half of what I have; the special costs all but 5.
-- -> true when the room's move this tick is the dodge (intents filled)
function QD.COXNI.dodge(S, F, B, intents)
    local v, V, ids = S.v, QD.COXNI.V, S.ids
    local d = F.tick
    if v.room == nil then return false end
    -- the reasons to be out: he is rising, or a crystal timed out (his walk
    -- back is to cast it)
    local why = nil
    local worth = F.hp >= V.DODGE_HP
    if v.form == "dormant" and v.rose and worth then why = "rise" end
    if v.form ~= "dormant" and v.timeout and v.timeout > (v.handled_to or -1) and worth then why = v.timeout end
    -- (early, while he siphons a crystal I will not break -- he stands still
    -- and throws nothing -- rather than across his walk back once it times out)
    if why == nil and worth and v.form == "healing" and v.crystal and v.arrived and v.arrived > (v.handled_to or -1) then
        if QD.COXNI.left(S, F) < QD.COXNI.crystal_need(S) * 0.8 then why = "wait" .. v.arrived end
        -- (and whatever the crystal: out before the 67 runs out, by as long
        -- as the run to the door takes -- s48 d8d vasa1: caught at the far
        -- corner by the timeout, his walk back between me and the door)
        local ix, iz = QD.COX.world(v.room, 15, 3)
        local door = math.max(math.abs(F.me.x - ix), math.abs(F.me.z - iz))
        if why == nil and QD.COXNI.left(S, F) <= door // 2 + 4 then why = "late" .. v.arrived end
    end
    if v.out == nil then
        if why == nil then return false end
        v.out = { why = why, since = d }
        v.dodges_tried = v.dodges_tried + 1
        QD.raid._tob_trace(S, d, "vasa dodge: out (" .. tostring(why) .. ")")
    end
    local fx, fz = QD.COX.world_sized(v.room, 15, 1, 2, 2)
    local ox, oz = QD.COX.world(v.room, 15, -2)
    local _, lz = QD.COX.template(v.room, F.me.x, F.me.z)
    local inside = QD.COX.room_at(F.me.x, F.me.z, v.room.level)
    inside = inside ~= nil and inside.ox == v.room.ox and inside.oz == v.room.oz
    -- back in once the group tick has passed with me out: a crystal lit since
    -- I left; or he has walked off his spawn after standing on it (the group
    -- is the tick after he arrives there; a far crystal is out of my 15
    -- tiles of sight from the door); or 14 ticks after the rise (the group is
    -- the rise + 13); or the special came anyway (too late: take it inside)
    local spawn = QD.COXNI.vasa_world(v.room, QD.COXNI.SPAWN[v.room.variant])
    local row = v.vasa
    -- (a crystal that times out while I wait outside means another special:
    -- its skip is a new one to see)
    if v.timeout and v.out.passed and v.timeout > v.out.passed then
        v.out.passed, v.out.at_spawn = nil, nil
        v.out.since = v.timeout
        v.out.why = v.timeout
        v.out.due = v.timeout
    end
    if row and row.x == spawn.x and row.z == spawn.z and v.form == "walking" then
        v.out.at_spawn = v.out.at_spawn or d
    end
    -- (his timeout out of my sight: the one I left for, or the one left() put
    -- ahead when I left early; the group is his walk back to the middle and
    -- one tick -- 16 from the timeout, d8o vasa0 t187 -> lit t203 -- so 20
    -- after it the skip is past. Waiting for a sight of him instead, d8o
    -- vasa0 sat out from t170 to t300 while he siphoned the next crystal to
    -- its end and healed 99)
    if v.out.due == nil then
        v.out.due = type(v.out.why) == "number" and v.out.why or (d + math.max(0, QD.COXNI.left(S, F)))
    end
    local passed = (v.lit_at and v.lit_at > v.out.since)
        or (v.out.why ~= "rise" and d >= v.out.due + 20)
        or (v.out.at_spawn and row and (row.x ~= spawn.x or row.z ~= spawn.z))
        or (v.out.why == "rise" and v.rose and d >= v.rose + 14 and d <= v.rose + 30)
    if passed and not v.out.passed then
        v.out.passed = d
        -- (the skip refilled his 67 (V17): left() counts from his arrival)
        v.used = 0
    end
    passed = v.out.passed ~= nil
    -- (back in while he siphons only when what is left of his 67 covers the
    -- run to his crystal and its break; else its timeout is the next special.
    -- Never in at all, d8o vasa0: out from t170 to t300 while he siphoned a
    -- crystal lit at t203 to its end and healed 99)
    local hold = false
    if v.form == "healing" then
        local run = 12
        if v.crystal then run = math.max(math.abs(F.me.x - v.crystal.x), math.abs(F.me.z - v.crystal.z)) // 2 + 3 end
        if QD.COXNI.left(S, F) < QD.COXNI.crystal_need(S) * 0.8 + run then
            passed = false
            -- (in already -- back across on the clock, he out of sight: by
            -- the door until this crystal times out, then out again; the
            -- siphon throws nothing and two more burns buy nothing)
            hold = inside and lz >= 1
        end
    end
    -- (and not into his way: the door is a corridor, and his walk to a crystal
    -- by it crosses the tiles inside the fire -- d8f vasa6/7, stomped there)
    local clear = true
    if passed and row and v.pred and (not inside or lz <= 0) then
        local ix, iz = QD.COX.world(v.room, 15, 3)
        for k = 1, math.min(#v.pred, 10) do
            local p = v.pred[k]
            if QD.COXNI.dist(p.x, p.z, 5, ix, iz) <= 4 then clear = false end
        end
    end
    if (passed and clear) or (v.G and v.G >= v.out.since) then
        if not inside or lz <= 0 then
            intents.mode = "dodge-in"
            intents.loc_op = { loc = ids.fire, op = 1, x = fx, z = fz }
            return true
        end
        if v.out.passed and not (v.G and v.G >= v.out.since) then
            v.dodged = v.dodged + 1
            -- the special he cast at nobody refilled his 67 (V17), as a
            -- special I took would have (and he may be on its crystal now)
            v.used = 0
            if v.form ~= "healing" then v.on_crystal = false end
            v.timeout_cleared = v.timeout
        end
        v.handled_to = d
        v.out = nil
        QD.raid._tob_trace(S, d, "vasa dodge: back in")
        return false
    end
    intents.gear = QD.COXNI.gear(S, "ranged")
    if hold then
        local ix, iz = QD.COX.world(v.room, 15, 3)
        intents.mode = "dodge-hold"
        B.pull({ name = "door", x = ix, z = iz, size = 1, weight = 1.0, t0 = d, t1 = d + B.h })
        return true
    end
    if inside and lz >= 3 then
        intents.mode = "dodge-out"
        -- to the door by the plan (his stomp and his boulders still priced),
        -- and the Cross only from beside it: the op's own walk is blind
        local ix, iz = QD.COX.world(v.room, 15, 3)
        if math.max(math.abs(F.me.x - ix), math.abs(F.me.z - iz)) <= 2 then
            intents.loc_op = { loc = ids.fire, op = 1, x = fx, z = fz }
        else
            -- (along the client's own route: a straight pull at the door
            -- sat in the nook it started in -- d8e vasa1 t272-296)
            local tx, tz = ix, iz
            local rr, route = api_drive.route(ix, iz)
            if rr == "ok" and route and route.tiles and #route.tiles > 0 then
                local w = route.tiles[math.min(#route.tiles, 10)]
                tx, tz = w.x or w[1], w.z or w[2]
            end
            B.pull({ name = "door", x = tx, z = tz, size = 1, weight = 1.0, t0 = d, t1 = d + B.h })
        end
    else
        intents.mode = "dodge-wait"
        B.pull({ name = "outside", x = ox, z = oz, size = 1, weight = 1.0, t0 = d, t1 = d + B.h })
    end
    return true
end

-- Ticks to break the lit crystal from what it has left (its health bar when
-- seen, else whole) with the weapon I hold for it.
function QD.COXNI.crystal_need(S)
    local v, V, ids = S.v, QD.COXNI.V, S.ids
    local hp = V.CRYSTAL_HP
    local c = v.crystal
    if c and c.health_ratio and c.health_scale and c.health_scale > 0 then
        hp = V.CRYSTAL_HP * c.health_ratio / c.health_scale
    end
    local stab = QD.raid._tob_worn(S, ids.sword) or QD.raid._tob_count(S, ids.sword) > 0
    return hp / (stab and V.CRYSTAL_DPS or V.CRYSTAL_DPS_SLASH)
end

-- Siphon ticks left before the next special (V15).
function QD.COXNI.left(S, F)
    local v = S.v
    local used = v.used
    if v.on_crystal and v.arrived then used = used + (F.tick - v.arrived) end
    return QD.COXNI.V.WINDOW - used
end

function QD.COXNI.strategy(S, F, B)
    local v, V, ids = S.v, QD.COXNI.V, S.ids
    local d = F.tick
    local intents = { mode = "approach", eat_below = V.BREW, brew_below = V.BREW }
    QD.COXNI.drinks(S, F, intents)
    -- (out past the fire he can be out of my 15 tiles of sight: wait there)
    if v.out ~= nil and v.vasa == nil and not v.dead then
        local ox, oz = QD.COX.world(v.room, 15, -2)
        -- (back across on the clock: his skip is past 20 ticks after the
        -- timeout -- QD.COXNI.dodge)
        if v.out.why ~= "rise" and v.out.due and d >= v.out.due + 20 then
            local _, lz = QD.COX.template(v.room, F.me.x, F.me.z)
            if lz <= 0 then
                local fx, fz = QD.COX.world_sized(v.room, 15, 1, 2, 2)
                intents.mode = "dodge-in"
                intents.loc_op = { loc = ids.fire, op = 1, x = fx, z = fz }
                return intents
            end
            -- in, and he is at a far crystal out of sight of the door: the
            -- skip refilled his 67; he lit it about 16 after the timeout and
            -- is there or on his way (an early arrival: left() errs short)
            v.dodged = v.dodged + 1
            v.used = 0
            v.arrived = v.out.due + 17
            v.on_crystal = true
            v.handled_to = v.out.due + 16
            v.timeout_cleared = v.timeout
            v.out = nil
            QD.raid._tob_trace(S, d, "vasa dodge: back in (on the clock)")
            return QD.COXNI.strategy(S, F, B)
        end
        intents.mode = "dodge-wait"
        B.pull({ name = "outside", x = ox, z = oz, size = 1, weight = 1.0, t0 = d, t1 = d + B.h })
        return intents
    end
    if v.vasa == nil then
        intents.mode = v.dead and "done" or "search"
        -- THE WAY IN (the whole raid: wherever the room before left me): on
        -- toward the room ahead, its middle, until he is in sight
        if not v.dead then
            local here = QD.COX.here()
            if here ~= nil then
                local cx, cz = here.ox + 16, here.oz + 16
                if here.name ~= "vasa" then
                    local _, _, nx, nz = QD.COX.next_origin(here)
                    cx, cz = nx, nz
                end
                B.pull({ name = "in", x = cx - 1, z = cz - 1, size = 3, weight = 1.0, t0 = d, t1 = d + B.h })
            end
        end
        return intents
    end
    local row = v.vasa
    local mid = { x = row.x + 2, z = row.z + 2 }
    if not (v.G and d < v.G + V.HITTABLE) and QD.COXNI.dodge(S, F, B, intents) then
        return intents
    end
    -- THE APPROACH (V5): walk in on ranged gear, overloaded, low; within 7 of
    -- his middle wakes him
    if v.form == "dormant" then
        intents.gear = QD.COXNI.gear(S, "ranged")
        intents.eat_below, intents.brew_below = V.BREW_PRE, V.BREW_PRE
        B.pull({ name = "wake", x = mid.x - 1, z = mid.z - 1, size = 3, weight = 1.0, t0 = d, t1 = d + B.h })
        return intents
    end
    -- THE SPECIAL (V8-V11): from the group on, no swing until G+12; no heal
    -- read before the end of G+2 (a decision at d lands at d+1)
    local in_special = v.G and d < v.G + V.HITTABLE
    if in_special then
        intents.mode = (d < v.G + V.STUN) and "stunned" or "explosion"
        if d >= v.G + 2 then
            intents.eat_below, intents.brew_below = V.BREW_STUN, V.BREW_STUN
        else
            intents.eat_below, intents.brew_below = V.BREW_PRE, V.BREW_PRE
        end
        intents.gear = QD.COXNI.gear(S, "ranged")
        -- protection is refused until the explosion tick (G+11)
        if d >= v.G + V.EXPLODE - 1 and not F.lit["protectfrommissiles"] and (F.prayer or 0) > 0 then
            intents.pray = "protectfrommissiles"
        end
        return intents
    end
    -- the special coming: he left a crystal on a timeout (the heal splat),
    -- or the timer cannot cover another crystal -- stay low
    local pre = ((v.timeout and v.G and v.timeout > v.G) or (v.timeout and v.G == nil)) and v.timeout ~= v.timeout_cleared
    local left = QD.COXNI.left(S, F)
    if pre then
        intents.eat_below, intents.brew_below = V.BREW_PRE, V.BREW_PRE
    end
    -- THE CRYSTAL (V20): he siphons, it can be hit; worth it only when the
    -- timer covers the break
    if v.form == "healing" and v.crystal then
        local hp = V.CRYSTAL_HP
        if v.crystal.health_ratio and v.crystal.health_scale and v.crystal.health_scale > 0 then
            hp = V.CRYSTAL_HP * v.crystal.health_ratio / v.crystal.health_scale
        end
        local stab = QD.raid._tob_worn(S, ids.sword) or QD.raid._tob_count(S, ids.sword) > 0
        local need = hp / (stab and V.CRYSTAL_DPS or V.CRYSTAL_DPS_SLASH)
        local weapon = stab and ids.sword or ids.tentacle
        if left >= need * 0.8 then
            intents.mode = "crystal"
            intents.gear = QD.COXNI.gear(S, stab and "melee" or "melee_slash")
            if QD.raid._tob_worn(S, weapon) then
                B.target({ name = "crystal", npc = v.crystal, size = V.CSIZE, range = 1, speed = V.SWORD_SPEED,
                           value = V.CRYSTAL_VALUE })
                intents.mode = "crystal"
            end
            local f = QD.COXNI.face(S, F, v.crystal, { x = row.x, z = row.z })
            if f then
                B.pull({ name = "face", x = f.x, z = f.z, size = 1, weight = 1.0, t0 = d, t1 = d + B.h })
            end
            intents.pray_boost = ((F.prayer or 0) >= 20) and "piety" or nil
            intents.pray_off = { "protectfrommissiles" }
            if not F.lit["protectfrommissiles"] then intents.pray_off = nil end
        else
            intents.mode = "wait"
            intents.eat_below, intents.brew_below = V.BREW_PRE, V.BREW_PRE
            intents.gear = QD.COXNI.gear(S, "ranged")
        end
        return intents
    end
    -- THE RANGING (V21): he walks; the pipe, Protect from Missiles, Rigour
    intents.mode = pre and "range-pre" or "range"
    intents.gear = QD.COXNI.gear(S, "ranged")
    if QD.raid._tob_worn(S, ids.pipe) and v.form == "walking" then
        local t_ok1 = nil
        if v.arrive then t_ok1 = v.arrive + 1 end
        -- a boulder in the air over my tile: walk first, swing after (an
        -- attack press in reach does not move me, whatever the chase model
        -- hoped: dbg vasa0 t294-296, six hits so)
        local t_ok0 = nil
        for _, r in ipairs(v.rocks) do
            if r.land >= d + 1 and math.abs(F.me.x - r.x) <= 1 and math.abs(F.me.z - r.z) <= 1 then
                t_ok0 = math.max(t_ok0 or 0, r.land)
            end
        end
        if t_ok0 == nil or t_ok1 == nil or t_ok0 <= t_ok1 then
            B.target({ name = "vasa", npc = row, size = V.SIZE, range = V.PIPE_RANGE, speed = V.PIPE_SPEED,
                       value = V.SWING_VALUE, path = v.pred, t_ok0 = t_ok0, t_ok1 = t_ok1 })
        end
    end
    if not F.lit["protectfrommissiles"] and (F.prayer or 0) > 0 then intents.pray = "protectfrommissiles" end
    intents.pray_boost = ((F.prayer or 0) >= 30) and "rigour" or nil
    -- (no drift to the lit crystal before he is on it: the nook beside it is
    -- on his way in -- s48 vasa20/27/38/46, stomped there; the crystal mode's
    -- face pull takes me round once he stands still)
    return intents
end

-- The overload (+) as the room starts (its five 10s lower me for the special,
-- V9), again when it ends; revitalisation for a drained Ranged; prayer
-- enhance under 50 points.
function QD.COXNI.drinks(S, F, intents)
    local v, ids = S.v, S.ids
    local n = 0
    for i, obj in ipairs(ids.overloads) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    if v.ovl_n ~= nil and n < v.ovl_n then v.ovl_until = F.tick + 501 end
    v.ovl_n = n
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    if v.G and F.tick < v.G + QD.COXNI.V.HITTABLE then return end
    local pick = nil
    -- (the overload once, as the room starts, while there is hp for its 50:
    -- it also lifts every drained stat back over base each 25 ticks, worth a
    -- restore a cycle -- and its 50 come back as it ends)
    if (v.ovl_until or -1) <= F.tick and F.hp >= 56 then pick = QD.raid._tob_first(S, ids.overloads) end
    -- (a brew drains Attack, Strength, Ranged and Magic 10% + 2 a sip: a
    -- restore pairs with it -- the prep relay's raid4 reached Attack 0 on
    -- brews alone and the crystals stopped breaking)
    if pick == nil and (F.prayer or 0) < 50 then pick = QD.raid._tob_first(S, ids.enhances) end
    if pick then
        intents.drink = pick
        S.last_drink = F.tick
        return
    end
    -- (the shared rule, QD.COX.restore_intent, at this room's own gap -- at 3
    -- under raid4 drank its 5 restore sips with its 11 brew sips and had none
    -- left)
    local stats = { "ranged", "attack", "strength" }
    local _, st = QD.COX.restore_due(F, stats, QD.COXNI.V.RESTORE_GAP)
    if QD.COX.restore_intent(S, F, intents, stats, QD.COXNI.V.RESTORE_GAP) then
        local lv = F.levels[st]
        v.restore_log[#v.restore_log + 1] = string.format("t%d %s %d/%d", F.tick, st, lv.level, lv.base)
    end
end

-- ================================================================ THE ROOM

QD.T2.register("cox_vasa", {
    ids = function(opts) return QD.COXNI.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_vasa_solve"
        S.max_ticks = opts.max_ticks or 1500
        S.t2.plan_opts = { h = opts.h or QD.COXNI.V.H, beam = opts.beam or QD.COXNI.V.BEAM }
        S.v = { hp = {}, stomp_hp = {}, splat_seen = {}, rock_seen = {}, rocks = {}, explosions = {},
                used = 0, specials = 0, stores = 0, heals = 0, healed_hp = 0, throws = 0,
                restore_log = {}, rock_hits = 0, rock_dmg = 0, stomps = 0, crystals_seen = 0, leg_i = 1, walk_checked = 0, walk_bad = 0, dodged = 0, dodges_tried = 0 }
        S.mon.rules.vasa_mech = { n = 0, checked = 0, traced = 0 }
    end,

    -- Rapid (style slot 1: the blowpipe's Rapid, the dragon sword's Lunge --
    -- stab), before the loop (cox2_olm.lua's enter).
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
        QD.raid._tob_tab(S, ids.inv_tab)
    end,

    predict = function(S, F, B)
        QD.COXNI.observe(S, F)
        -- (the crystal he leaves: remembered for the route back)
        if S.v.leaving and S.v.ci then S.v.ci_left = S.v.ci end
        QD.COXNI.predict(S, F, B)
    end,

    strategy = function(S, F, B)
        local intents = QD.COXNI.strategy(S, F, B)
        S.t2.want.boost = intents.pray_boost
        intents.pray_boost = nil
        return intents
    end,

    -- done: he is dead AND the way out is open (the exit crystals shattered)
    done = function(S, F)
        local v = S.v
        if not v.dead then return nil end
        local lr, rows = api_drive.locs(40)
        if lr == "ok" then
            for _, row in ipairs(rows) do
                local mine = v.room == nil or (row.x >= v.room.ox and row.x < v.room.ox + 32
                    and row.z >= v.room.oz and row.z < v.room.oz + 32)
                if mine and (row.loc_id == S.ids.exit or row.resolved_loc_id == S.ids.exit) then
                    if F.tick - v.dead < 10 then return nil end
                    QD.raid._tob_trace(S, F.tick, "vasa: dead, but an exit crystal still stands at " .. row.x .. "," .. row.z)
                    return "blocked"
                end
            end
        end
        return "ok"
    end,

    summary = function(S)
        local v = S.v
        return string.format("vasa: specials %d (explosions %s), crystals %d, stores %d, timeouts %d (healed %d), throws %d, "
            .. "boulder hits %d (%d dmg), stomps %d, walk mispredicted %d/%d, mech mismatches %d/%d, woke t%s, dead t%s, specials dodged %d/%d, restores at %s",
            v.specials, table.concat(v.explosions, " "), v.crystals_seen, v.stores, v.heals, v.healed_hp, v.throws,
            v.rock_hits, v.rock_dmg, v.stomps, v.walk_bad, v.walk_checked, S.mon.rules.vasa_mech.n, S.mon.rules.vasa_mech.checked,
            tostring(v.woke), tostring(v.dead), v.dodged, v.dodges_tried,
            #v.restore_log > 0 and table.concat(v.restore_log, " ") or "none")
    end,
})
