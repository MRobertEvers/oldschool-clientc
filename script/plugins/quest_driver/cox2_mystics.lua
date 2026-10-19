-- quest-driver / cox2_mystics: THE SKELETAL MYSTICS, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/mystics_solver.md is the plan,
-- docs/minigames/cox/rooms/mystics_spec.md the mechanics. A QD.T2 room
-- (tob2_run.lua): PREDICT and STRATEGY here; the planner, the executive, the
-- monitor and the supplies are the shared layers.
--
-- THE IDEA (Synq 0:31:53, the wiki's Strategies, Tubby): ranged with the salve
-- amulet (ei) -- they are undead -- under Protect from Magic, ONE MYSTIC AT A
-- TIME: a mystic takes whoever it can see within its range, so every tile is
-- priced by the mystics that can see it (planner watchers: line of sight from
-- the 2x2's near edge, the same rule the content hunts with), the one I fight
-- cheaply and every other dearly; and never orthogonally beside one (its punch
-- is 25, through the magic prayer). The blowpipe's reach (5) is the target's.
--
-- Perception: npc rows (7604-7606, their seqs: 5528 cast, 5485 punch), the
-- casts' projectiles (130 fire blast, 109 vulnerability), the Mark of Power
-- (loc 29741) and the portals (49996-49998) in the loc rows.
-- ==========================================================================

QD.COXM = QD.COXM or {}

QD.COXM.V = {
    -- (a 14-tick horizon and a wider beam: the plan walks the maze to the
    -- fight tile itself)
    H = 14, BEAM = 48,
    MYSTIC = { [7604] = true, [7605] = true, [7606] = true }, SIZE = 2,
    -- (a swing is worth more than a tick of every caster on me: the fight
    -- ends sooner, and each kill takes a caster off -- at 6 against a
    -- waking price of 4 the plan never shot, my1)
    RANGE = 5, SPEED = 2, SWING_VALUE = 12,
    -- a cast's expected damage a tick: 4 ticks a cast, max 25 halved by the
    -- prayer, about half land (accuracy vs the d'hide's magic defence)
    -- (per TICK: a cast is every 4, so its 12 is 3 a tick for the worst
    -- case -- at 12 a tick, hp_worst read a cast a tick from every mystic and
    -- the plan fled every shot, my22. The "max" is 1.2 a tick, a bad run
    -- rather than every cast landing its max: at 3, ten ticks of three
    -- mystics read 90, hp_worst went negative under 90 hp and the RESCUE sipped
    -- Xeric's aid at 84-90 hp -- 88->107, 90->109, 89->108 -- half of every sip
    -- wasted over the cap, and the whole raid's doses ran out in this room,
    -- raid2 room 10. The supplies' floor (BREW_BELOW) does the steady brewing.)
    SEEN_EXP = 0.8, SEEN_MAX = 1.2, OTHER_EXP = 3,
    -- (the restore's gap under Ranged's base, measured on my0-3 as heal +
    -- restore sips: gap 8 -> 64 sips, 12 -> 65, 20 -> 78; a shallower drain
    -- costs restores but misses fewer shots, and the fight is the dear part)
    RESTORE_GAP = 8,
    -- a mystic not yet on me: coming within its hunt range wakes it (one more
    -- caster). RSProx (docs/rsprox-analysis/cox/mystics.md RM-W1): 7 from its
    -- south-west tile, and only if it sees me; the watcher has no range, so
    -- the price is a box (the sight is not priced: the box is the worst case)
    WAKE_EXP = 1.5, WAKE_MAX = 1.2, HUNT = 7,
    SIGHT = 15,
    EAT_BELOW = 60, BREW_BELOW = 60,
}

function QD.COXM.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_mystics", opts)
    local room = QD.raid._tob_symbols("cox_mystics", {
        { "cast", "seq", "skeleton_update_mage_casting_swansong" }, { "punch", "seq", "skeleton_update_attack_weapon" },
        { "mark", "loc", "raids_skeletalmystics_symbol" }, { "portal_end", "loc", "raids_mystics_portal_end" },
        { "pipe", "obj", "toxic_blowpipe_loaded" }, { "salve", "obj", "nzone_salve_amulet_e" },
        { "assembler", "obj", "avas_assembler" }, { "dhide_body", "obj", "black_dragonhide_body" },
        { "chaps", "obj", "black_dragonhide_chaps" },
        { "com_mode", "varp", "varp43_com_mode" }, { "style_slot_1", "component", "combat_interface:style_slot_1" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_mystics: no combat tab")
    ids.combat_tab = combat_tab
    -- THE SUPPLY FAMILIES (cox2_raid.lua QD.COX.SUPPLY, best first): heal =
    -- Xeric's aid then Saradomin brew (both drain Ranged: a restore pairs
    -- with them, QD.COXM.restore_due), restore = revitalisation then super
    -- restore (every stat and Prayer), overload. The prayer ENHANCES are the
    -- prayer family's three raid tiers (its first twelve): a point a prayer
    -- tick for 458 ticks, timed as such; a prayer potion is not one.
    ids.brews = QD.COX.supply_ids("heal")
    ids.restores = QD.COX.supply_ids("restore")
    ids.stat_restores = ids.restores
    ids.overloads = QD.COX.supply_ids("overload")
    ids.enhances = {}
    for i, id in ipairs(QD.COX.supply_ids("prayer")) do
        if i <= 12 then ids.enhances[#ids.enhances + 1] = id end
    end
    ids.combats, ids.rangings = {}, {}
    -- the ranged set: the blowpipe is two-handed
    ids.set = { { ids.pipe, true }, { ids.salve, false }, { ids.assembler, false }, { ids.dhide_body, false },
                { ids.chaps, false } }
    return ids
end

function QD.COXM.cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- Chebyshev gap from a tile to a 2x2 footprint.
function QD.COXM.gap(row, x, z)
    local n = QD.COXM.V.SIZE
    local dx = (x < row.x) and (row.x - x) or ((x > row.x + n - 1) and (x - (row.x + n - 1)) or 0)
    local dz = (z < row.z) and (row.z - z) or ((z > row.z + n - 1) and (z - (row.z + n - 1)) or 0)
    return math.max(dx, dz)
end

-- ================================================================ OBSERVE

function QD.COXM.observe(S, F)
    local m, V, ids = S.m, QD.COXM.V, S.ids
    m.rows = {}
    for _, row in ipairs(F.npcs) do
        if V.MYSTIC[row.npc_id] then
            local key = QD.T2.npc_key(row)
            m.rows[#m.rows + 1] = row
            local st = m.st[key]
            if st == nil then
                st = { seq_tick = nil, awake = false, casts = 0, punches = 0, first = F.tick, x = row.x, z = row.z }
                m.st[key] = st
                m.seen_n = m.seen_n + 1
            end
            row.st = st
            if row.seq_tick ~= nil and row.seq_tick ~= st.seq_tick then
                st.seq_tick = row.seq_tick
                if row.seq_id == ids.cast then
                    st.casts = st.casts + 1
                    st.awake = true
                    m.casts = m.casts + 1
                elseif row.seq_id == ids.punch then
                    st.punches = st.punches + 1
                    st.awake = true
                    m.punches = m.punches + 1
                end
            end
            -- (a step is NOT a target: an idle mystic wanders, up to 7 round
            -- its spawn, RSProx RM-S1. Read as "awake and walking at me" the
            -- trap held a tile for 1150 ticks against a wanderer, myst1. It is
            -- awake once it casts or punches; a step after that is its melee
            -- walk.)
            if row.x ~= st.x or row.z ~= st.z then
                st.x, st.z = row.x, row.z
                if st.awake then st.moved_at = F.tick end
            end
        end
    end
    -- the exit: the Mark of Power, and the portals when it goes
    local lr, locs = api_drive.locs(40)
    m.mark_seen, m.portal_seen = false, false
    if lr == "ok" then
        for _, l in ipairs(locs) do
            if l.loc_id == ids.mark or l.resolved_loc_id == ids.mark then m.mark_seen = true end
            if l.loc_id == ids.portal_end or l.resolved_loc_id == ids.portal_end then m.portal_seen = true end
        end
    end
    if m.mark_seen then m.mark_ever = true end
end

-- ================================================================ PREDICT

-- Every mystic that can see a tile is a caster on it: the awake ones at their
-- cast's expected cost, a sleeping one at the price of waking it. Beside any
-- of them: forbidden (the punch).
function QD.COXM.predict(S, F, B)
    local m, V = S.m, QD.COXM.V
    local d = F.tick
    for _, row in ipairs(m.rows) do
        if QD.COXM.cheb(row.x, row.z, F.me.x, F.me.z) <= V.SIGHT + B.h then
            local awake = row.st.awake
            -- (one caster at a time: the one I shoot sees me anyway; every
            -- OTHER that sees me is a caster the shots do not pay for -- priced
            -- at the same 0.8, the plan stood where all three saw it for a
            -- hundred ticks, raid2 room 10)
            if awake then
                local exp = (QD.T2.npc_key(row) == m.target) and V.SEEN_EXP or V.OTHER_EXP
                B.watcher({ name = "cast_" .. row.npc_id, x = row.x, z = row.z, size = V.SIZE,
                            t0 = d + 1, t1 = d + B.h, dmg_exp = exp, dmg_max = V.SEEN_MAX })
            else
                B.hit({ name = "wake_" .. row.npc_id, x0 = row.x - V.HUNT, z0 = row.z - V.HUNT, x1 = row.x + V.HUNT,
                        z1 = row.z + V.HUNT, t0 = d + 1, t1 = d + B.h, dmg_exp = V.WAKE_EXP, dmg_max = V.WAKE_MAX })
            end
        end
        -- (a mystic that stepped last tick is walking at me -- melee style --
        -- and steps again before it can swing: a tile more of room, or it
        -- arrives beside me and punches on the next read; my1)
        local r = (row.st.moved_at ~= nil and row.st.moved_at >= d - 1) and 2 or 1
        if m.trap ~= nil and m.trap.key == QD.T2.npc_key(row) then
            -- (the trapped one: only its punch tiles -- the four sides, not
            -- the corners; the trap tile is a corner's diagonal)
            B.hit({ name = "punch_" .. row.npc_id, x0 = row.x, z0 = row.z - 1, x1 = row.x + V.SIZE - 1,
                    z1 = row.z + V.SIZE, t0 = d + 1, t1 = d + B.h, forbid = true, dmg_exp = 12, dmg_max = 25 })
            B.hit({ name = "punch_" .. row.npc_id, x0 = row.x - 1, z0 = row.z, x1 = row.x + V.SIZE,
                    z1 = row.z + V.SIZE - 1, t0 = d + 1, t1 = d + B.h, forbid = true, dmg_exp = 12, dmg_max = 25 })
        else
            B.hit({ name = "punch_" .. row.npc_id, x0 = row.x - r, z0 = row.z - r, x1 = row.x + V.SIZE - 1 + r,
                    z1 = row.z + V.SIZE - 1 + r, t0 = d + 1, t1 = d + B.h, forbid = true, dmg_exp = 12, dmg_max = 25 })
        end
    end
end

-- ================================================================ STRATEGY

-- The mystic to fight: an awake one first (fewest hitpoints), else the
-- nearest.
function QD.COXM.pick(S, F)
    local m = S.m
    local best, bkey = nil, nil
    for _, row in ipairs(m.rows) do
        local hp = row.health_ratio or 30
        -- (one within three tiles first: it is the one about to reach me, and
        -- going past it for another walked me beside it, my13/my22)
        local close = QD.COXM.gap(row, F.me.x, F.me.z) <= 3 and 0 or 10000
        local k = close + (row.st.awake and 0 or 1000) + hp * 10 + QD.COXM.cheb(row.x, row.z, F.me.x, F.me.z)
        if bkey == nil or k < bkey then best, bkey = row, k end
    end
    return best
end

function QD.COXM.gear(S)
    local out = {}
    for _, e in ipairs(S.ids.set) do
        if not QD.raid._tob_worn(S, e[1]) and QD.raid._tob_count(S, e[1]) > 0 then
            out[#out + 1] = { obj = e[1], two_hand = e[2] }
        end
    end
    return out
end

function QD.COXM.strategy(S, F, B)
    local m, V, ids = S.m, QD.COXM.V, S.ids
    local d = F.tick
    local intents = { mode = "idle", eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    local gear = QD.COXM.gear(S)
    if #gear > 0 then intents.gear = gear end
    -- the overhead and the boost from the start: they see me on the way in
    if not F.lit["protectfrommagic"] and (F.prayer or 0) > 0 then intents.pray = "protectfrommagic" end
    intents.pray_boost = ((F.prayer or 0) >= 20) and "rigour" or nil
    -- (Rigour pressed here once the overhead is up, at any points: the
    -- supplies' own boost waits for 40, and the whole raid sent me in at 33
    -- -- the fight ran on Protect from Magic alone, raid2 room 10)
    if intents.pray == nil and F.lit["protectfrommagic"] and not F.lit["rigour"] and (F.prayer or 0) > 0
        and (m.rigour_pressed == nil or d - m.rigour_pressed > 6) then
        intents.pray = "rigour"
        m.rigour_pressed = d
    end
    QD.COXM.drinks(S, F, intents)
    local row = QD.COXM.pick(S, F)
    -- AN ADD (something that followed me in from the room before, the whole
    -- raid's first probe): within three tiles, it is shot first
    for _, r in ipairs(F.npcs) do
        if not V.MYSTIC[r.npc_id] and (r.health_ratio == nil or r.health_ratio > 0)
            and QD.COXM.cheb(r.x, r.z, F.me.x, F.me.z) <= 3 then
            if m.add_key ~= QD.T2.npc_key(r) then
                m.add_key = QD.T2.npc_key(r)
                m.adds = (m.adds or 0) + 1
            end
            if QD.raid._tob_worn(S, ids.pipe) then
                B.target({ name = "add", npc = r, size = r.size or 1, range = V.RANGE, speed = V.SPEED, value = V.SWING_VALUE * 2 })
            end
        end
    end
    if row == nil then
        -- none in sight: on through the room (its middle), unless it is done
        if m.centre and not m.portal_seen then
            B.pull({ name = "centre", x = m.centre.x - 1, z = m.centre.z - 1, size = 3, weight = 1, t0 = d + 1, t1 = d + B.h })
            intents.mode = "search"
        else
            intents.mode = "clear"
        end
        return intents
    end
    m.target = QD.T2.npc_key(row)
    if QD.raid._tob_worn(S, ids.pipe) then
        B.target({ name = "mystic", npc = row, size = V.SIZE, range = V.RANGE, speed = V.SPEED, value = V.SWING_VALUE })
        -- THE CLICK (what a player does: click the mystic; the server walks
        -- the maze to a tile in reach that sees it and keeps shooting) unless
        -- a mystic is close: beside one is its punch (25, through the magic
        -- prayer), and one walking at me (melee style) steps again before I
        -- would see it arrive. Then the plan moves me off (its forbids) and
        -- the click comes back when it is clear. (The plan alone -- the
        -- target in the spec, the watchers pricing every tile a mystic sees --
        -- found no shot in the maze for hundreds of ticks: my1, my3, my5.)
        local danger, near = false, false
        for _, other in ipairs(m.rows) do
            local gap = QD.COXM.gap(other, F.me.x, F.me.z)
            local walking = other.st.moved_at ~= nil and other.st.moved_at >= d - 1
            if gap <= 1 or (walking and gap <= 3) then danger = true end
            -- (the click runs two tiles a tick on a route I do not see: from
            -- three tiles of a mystic its next step can end beside it, my22
            -- t63-65 and my13 t63-65, going for a different one)
            if gap <= 3 then near = true end
        end
        -- The click is LATCHED: started from out of the target's reach, it
        -- holds until I swing or a mystic is close. (Toggled tick by tick
        -- against the plan it shot 3 times in 300 ticks, my1/my5/my10.)
        local gap_t = QD.COXM.gap(row, F.me.x, F.me.z)
        -- THE CORNER TRAP (Synq 0:32:28-0:33:39, the wiki's Strategies,
        -- Zulerah's luring guide, Extile): a mystic that has turned to melee
        -- walks at me naively; a tile it cannot reach because an obstacle
        -- stops its step -- diagonal to its corner, inside its reach box so it
        -- stays melee -- is a tile it never attacks from, and I shoot it there.
        QD.COXM.trap(S, F, row)
        -- THE FIGHT TILE (Tubby: "try to attack one mystic without aggroing
        -- the other two"; the wiki: "Have one mystic attack the team at a
        -- time"): a tile in the blowpipe's reach that the target sees and as
        -- few others as the room allows, reached by the client's route. The
        -- click (below) walked the server's route through every mystic's
        -- sight: prep raid2 room 10, 141 casts at me in 330 ticks.
        if m.trap == nil then QD.COXM.fight_tile(S, F, row) end
        if m.trap == nil and m.ftile ~= nil then
            m.click = nil
            B.pull({ name = "fight", x = m.ftile.x, z = m.ftile.z, size = 1, weight = 3, t0 = d + 1, t1 = d + B.h })
            intents.mode = "fighttile"
        end
        if m.trap ~= nil then
            m.click = nil
            B.pull({ name = "trap", x = m.trap.x, z = m.trap.z, size = 1, weight = 3, t0 = d + 1, t1 = d + B.h })
            intents.mode = "trap"
        end
        if m.click ~= nil and (m.click ~= m.target or danger or near or F.my_swing) then m.click = nil end
        if m.click == nil and m.trap == nil and m.ftile == nil and not danger and not near and gap_t > V.RANGE then m.click = m.target end
        if m.click ~= nil then
            intents.world_op = { kind = "npc", id = row.npc_id, op = 2, element = row.element_id }
            intents.mode = row.st.awake and "fight" or "pull"
        elseif m.trap == nil and m.ftile == nil then
            intents.mode = danger and "clear" or "shoot"
            if danger then m.dangers = (m.dangers or 0) + 1 end
        end
    else
        intents.mode = "gear"
    end
    return intents
end

-- The trap tile for `row`, kept in m.trap = { key, x, z, at = {x, z} } while
-- it holds: `row` walked within the last 3 ticks (it is melee: magic stands
-- and casts), and a tile T within 6 of me that (1) I reach by a straight
-- walk, (2) the mystic's naive walk at T stops short of (api_drive.npc_stall:
-- an obstacle on the diagonal, the X leg and the Z leg) at P, (3) with T
-- diagonal to P's footprint or two off it -- never on a side (its punch) --
-- and inside the two-tile box its melee reach check reads (so it stays melee
-- instead of turning back to magic after eight ticks), (4) within the
-- blowpipe's reach of P. The nearest such T to me.
function QD.COXM.trap(S, F, row)
    local m, V = S.m, QD.COXM.V
    local d = F.tick
    local key = QD.T2.npc_key(row)
    local walking = row.st.moved_at ~= nil and row.st.moved_at >= d - 3
    if m.trap ~= nil and m.trap.key ~= key then m.trap = nil end
    if m.trap ~= nil then
        -- held while the mystic is where the trap stops it (or still on its way)
        local r, st = api_drive.npc_stall(row.x, row.z, V.SIZE, m.trap.x, m.trap.z, 12)
        if r ~= "ok" or not st.stalled or st.x ~= m.trap.at.x or st.z ~= m.trap.at.z then m.trap = nil end
        if m.trap ~= nil then return end
    end
    if not walking or api_drive.npc_stall == nil then return end
    local best, bd = nil, nil
    local dbg = { stalled = 0, gapok = 0, inbox = 0, reach = 0, side = 0 }
    local cands = {}
    for dx = -6, 6 do
        for dz = -6, 6 do
            local tx, tz = F.me.x + dx, F.me.z + dz
            local r, st = api_drive.npc_stall(row.x, row.z, V.SIZE, tx, tz, 12)
            if r == "ok" and st.stalled then
                dbg.stalled = dbg.stalled + 1
                local p = { x = st.x, z = st.z }
                local gap = QD.COXM.gap(p, tx, tz)
                local side = (gap == 1) and QD.COXM.beside2(p, tx, tz)
                local inbox = tx >= p.x - 2 and tx <= p.x + 2 and tz >= p.z - 2 and tz <= p.z + 2
                if gap >= 1 and gap <= V.RANGE then dbg.gapok = dbg.gapok + 1 end
                if inbox then dbg.inbox = dbg.inbox + 1 end
                if side then dbg.side = dbg.side + 1 end
                if gap >= 1 and gap <= V.RANGE and not side and inbox then
                    cands[#cands + 1] = { key = key, x = tx, z = tz, at = p, dist = QD.COXM.cheb(tx, tz, F.me.x, F.me.z) }
                end
            end
        end
    end
    -- (the nearest few the client's route reaches exactly: a tile in a rock
    -- or behind a wall is no trap tile)
    table.sort(cands, function(a, b) return a.dist < b.dist end)
    for i = 1, math.min(#cands, 6) do
        local c = cands[i]
        local rr, route = api_drive.route(c.x, c.z)
        local a = route and route.arrive
        if rr == "ok" and a and (a.x or a[1]) == c.x and (a.z or a[2]) == c.z then
            best = c
            break
        end
        dbg.reach = dbg.reach + 1
    end
    if best ~= nil then
        m.trap = best
        m.traps = (m.traps or 0) + 1
    end
    if (m.trap_dbg or -100) < d - 15 then
        m.trap_dbg = d
        QD.raid._tob_trace(S, d, string.format("trap search at %d,%d for %d,%d: stalled %d gap %d inbox %d side %d cands %d unreached %d -> %s",
            F.me.x, F.me.z, row.x, row.z, dbg.stalled, dbg.gapok, dbg.inbox, dbg.side, #cands, dbg.reach, best and (best.x .. "," .. best.z) or "none"))
    end
end

-- The fight tile for `row`, kept in m.ftile = { key, x, z, seen } and
-- re-chosen when the target changes or moves, or every 8 ticks: within the
-- blowpipe's reach (gap 2..5: never in its punch's reach), seen by the
-- target (api_drive.npc_sees, the planner's own watcher rule), seen by the
-- fewest other mystics within 16, not within 2 of any other, then the
-- nearest; the client's route must reach it exactly.
function QD.COXM.fight_tile(S, F, row)
    local m, V = S.m, QD.COXM.V
    local d = F.tick
    if api_drive.npc_sees == nil then return end
    local key = QD.T2.npc_key(row)
    local f = m.ftile
    if f ~= nil and f.key == key and f.rx == row.x and f.rz == row.z and d - f.t < 8 then return end
    m.ftile = nil
    local cands = {}
    for dx = -7, 8 do
        for dz = -7, 8 do
            local tx, tz = row.x + dx, row.z + dz
            local gap = QD.COXM.gap(row, tx, tz)
            if gap >= 2 and gap <= V.RANGE then
                local r, sees = api_drive.npc_sees(row.x, row.z, V.SIZE, tx, tz)
                if r == "ok" and sees then
                    local seen, close = 0, false
                    for _, o in ipairs(m.rows) do
                        if QD.T2.npc_key(o) ~= key then
                            if QD.COXM.gap(o, tx, tz) <= 2 then close = true end
                            if QD.COXM.cheb(o.x, o.z, tx, tz) <= 16 then
                                local r2, s2 = api_drive.npc_sees(o.x, o.z, V.SIZE, tx, tz)
                                if r2 == "ok" and s2 then seen = seen + 1 end
                            end
                        end
                    end
                    if not close then
                        cands[#cands + 1] = { x = tx, z = tz, seen = seen,
                                              score = seen * 100 + QD.COXM.cheb(tx, tz, F.me.x, F.me.z) }
                    end
                end
            end
        end
    end
    table.sort(cands, function(a, b) return a.score < b.score end)
    for i = 1, math.min(#cands, 6) do
        local c = cands[i]
        local rr, route = api_drive.route(c.x, c.z)
        local a = route and route.arrive
        if rr == "ok" and a and (a.x or a[1]) == c.x and (a.z or a[2]) == c.z then
            m.ftile = { key = key, x = c.x, z = c.z, seen = c.seen, rx = row.x, rz = row.z, t = d }
            m.ftiles = (m.ftiles or 0) + 1
            return
        end
    end
end

-- Orthogonally beside a 2x2 at p (its punch tiles).
function QD.COXM.beside2(p, x, z)
    local n = QD.COXM.V.SIZE
    local dx, dz = x - p.x, z - p.z
    if dx >= 0 and dx < n and (dz == -1 or dz == n) then return true end
    if dz >= 0 and dz < n and (dx == -1 or dx == n) then return true end
    return false
end

-- The overload (+) once at the start (five hits of 10 as it boosts), at 85+.
function QD.COXM.drinks(S, F, intents)
    local m, ids = S.m, S.ids
    local n = 0
    for i, obj in ipairs(ids.overloads) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    if m.ovl_n ~= nil and n < m.ovl_n then m.ovl_until = F.tick + 501 end
    m.ovl_n = n
    local e = 0
    for i, obj in ipairs(ids.enhances) do e = e + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    if m.penh_n ~= nil and e < m.penh_n then m.penh_until = F.tick + 458 end
    m.penh_n = e
    -- THE OVERLOAD, by what I can see: a Ranged over its base is one still
    -- running (the whole raid sends me in mid-overload, or after it ended,
    -- with whatever the rooms before left; a room test starts fresh). Due
    -- when Ranged is not boosted, none I drank is running and a dose is left:
    -- the brews top me to 88 first (its five hits of 10), then the sip at 85+.
    -- (raid2 room 10: in at 69 hp with it ended, the old rule waited for 85
    -- that the floor of 60 never reached, and 74 swings landed 18.)
    local lv = F.levels and F.levels.ranged
    local boosted = lv ~= nil and lv.level > lv.base
    m.ovl_due = (not boosted) and (m.ovl_until or -1) <= F.tick and n > 0
    -- (the topping only before a mystic is on me: in a fight, brewing to 88
    -- every three ticks for an overload the hits never let it reach drank 20
    -- sips of Xeric's aid in 150 ticks and drained Ranged to a 23% hit rate,
    -- raid7 room 4 -- 399 taken. Once fighting, the sip waits for 85+ and
    -- the drained Ranged gets its revitalisation.)
    local fighting = false
    for _, row in ipairs(m.rows) do if row.st.awake then fighting = true end end
    local topping = m.ovl_due and not fighting
    if topping then
        intents.brew_below, intents.eat_below = 88, 88
    else
        -- (the shared floor: dropped once no restore is held, QD.COX.brew_floor)
        local floor = QD.COX.brew_floor(S, intents.brew_below or QD.COXM.V.BREW_BELOW)
        intents.brew_below, intents.eat_below = floor, floor
    end
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    local pick = nil
    if m.ovl_due and F.hp >= 85 then
        pick = QD.raid._tob_first(S, ids.overloads)
    end
    -- THE PRAYER: Rigour is 20% of every shot's accuracy, and the supplies
    -- press it only at 40+ prayer points. The whole raid sends me in low
    -- (raid2 room 10: 33 points, Protect from Magic alone, 13 of 70 shots
    -- landed): a prayer enhance (+) -- a point a prayer tick for 458 ticks --
    -- under 85, as the Olm solver drinks it.
    if pick == nil and (m.penh_until or -1) < F.tick and (F.prayer or 0) < 85 then
        pick = QD.raid._tob_first(S, ids.enhances)
    end
    if pick then
        intents.drink = pick
        S.last_drink = F.tick
        return
    end
    -- THE RESTORE, the raid's shared rule (QD.COX.restore_intent): only once
    -- Ranged is 20 under its base and hp is over the heal floor. (The room's
    -- own rule -- 5 under, or under at all between overload re-boosts, and a
    -- revitalisation for Prayer under 20 -- spent the restores the whole raid
    -- runs short of.)
    -- (QD.COX.restore_intent's own steps, with this room's gap:
    -- restore_intent takes none, restore_due does)
    if not topping and F.hp > (intents.brew_below or 45)
        and QD.COX.restore_due(F, { "ranged" }, m.restore_gap) then
        local pick2 = QD.raid._tob_first(S, ids.stat_restores)
        if pick2 then
            intents.drink = pick2
            S.last_drink = F.tick
            m.restores = (m.restores or 0) + 1
        end
    end
end

-- Xeric's aid drains Ranged with every sip (cox_potion.rs2: 10% + 2 of the
-- level, like a brew), and a drained Ranged is a blowpipe that misses: the
-- first probe swung 90 times and landed 15 (my2). A revitalisation (+) when
-- Ranged is deep under its base, or under it with the overload's next
-- re-boost (each 25 ticks from the sip) more than 8 ticks off -- the Olm
-- solver's rule (CO.restore_due).
function QD.COXM.restore_due(S, F)
    local m = S.m
    local lv = F.levels and F.levels.ranged
    if lv == nil or lv.level >= lv.base then return false end
    if lv.level < lv.base - 5 then return true end
    if m.ovl_until == nil or m.ovl_until < F.tick then return true end
    local drunk = m.ovl_until - 500
    local next_boost = drunk + 25 * ((F.tick - drunk) // 25 + 1)
    return next_boost - F.tick > 8
end

-- ================================================================ THE ROOM

QD.T2.register("cox_mystics", {
    ids = function(opts) return QD.COXM.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_mystics_solve"
        S.max_ticks = opts.max_ticks or 1500
        S.t2.plan_opts = { h = opts.h or QD.COXM.V.H, beam = opts.beam or QD.COXM.V.BEAM }
        S.m = { st = {}, rows = {}, seen_n = 0, casts = 0, punches = 0, target = nil, stalled = 0,
                mark_seen = false, mark_ever = false, portal_seen = false }
        -- (the restore's gap under Ranged's base: opts for a sweep, else the
        -- room's measured best)
        S.m.restore_gap = opts.restore_gap or QD.COXM.V.RESTORE_GAP
        -- the room from where I stand: this one, or the one ahead (the whole
        -- raid passes nothing; a room test's ::coxtest centre is a fallback)
        local here = QD.COX.here()
        local room = nil
        if here and here.name == "mystics" then
            room = here
        elseif here then
            local _, _, cx, cz = QD.COX.next_origin(here)
            local ahead = QD.COX.room_at(cx, cz, here.level)
            if ahead and ahead.name == "mystics" then room = ahead end
        end
        if room then
            S.m.centre = { x = room.ox + 16, z = room.oz + 16 }
        elseif opts.centre then
            S.m.centre = { x = opts.centre.cx, z = opts.centre.cz }
        end
        local kr, ks = api_drive.symbol("seq", "human_killerwatt_electricshock")
        assert(kr == "ok", "cox_mystics: no seq human_killerwatt_electricshock")
        S.mon.not_swing[ks] = true
    end,

    -- Rapid (style slot 1, com_mode 1) for the blowpipe, before the loop; it
    -- survives the wields.
    enter = function(S)
        local ids = S.ids
        -- THE ARRIVAL, logged first: the room's cost is read against it
        do
            local function sk(id)
                local r, st = api_drive.skill(id)
                if r ~= "ok" then return "?" end
                return tostring(st.level) .. "/" .. tostring(st.base_level)
            end
            local function doses(list)
                local n = 0
                for i, obj in ipairs(list) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
                return n
            end
            S.m.arrival = string.format("arrival hp %s prayer %s ranged %s; doses heal %d restore %d overload %d prayer %d",
                sk(ids.hitpoints), sk(ids.prayer), sk(ids.ranged), doses(ids.brews), doses(ids.restores),
                doses(ids.overloads), doses(ids.enhances))
            QD.raid._tob_trace(S, api_drive.tick(), S.m.arrival)
        end
        for press = 1, 4 do
            local mr, mode = api_drive.varp(ids.com_mode)
            assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
            if mode == 1 then break end
            assert(press <= 3, S.who .. ": three style presses and com_mode reads " .. tostring(mode))
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
        QD.COXM.observe(S, F)
        QD.COXM.predict(S, F, B)
    end,

    strategy = function(S, F, B)
        local intents = QD.COXM.strategy(S, F, B)
        S.t2.want.boost = intents.pray_boost
        intents.pray_boost = nil
        return intents
    end,

    -- done: every mystic down, the Mark of Power gone and the portals up
    done = function(S, F)
        local m = S.m
        if #m.rows == 0 and m.seen_n > 0 and m.mark_ever and not m.mark_seen and m.portal_seen then return "ok" end
        return nil
    end,

    summary = function(S)
        local m = S.m
        return string.format(tostring(m.arrival) .. "; mystics: fight tiles " .. tostring(m.ftiles or 0) .. ", traps %d, adds %d, seen %d, casts %d, punches %d, restores %d, mark seen %s now %s, portals %s",
            m.traps or 0, m.adds or 0, m.seen_n, m.casts, m.punches, m.restores or 0, tostring(m.mark_ever), tostring(m.mark_seen), tostring(m.portal_seen))
    end,
})
