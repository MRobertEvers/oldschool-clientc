-- quest-driver / cox2_tightrope: THE TIGHTROPE, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/tightrope_solver.md is the plan, tightrope_spec.md
-- the mechanics (rows Tn); docs/CONTENT_SOLVER_MANUAL.md the method. A QD.T2
-- room (tob2_run.lua): PREDICT and STRATEGY here; the planner, executive,
-- monitor and supplies shared.
--
-- THE ROOM AS A SOLO PLAYER PLAYS IT (round 4, the raid's supply budget):
--   KILL   the mages first (their lower max hit), then the rangers, ONE AT A
--          TIME: the guards are passive (T2) and only the one I hit fights
--          back (wiki Strategies "Tightrope": "the mages should be dealt
--          with first, due to their lower max hit").
--          SAFESPOTTED, as the wiki gives it for a solo: "enemies can be
--          'safespotted' by using a weapon with a range of 8 or greater (to
--          ensure as much distance as possible), then running away until
--          they are no longer rendered on the map. Once this occurs, the
--          enemies will lose aggression. While slower, it does ensure that
--          supplies are not touched, as the rangers can deal considerable
--          damage." Synq 0:28:41 the same ("attack any mob and run out of its
--          render distance ... go back and forth attacking it without taking
--          any damage at all"). The weapon: the trident of the swamp on
--          Longrange (9 tiles, 4 ticks; Synq 0:26:22 "the shadow on the
--          rangers": their magic defence is their Defence, bonus 0, wiki
--          infobox) in the mage switch. A guard keeps me as its target
--          only while I end a tick within LEASH of its post (cox.npc
--          maxrange 8 + 1: RSProx shows a raider 9 off a post shot for 40
--          ticks), so I step in for each cast and out before its flinch
--          (the wiki's run, in miniature); one that does swing at me is
--          prayed against.
--   CROSS  with every guard dead nothing wakes (T3); take the keystone on the
--          far side, cross back, Dispel the barrier in front of the exit.
-- The rope skip (crossing under fire) is not played: it dies 1 in 20 (Nang).
-- Perception: npc rows (the guards), locs (rope ends, keystone, barrier), the
-- backpack. Nothing else.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.COXT = QD.COXT or {}
local CT = QD.COXT

CT.V = {
    H = 8, BEAM = 32,
    -- the safespot weapon: the trident on Longrange (+2: 9 tiles), 4 ticks
    RANGE = 9, SPEED = 4,
    SWING_VALUE = 3,
    -- a guard drops a target more than LEASH tiles from its post (the
    -- engine's leash, cox.npc maxrange 8 + 1, measured from the spawn tile)
    LEASH = 9,
    -- no closer to the guard than this + 1: a cast from 8 or 9 lands in 4
    CLOSE = 7,
    FREE = 1,                   -- pack slots the keystone needs
    NEAR_KEY = 6,               -- on the keystone's side of the rope
    SEE_LOC = 10,               -- a loc this near that is not listed is gone
    -- heals: nothing here should hit me, so only a low hp is topped up
    EAT_BELOW = 30, BREW_BELOW = 30,
    RESTORE_GAP = 20,           -- QD.COX.restore_intent's gap on Magic
    HIT_PRAY = 8,               -- ticks a protection stays up after a hit lands
}

function CT.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_tightrope", opts)
    local room = QD.raid._tob_symbols("cox_tightrope", {
        { "ranger", "npc", "raids_tightrope_ranger" }, { "mage", "npc", "raids_tightrope_mage" },
        { "rope", "loc", "raids_tightrope_end" }, { "key_loc", "loc", "raids_tightrope_keystone_loc" },
        { "barrier", "loc", "raids_tightrope_barrier" },
        { "keystone", "obj", "raids_tightrope_keystone" },
        { "style_slot_3", "component", "combat_interface:style_slot_3" },
        { "com_mode", "varp", "varp43_com_mode" },
        { "trident", "obj", "toxic_tots_charged" }, { "occult", "obj", "occult_necklace" },
        { "imbued_cape", "obj", "ma2_saradomin_cape" }, { "mystic_top", "obj", "mystic_robe_top" },
        { "mystic_bottom", "obj", "mystic_robe_bottom" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_tightrope: no combat tab")
    ids.combat_tab = combat_tab
    -- the mage switch (the Vespula solver's magic set, the defender kept),
    -- and what it takes off, put back on at the end
    ids.magic_set = { ids.trident, ids.occult, ids.imbued_cape, ids.mystic_top, ids.mystic_bottom }
    ids.home_set = {}
    for _, name in ipairs({ "barbassault_penance_fighter_torso", "black_dragonhide_chaps", "tzhaar_cape_fire",
                            "enchanted_onyx_amulet", "abyssal_tentacle" }) do
        local r, id = api_drive.symbol("obj", name)
        if r == "ok" then ids.home_set[#ids.home_set + 1] = id end
    end
    -- the keystone needs a slot: what this room never uses goes on the floor
    -- before the first crossing and comes back after the Dispel
    ids.droppable = {}
    for _, name in ipairs({ "xbows_crossbow_dragon", "bh_rune_pouch", "bgs", "dragon_shortsword",
                            "avas_assembler", "black_dragonhide_body" }) do
        local r, id = api_drive.symbol("obj", name)
        if r == "ok" then ids.droppable[#ids.droppable + 1] = id end
    end
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_tightrope: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    ids.brews = QD.COX.supply_ids("heal")
    -- prayer: revitalisation (stats and prayer), then the prayer enhance --
    -- the raid's kit carries three and raid6 reached the ice demon with the
    -- revitalisations drunk and no prayer, so Protect from Missiles went out,
    -- the demon cast Ice Burst and its freeze held me under the splats
    ids.restores = QD.COX.supply_ids("restore")
    for _, id in ipairs(QD.COX.supply_ids("prayer")) do ids.restores[#ids.restores + 1] = id end
    -- (a drained stat: a restore, never a prayer-only dose)
    ids.stat_restores = QD.COX.supply_ids("restore")
    ids.combats, ids.rangings = {}, {}
    return ids
end

function CT.cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

function CT.in_room(S, x, z)
    local lx, lz = QD.COX.template(S.room, x, z)
    return lx >= 0 and lx < 32 and lz >= 0 and lz < 32
end

function CT.observe(S, F)
    -- (my level: a client holds every plane's locs, and a room on the other
    -- floor can share this cell -- ice26 met the Muttadiles' axe one level up)
    local _, me_tile = api_drive.player_tile()
    local lvl = me_tile and me_tile.level
    local o, ids = S.o, S.ids
    o.mages, o.rangers = {}, {}
    for _, row in ipairs(F.npcs) do
        local nid = row.npc_id
        if (nid == ids.mage or nid == ids.ranger) and (row.level == nil or row.level == lvl) and CT.in_room(S, row.x, row.z) then
            if row.npc_id == ids.mage then o.mages[#o.mages + 1] = row
            elseif row.npc_id == ids.ranger then o.rangers[#o.rangers + 1] = row end
        end
    end
    o.ropes, o.key_loc, o.barrier = {}, nil, nil
    local lr, rows = api_drive.locs(40)
    if lr == "ok" then
        for _, row in ipairs(rows) do
            local lid = row.loc_id
            if (lid == ids.rope or lid == ids.key_loc or lid == ids.barrier) and (row.level == nil or row.level == lvl) and CT.in_room(S, row.x, row.z) then
                if row.loc_id == ids.rope then o.ropes[#o.ropes + 1] = row
                elseif row.loc_id == ids.key_loc then o.key_loc = row
                elseif row.loc_id == ids.barrier then o.barrier = row end
            end
        end
    end
    if o.barrier then
        o.barrier_seen = F.tick
        o.barrier_last = o.barrier
    elseif o.barrier_last and not o.barrier_gone
        and CT.cheb(F.me.x, F.me.z, o.barrier_last.x, o.barrier_last.z) > CT.V.SEE_LOC then
        -- out of the client's loc view (the far side of the rope, tr17), not
        -- gone: it stands until I see its tile empty
        o.barrier = o.barrier_last
    elseif o.barrier_last then
        -- its tile in view and empty: dispelled, for good (a walk away
        -- afterwards brought the stand-in back and sent me across again)
        o.barrier_gone = true
    end
    if #o.mages + #o.rangers > 0 then o.guards_seen = true end
    o.held = QD.raid._tob_count(S, ids.keystone) > 0
end

-- The guard to kill now: a mage (nearest) while any stands, then a ranger.
-- The one already fighting me first, whatever it is (it swings either way).
function CT.pick(S, F)
    local o = S.o
    local function nearest(list)
        local best, bd = nil, nil
        for _, r in ipairs(list) do
            local d = CT.cheb(F.me.x, F.me.z, r.x, r.z)
            if bd == nil or d < bd then best, bd = r, d end
        end
        return best
    end
    if o.fighting then
        for _, list in ipairs({ o.mages, o.rangers }) do
            for _, r in ipairs(list) do
                if QD.T2.npc_key(r) == o.fighting then return r, (list == o.mages) and "magic" or "ranged" end
            end
        end
        o.fighting = nil
    end
    local m = nearest(o.mages)
    if m then return m, "magic" end
    if #o.rangers <= o.cross_with then return nil, nil end
    local r = nearest(o.rangers)
    if r then return r, "ranged" end
    return nil, nil
end

-- The room done: what I put on the floor back in the pack, and the torso
-- back on (the raid's melee rooms wear it).
function CT.pickup(S, F, intents)
    local o, ids = S.o, S.ids
    intents.mode = "pickup"
    local gear = {}
    for _, id in ipairs(ids.home_set) do
        if not QD.raid._tob_worn(S, id) and QD.raid._tob_count(S, id) > 0 then
            gear[#gear + 1] = { obj = id, two_hand = false }
        end
    end
    if #gear > 0 then intents.gear = gear end
    for id in pairs(o.dropped) do
        if QD.raid._tob_count(S, id) > 0 then o.dropped[id] = nil end
    end
    if next(o.dropped) == nil then
        o.picked = intents.gear == nil
        return intents
    end
    local best, bd = nil, nil
    local orr, rows = api_drive.objs(30)
    if orr == "ok" then
        for _, row in ipairs(rows) do
            if o.dropped[row.obj_id] and CT.in_room(S, row.x, row.z) then
                local d = CT.cheb(F.me.x, F.me.z, row.x, row.z)
                if bd == nil or d < bd then best, bd = row, d end
            end
        end
    end
    if best == nil then
        o.pickup_misses = (o.pickup_misses or 0) + 1
        if o.pickup_misses > 5 then o.picked = true end
        return intents
    end
    intents.world_op = { kind = "obj", id = best.obj_id, op = 3, x = best.x, z = best.z }
    return intents
end

function CT.nearest_rope(S, F)
    local best, bd = nil, nil
    for _, r in ipairs(S.o.ropes) do
        local d = CT.cheb(F.me.x, F.me.z, r.x, r.z)
        if bd == nil or d < bd then best, bd = r, d end
    end
    return best
end

-- The guards' posts (spawn tiles, cox_puzzles.rs2 ~cox_tightrope_*_tile)
-- for a kind, in world tiles.
CT.POSTS = {
    mage = { [0] = { { 21, 19 }, { 23, 19 } }, [1] = { { 21, 19 }, { 23, 19 } }, [2] = { { 12, 21 }, { 12, 23 } } },
    ranger = { [0] = { { 20, 26 }, { 22, 26 } }, [1] = { { 20, 26 }, { 22, 26 } }, [2] = { { 5, 20 }, { 5, 22 } } },
}

-- The box no tile of which may end a tick while I fight a guard of `kind`:
-- within LEASH of either post of that kind (two guards of a kind can stand
-- on either post, R6). -> x0, z0, x1, z1
function CT.leash_box(S, kind)
    local x0, z0, x1, z1
    for _, t in ipairs(CT.POSTS[kind][S.room.variant]) do
        local x, z = QD.COX.world(S.room, t[1], t[2])
        x0 = x0 and math.min(x0, x) or x
        z0 = z0 and math.min(z0, z) or z
        x1 = x1 and math.max(x1, x) or x
        z1 = z1 and math.max(z1, z) or z
    end
    local L = CT.V.LEASH
    return x0 - L, z0 - L, x1 + L, z1 + L
end

function CT.strategy(S, F, B)
    local o, V, ids = S.o, CT.V, S.ids
    local intents = { eat_below = V.EAT_BELOW, brew_below = QD.COX.brew_floor(S, V.BREW_BELOW) }
    local function want_pray(name)
        if not F.lit[name] and (F.prayer or 0) > 0 then return name end
        return nil
    end
    -- a hit landed (hp down at or under base): the guard reached me after all
    if o.last_hp_seen and F.hp < o.last_hp_seen and o.last_hp_seen <= (F.hp_base or 99) then o.hit_at = F.tick end
    o.last_hp_seen = F.hp
    -- SUPPLIES: the trident's Magic, drained by the brews -- a restore at
    -- the room's gap (QD.COX.restore_intent, the shared rule)
    QD.COX.restore_intent(S, F, intents, { "magic" }, V.RESTORE_GAP)
    S.t2.want.boost = nil
    S.t2.want.boost_stat = nil
    -- IN: the room before's Passage can leave me on our shared edge, a tile
    -- outside; nothing below is decided from there
    if o.barrier ~= nil and not CT.in_room(S, F.me.x, F.me.z) then
        intents.mode = "enter"
        B.pull({ name = "in", x = S.centre.x - 1, z = S.centre.z - 1, size = 3, weight = 1,
                 t0 = F.tick, t1 = F.tick + B.h })
        return intents
    end
    local row, style = CT.pick(S, F)
    if row then
        local kind = (style == "magic") and "mage" or "ranger"
        intents.mode = "kill-" .. kind
        o.fighting = QD.T2.npc_key(row)
        -- THE MAGE SWITCH (one-handed: the defender stays, nothing needs a slot)
        local gear = {}
        for _, id in ipairs(ids.magic_set) do
            if not QD.raid._tob_worn(S, id) and QD.raid._tob_count(S, id) > 0 then
                gear[#gear + 1] = { obj = id, two_hand = false }
            end
        end
        if #gear > 0 then intents.gear = gear end
        local worn = QD.raid._tob_worn(S, ids.trident)
        -- Longrange once the trident is worn (varp43_com_mode 2; a powered
        -- staff's button writes 3, clamped to 2 at its first swing: Vespula)
        if worn and not o.style_ok then
            local mr, mode = api_drive.varp(ids.com_mode)
            if mr == "ok" and mode >= 2 then
                o.style_ok = true
            elseif F.tick - (o.style_pressed or -10) >= 3 then
                QD.raid._tob_tab(S, ids.combat_tab)
                api_drive.if_click(ids.style_slot_3, 1)
                o.style_pressed = F.tick
            end
        end
        -- THE LEASH DANCE (the wiki's "running away until ... they lose
        -- aggression"): a guard keeps its target only while he ends a tick
        -- within LEASH of its post (RSProx: a raider 9 off a mage's post was
        -- shot for 40 ticks, cox.npc maxrange 8), and it shoots 2 ticks after
        -- my hit lands (its flinch). So I end a tick inside only on the tick
        -- before a cast and the cast's own; a cast from 8 or 9 lands 4 ticks
        -- later, and the flinch 2 after that reads a tick I ended outside, so
        -- it drops me instead of shooting. Closer than 8 the hit lands a tick
        -- sooner and the flinch reads the next cast's inside tick: never.
        local x0, z0, x1, z1 = CT.leash_box(S, kind)
        -- (the windows sit on a fixed phase -- the swing clock's once it
        -- runs: anchored on "the next tick" they slid away as fast as I
        -- walked to them and I never stepped in)
        local W = math.max(F.tick + 1, S.x2.swing_next or (F.tick + 1))
        if S.x2.swing_next ~= nil and S.x2.swing_next > F.tick then o.phase = S.x2.swing_next % V.SPEED end
        o.phase = o.phase or (F.tick % V.SPEED)
        local function inside_ok(tk)
            if tk < W - 1 then return false end
            local r = (tk - o.phase) % V.SPEED
            return r == 0 or r == V.SPEED - 1
        end
        local t = F.tick + 1
        while t <= F.tick + B.h and not o.tank do
            if inside_ok(t) then
                t = t + 1
            else
                local t1 = t
                while t1 + 1 <= F.tick + B.h and not inside_ok(t1 + 1) do t1 = t1 + 1 end
                B.hit({ name = "leash-" .. kind, x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = t, t1 = t1, forbid = true })
                t = t1 + 1
            end
        end
        B.hit({ name = "close-" .. kind, x0 = row.x - V.CLOSE, z0 = row.z - V.CLOSE, x1 = row.x + V.CLOSE,
                z1 = row.z + V.CLOSE, t0 = F.tick + 1, t1 = F.tick + B.h, forbid = true })
        local inside = o.tank or (F.me.x >= x0 and F.me.x <= x1 and F.me.z >= z0 and F.me.z <= z1)
        -- the protection only where it can reach me, or after it has
        if inside or (o.hit_at and F.tick - o.hit_at <= V.HIT_PRAY) then
            intents.pray = want_pray(QD.T2.PROTECT[style])
            o.prayed_ticks = (o.prayed_ticks or 0) + 1
        end
        if worn and o.style_ok then
            -- (tank: from 8, inside the leash even on its corner)
            B.target({ name = kind, npc = row, size = 1, range = o.tank and V.CLOSE + 1 or V.RANGE, speed = V.SPEED,
                       value = V.SWING_VALUE })
        end
        -- far off (the client's view of the far platform): towards it
        if CT.cheb(F.me.x, F.me.z, row.x, row.z) > V.RANGE + V.LEASH then
            B.pull({ name = "toward", x = row.x, z = row.z, size = 1, weight = 1, t0 = F.tick + 1, t1 = F.tick + B.h })
        end
        return intents
    end
    o.fighting = nil
    if not o.guards_seen then
        -- not in sight yet (the room's far side is past the client's npc
        -- range from the door): walk in before anything is decided; a
        -- crossing now would wake all four (tr1)
        intents.mode = "enter"
        B.pull({ name = "in", x = S.centre.x - 1, z = S.centre.z - 1, size = 3, weight = 1,
                 t0 = F.tick, t1 = F.tick + B.h })
        return intents
    end
    if o.barrier == nil then
        return CT.pickup(S, F, intents)
    end
    -- ROOM IN THE PACK for the keystone (picked up again at the end)
    if not o.held and o.taken_at_cross == nil and QD.T2.exec.free_slots(S) < V.FREE
        and F.tick - (o.dropped_at or -10) >= 2 then
        for _, id in ipairs(ids.droppable) do
            if QD.raid._tob_count(S, id) > 0 then
                intents.mode = "drop"
                intents.drop = id
                o.dropped[id] = true
                o.dropped_at = F.tick
                return intents
            end
        end
    end
    -- (tells: a ranger left for the crossing, prayed against)
    if #o.rangers > 0 then intents.pray = want_pray(QD.T2.PROTECT.ranged) end
    local near_key = o.key_loc ~= nil and CT.cheb(F.me.x, F.me.z, o.key_loc.x, o.key_loc.z) <= V.NEAR_KEY
    if not o.held and near_key then
        intents.mode = "take"
        intents.world_op = { kind = "loc", id = ids.key_loc, op = 1, x = o.key_loc.x, z = o.key_loc.z }
    elseif (not o.held) or near_key then
        local rope = CT.nearest_rope(S, F)
        assert(rope, S.who .. ": no rope end in the room")
        intents.mode = o.held and "cross-back" or "cross"
        intents.world_op = { kind = "loc", id = ids.rope, op = 1, x = rope.x, z = rope.z }
    else
        intents.mode = "dispel"
        intents.world_op = { kind = "loc", id = ids.barrier, op = 1, x = o.barrier.x, z = o.barrier.z }
    end
    return intents
end

-- The tightrope room I am in or beside: { name, variant, turn, ox, oz, level }.
-- THE ROOM from where I stand -> room, or nil and why. The room ahead is
-- looked up at ITS tile nearest me, as QD.COX.room_ahead does: the relay
-- leaves me on the room before's way-out edge (raid7: the small scavenger
-- room's), up to 32 tiles from this room's middle, and the middle can lie
-- outside the zones the client holds. In order: the cell I stand in; the
-- room ahead of it; each cell beside mine at its tile nearest me; a test's
-- ::coxtest centre.
function CT.find_room(centre)
    local tr, me = api_drive.player_tile()
    if tr ~= "ok" or me == nil then return nil, "no player tile (" .. tostring(tr) .. ")" end
    local here = QD.COX.room_at(me.x, me.z, me.level)
    if here and here.name == "tightrope" then return here end
    if here then
        local ahead = QD.COX.room_ahead(here)
        if ahead and ahead.name == "tightrope" then return ahead end
    end
    local best, bd = nil, nil
    local cx, cz = me.x - me.x % 32, me.z - me.z % 32
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local nx, nz = cx + 32 * d[1], cz + 32 * d[2]
        local px = math.max(nx, math.min(nx + 31, me.x))
        local pz = math.max(nz, math.min(nz + 31, me.z))
        local r = QD.COX.room_at(px, pz, me.level)
        if r and r.name == "tightrope" then
            local dist = math.max(math.abs(me.x - px), math.abs(me.z - pz))
            if bd == nil or dist < bd then best, bd = r, dist end
        end
    end
    if best then return best end
    if centre ~= nil then
        local r = QD.COX.room_at(centre.x, centre.z, me.level)
        if r and r.name == "tightrope" then return r end
    end
    return nil, string.format("no a tightrope room here or beside me (at %d,%d level %d, in %s)",
        me.x, me.z, me.level, here and here.name or "no room")
end

-- ================================================================ THE ROOM

QD.T2.register("cox_tightrope", {
    ids = function(opts) return CT.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_tightrope_solve"
        S.t2.plan_opts = { h = opts.h or CT.V.H, beam = opts.beam or CT.V.BEAM }
        -- THE ROOM: found from where I stand (the whole-raid contract,
        -- ROUND2_BRIEF.md): the cell I am in, else the one beside it (the
        -- room before's Passage leaves me on our shared edge); a test's
        -- ::coxtest centre is only a fallback.
        local room = opts.room
        assert(room, "cox_tightrope: init without a room (QD.raid.cox_tightrope_solve finds it)")
        S.room = room
        S.centre = { x = room.ox + 16, z = room.oz + 16 }
        S.o = { mages = {}, rangers = {}, ropes = {}, dropped = {} }
        -- TELLS ONLY (test/raids/_tightrope_tells.lua): `tank` stands in the
        -- guards' fire under the protection instead of the leash dance, and
        -- `cross_with` rangers are left alive for the crossing (they wake on
        -- the rope, their hits wait for the far end, the keystone kills them)
        S.o.tank = opts.tank == true
        S.o.cross_with = opts.cross_with or 0
    end,

    -- (the style: Longrange, pressed once the trident is worn, CT.strategy)
    enter = function(S) end,

    predict = function(S, F, B)
        CT.observe(S, F)
        B.prayer_factor.ranged = 0.33   -- T7
        B.prayer_factor.magic = 0.33
    end,

    strategy = function(S, F, B)
        local intents = CT.strategy(S, F, B)
        local o = S.o
        -- the hp lost from the first crossing on (the PASS row's zero)
        -- (hp lost to a hit: a drop at or under base -- Xeric's aid's
        -- overheal decays a point at a time above it, tr22)
        if intents.mode == "cross" and o.taken_at_cross == nil then o.taken_at_cross = 0 end
        if o.taken_at_cross and o.last_hp and F.hp < o.last_hp and o.last_hp <= (F.hp_base or 99) then
            o.cross_taken = (o.cross_taken or 0) + (o.last_hp - F.hp)
        end
        o.last_hp = F.hp
        return intents
    end,

    done = function(S, F)
        local o = S.o
        if o.barrier_last and o.barrier == nil and #o.mages == 0 and #o.rangers == 0 and o.picked then return "ok" end
        return nil
    end,

    summary = function(S)
        local o = S.o
        return string.format("tightrope: guards left %d mages %d rangers, barrier %s, keystone held %s, prayed %d ticks, last hit t%s",
            #o.mages, #o.rangers, o.barrier and "standing" or "gone", tostring(o.held), o.prayed_ticks or 0, tostring(o.hit_at))
    end,
})

-- The relay's entry: the room looked up BEFORE the room loop, and a room not
-- found is a failure the relay reports ("no_room", why), never an assert.
do
    local solve = QD.raid.cox_tightrope_solve
    QD.raid.cox_tightrope_solve = function(opts)
        opts = opts or {}
        local room, why = CT.find_room(opts.centre)
        if room == nil then return "no_room", "cox_tightrope_solve: " .. why, nil end
        local o2 = {}
        for k, v in pairs(opts) do o2[k] = v end
        o2.room = room
        return solve(o2)
    end
end

-- The room test's drained-arrival runs (test/raids/cox_tightrope.lua, run
-- names "trd<n>") -> true when this run is one.
function QD.raid.cox_tightrope_drained_run()
    local pr, players = api_drive.players()
    if pr ~= "ok" then return false end
    for _, p in ipairs(players) do
        if p.me then return string.match(tostring(p.name), "^trd") ~= nil end
    end
    return false
end
