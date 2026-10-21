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
    -- the price is its WAKE MASK (QD.COXM.mask: the tiles within WAKE_R it
    -- sees), dear enough that the plan walks round it (round 5: 1.5 a tick
    -- over a box let the walks wake the others in 8 of 9 relay losses; at 6,
    -- under a swing's 12, the plan stood and shot while a wanderer's mask
    -- came over me, w1 raid29 t512)
    WAKE_EXP = 25, WAKE_MAX = 1.2, HUNT = 7, WAKE_R = 8,
    -- the prayers go up within PRAY_NEAR of a sleeper's hunt; NO_SWING ticks
    -- with the target awake and no swing of mine hand over to the click
    PRAY_NEAR = 3, NO_SWING = 12,
    SIGHT = 15,
    EAT_BELOW = 60, BREW_BELOW = 60,
}

function QD.COXM.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_mystics", opts)
    local room = QD.raid._tob_symbols("cox_mystics", {
        { "cast", "seq", "skeleton_update_mage_casting_swansong" }, { "punch", "seq", "skeleton_update_attack_weapon" },
        { "mark", "loc", "raids_skeletalmystics_symbol" }, { "portal_end", "loc", "raids_mystics_portal_end" },
        { "pipe", "obj", "toxic_blowpipe_loaded" }, { "salve", "obj", "nzone_salve_amulet_e" },
        { "assembler", "obj", QD.COX.ava_name() }, { "dhide_body", "obj", "black_dragonhide_body" },
        { "chaps", "obj", "black_dragonhide_chaps" },
        { "vial_empty", "obj", "raids_vial_empty" },
        { "com_mode", "varp", "varp43_com_mode" }, { "damagestyle", "varp", "varp6969_damagestyle" }, { "style_slot_1", "component", "combat_interface:style_slot_1" },
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
    -- a full pack lends one of these to the floor for the blowpipe's wield
    ids.spares = QD.COX.spare_ids()
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
                -- (a swing re-rolls its style, so a step after the last
                -- swing is what says melee: st.walk_at, cleared by a swing)
                if row.seq_id == ids.cast then
                    st.casts = st.casts + 1
                    st.awake = true
                    st.awake_at = st.awake_at or F.tick
                    st.last_cast = F.tick
                    st.walk_at = nil
                    m.casts = m.casts + 1
                elseif row.seq_id == ids.punch then
                    st.punches = st.punches + 1
                    st.awake = true
                    st.awake_at = st.awake_at or F.tick
                    st.walk_at = nil
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
                if st.awake then
                    st.moved_at = F.tick
                    st.walk_at = F.tick
                    st.walked = true
                end
            end
        end
    end
    -- the exit: the Mark of Power, and the portals when it goes
    local lr, locs = api_drive.locs(40)
    m.mark_seen, m.portal_seen = false, false
    if lr == "ok" then
        for _, l in ipairs(locs) do
            if l.loc_id == ids.mark or l.resolved_loc_id == ids.mark then
                m.mark_seen = true
                m.mark_at = { x = l.x, z = l.z }
            end
            if l.loc_id == ids.portal_end or l.resolved_loc_id == ids.portal_end then m.portal_seen = true end
        end
    end
    if m.mark_seen then m.mark_ever = true end
end

-- ================================================================ PREDICT

-- THE WAKE MASK of a sleeping mystic (RSProx RM-W1: it opens on a raider
-- within 7 of its south-west tile that it can see; one tile more for its
-- wander): { cells = { [x * 65536 + z] = true }, rects = { x0, z0, x1, z1 } },
-- kept while it stands still. The planner's box alone (round 2-4) priced
-- tiles behind a wall it never sees, and the fight-tile search did not read
-- it at all: in 8 of the 9 relay losses (sweep r29) the walk to one mystic
-- woke the other two within 1-9 ticks.
function QD.COXM.mask(row)
    local st, V = row.st, QD.COXM.V
    if st.mask ~= nil and st.mask_x == row.x and st.mask_z == row.z then return st.mask end
    local R = V.WAKE_R
    local cells, rects, open = {}, {}, {}
    for z = row.z - R, row.z + R do
        local runs, x0 = {}, nil
        for x = row.x - R, row.x + R + 1 do
            local inside = false
            if x <= row.x + R and QD.COXM.gap(row, x, z) >= 1 then
                local r, sees = api_drive.npc_sees(row.x, row.z, V.SIZE, x, z)
                inside = r ~= "ok" or sees
            end
            if inside then
                cells[x * 65536 + z] = true
                if x0 == nil then x0 = x end
            elseif x0 ~= nil then
                runs[#runs + 1] = { x0 = x0, x1 = x - 1 }
                x0 = nil
            end
        end
        -- (a run the row below had too grows that rectangle)
        local still = {}
        for _, run in ipairs(runs) do
            local k = run.x0 * 65536 + run.x1
            local rc = open[k]
            if rc ~= nil then
                rc.z1 = z
            else
                rc = { x0 = run.x0, x1 = run.x1, z0 = z, z1 = z }
                rects[#rects + 1] = rc
            end
            still[k] = rc
        end
        open = still
    end
    st.mask, st.mask_x, st.mask_z = { cells = cells, rects = rects }, row.x, row.z
    return st.mask
end

-- Every awake mystic that can see a tile is a caster on it; a sleeping one
-- prices its wake mask (but the one I mean to open on); beside any of them:
-- forbidden (the punch).
function QD.COXM.predict(S, F, B)
    local m, V = S.m, QD.COXM.V
    local d = F.tick
    for _, row in ipairs(m.rows) do
        local key = QD.T2.npc_key(row)
        local c = QD.COXM.cheb(row.x, row.z, F.me.x, F.me.z)
        if row.st.awake then
            if c <= V.SIGHT + B.h then
                -- (one caster at a time: the one I shoot sees me anyway;
                -- every OTHER that sees me is a caster the shots do not pay
                -- for -- priced at the same 0.8, the plan stood where all
                -- three saw it for a hundred ticks, raid2 room 10)
                local exp = (key == m.target) and V.SEEN_EXP or V.OTHER_EXP
                B.watcher({ name = "cast_" .. row.npc_id, x = row.x, z = row.z, size = V.SIZE,
                            t0 = d + 1, t1 = d + B.h, dmg_exp = exp, dmg_max = V.SEEN_MAX })
            end
        elseif key ~= m.target and c <= V.WAKE_R + B.h then
            for _, rc in ipairs(QD.COXM.mask(row).rects) do
                B.hit({ name = "wake_" .. row.npc_id, x0 = rc.x0, z0 = rc.z0, x1 = rc.x1, z1 = rc.z1,
                        t0 = d + 1, t1 = d + B.h, dmg_exp = V.WAKE_EXP, dmg_max = V.WAKE_MAX })
            end
        end
        -- (a mystic that stepped last tick is walking at me -- melee style --
        -- and steps again before it can swing: a tile more of room, or it
        -- arrives beside me and punches on the next read; my1)
        local r = (row.st.moved_at ~= nil and row.st.moved_at >= d - 1) and 2 or 1
        B.hit({ name = "punch_" .. row.npc_id, x0 = row.x - r, z0 = row.z - r, x1 = row.x + V.SIZE - 1 + r,
                z1 = row.z + V.SIZE - 1 + r, t0 = d + 1, t1 = d + B.h, forbid = true, dmg_exp = 12, dmg_max = 25 })
    end
end

-- ================================================================ STRATEGY

-- The fight tiles for `row`, cheapest first: in the blowpipe's reach (gap
-- 2..5: never its punch's), a tile it sees, priced by the sleepers it wakes
-- (1000 each), the other awake mystics that see it (300 each), a mystic
-- within 2 (400), a line of walk from it (25: its melee re-roll needs one,
-- ~cox_mystic_can_walk_to_target; without one it stays on magic and never
-- steps, RM-S2) and my distance. -> { { x, z, cost, wakes } }
function QD.COXM.tiles(S, F, row)
    local m, V = S.m, QD.COXM.V
    local key = QD.T2.npc_key(row)
    -- (once a tick per mystic: the pick and the fight tile both ask, and the
    -- two searches ran out the tick's instruction budget, w3 raid29 t484)
    local tc = m.tcache[key]
    if tc ~= nil and tc.t == F.tick then return tc.cands end
    local cands = {}
    for dx = -V.RANGE - 1, V.RANGE + 2 do
        for dz = -V.RANGE - 1, V.RANGE + 2 do
            local tx, tz = row.x + dx, row.z + dz
            local gap = QD.COXM.gap(row, tx, tz)
            if gap >= 2 and gap <= V.RANGE and (m.ban[key .. "@" .. tx .. "," .. tz] or -1) < F.tick then
                local r, sees = api_drive.npc_sees(row.x, row.z, V.SIZE, tx, tz)
                if r == "ok" and sees and QD.COXM.open(tx, tz) then
                    local cost, wakes = 0, 0
                    for _, o in ipairs(m.rows) do
                        if QD.T2.npc_key(o) ~= key then
                            if QD.COXM.gap(o, tx, tz) <= 2 then cost = cost + 400 end
                            if o.st.awake then
                                if QD.COXM.cheb(o.x, o.z, tx, tz) <= V.SIGHT then
                                    local r2, s2 = api_drive.npc_sees(o.x, o.z, V.SIZE, tx, tz)
                                    if r2 ~= "ok" or s2 then cost = cost + 300 end
                                end
                            elseif QD.COXM.mask(o).cells[tx * 65536 + tz] then
                                cost = cost + 1000
                                wakes = wakes + 1
                            end
                        end
                    end
                    cost = cost + QD.COXM.cheb(tx, tz, F.me.x, F.me.z)
                    cands[#cands + 1] = { x = tx, z = tz, cost = cost, wakes = wakes }
                end
            end
        end
    end
    table.sort(cands, function(a, b) return a.cost < b.cost end)
    -- (the line of walk only for the cheapest 12: the ray is dear, and the
    -- whole search ran out the tick's instruction budget, s4 raid13)
    for i = 1, math.min(#cands, 12) do
        local c = cands[i]
        if QD.COXM.walk_line(c.x, c.z, row.x, row.z) then c.cost = c.cost + 25 end
    end
    table.sort(cands, function(a, b) return a.cost < b.cost end)
    m.tcache[key] = { t = F.tick, cands = cands }
    return cands
end

-- THE TARGET (the owner, 2026-10-19: "avoid tanking multiple mystics at a
-- time. Try to kill them one at a time"): an awake one first -- the one I
-- fight, held while it lives, else the fewest hitpoints; with none awake,
-- the sleeper whose cheapest fight tile wakes no other, held 8 ticks.
function QD.COXM.pick(S, F)
    local m = S.m
    local d = F.tick
    local best, bkey = nil, nil
    for _, row in ipairs(m.rows) do
        if row.st.awake then
            -- (health_ratio is -1 with no headbar seen yet: as -1 it outranked
            -- the held target's -1 and the pick flipped between two awake
            -- mystics every tick, raid13 t1926-1939)
            local hp = row.health_ratio
            if hp == nil or hp < 0 then hp = 30 end
            local k = hp * 10 + QD.COXM.cheb(row.x, row.z, F.me.x, F.me.z)
            if QD.T2.npc_key(row) == m.target then k = -1e9 end
            if bkey == nil or k < bkey then best, bkey = row, k end
        end
    end
    if best ~= nil then
        m.next = nil
        return best
    end
    if m.next ~= nil and d - m.next.t < 8 then
        for _, row in ipairs(m.rows) do
            if QD.T2.npc_key(row) == m.next.key then return row end
        end
    end
    m.next = nil
    -- (the tile's price without its walk: three route searches a tick ran
    -- out the instruction budget; the chosen one's walk is priced by
    -- fight_tile)
    for _, row in ipairs(m.rows) do
        local c = QD.COXM.tiles(S, F, row)[1]
        local k = (c and c.cost or 5000) + QD.COXM.cheb(row.x, row.z, F.me.x, F.me.z)
        if bkey == nil or k < bkey then best, bkey = row, k end
    end
    if best ~= nil then m.next = { key = QD.T2.npc_key(best), t = d } end
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
    local free = QD.T2.exec.free_slots(S)
    -- A FULL PACK: the two-handed blowpipe's wield needs a slot for the
    -- defender. The relay's entry makes one only by eating or drinking a
    -- one-dose (QD.raid.cox_make_free), and raid13/24/43 came in with none:
    -- the wield refused for 260-546 ticks. A spare raid item goes down
    -- (QD.COX.spare: an empty vial first, else a raid dose, lent and taken
    -- back between fights).
    if not QD.raid._tob_worn(S, ids.pipe) and QD.raid._tob_count(S, ids.pipe) > 0 and free == 0 then
        local spare, lend = QD.COX.spare(S)
        if spare ~= nil then
            intents.gear = nil
            intents.drop = spare
            if lend ~= nil and m.lent == nil then m.lent = lend end
            m.lends = (m.lends or 0) + 1
        end
    end
    -- THE PRAYER (the owner: "pray mage and rigour"): Protect from Magic and
    -- Rigour while a mystic is awake or I am within 3 of a sleeper's hunt;
    -- off before (raid24 burned four restore doses on both lit for 546 ticks
    -- with every mystic asleep). Rigour is pressed here, not through the
    -- raid's pool gate, which would click it off on a thin pool.
    local fighting, close, awake_n = false, false, 0
    for _, row in ipairs(m.rows) do
        if row.st.awake then
            fighting = true
            awake_n = awake_n + 1
        elseif QD.COXM.cheb(row.x, row.z, F.me.x, F.me.z) <= V.HUNT + V.PRAY_NEAR then
            close = true
        end
    end
    if awake_n >= 2 then m.multi_ticks = (m.multi_ticks or 0) + 1 end
    if d % 20 == 0 then
        local lv = F.levels and F.levels.ranged
        local _, cm = api_drive.varp(ids.com_mode)
        local _, ds = api_drive.varp(ids.damagestyle)
        api_drive.ticklog_mark(string.format("myst stat ranged %s pr %d pfm %s rig %s awake %d hp %d com %s style %s",
            lv and (lv.level .. "/" .. lv.base) or "?", F.prayer or -1, tostring(F.lit["protectfrommagic"] == true),
            tostring(F.lit["rigour"] == true), awake_n, F.hp or -1, tostring(cm), tostring(ds)))
    end
    if awake_n > (m.max_awake or 0) then m.max_awake = awake_n end
    if fighting or close then
        if not F.lit["protectfrommagic"] and (F.prayer or 0) > 0 then
            intents.pray = "protectfrommagic"
        elseif not F.lit["rigour"] and (F.prayer or 0) > 0 and d - (m.rigour_pressed or -10) > 6 then
            intents.pray = "rigour"
            m.rigour_pressed = d
        end
    else
        local off = {}
        if F.lit["protectfrommagic"] then off[#off + 1] = "protectfrommagic" end
        if F.lit["rigour"] then off[#off + 1] = "rigour" end
        if #off > 0 then intents.pray_off = off end
    end
    QD.COXM.drinks(S, F, intents, fighting or close)
    -- THE LENT ITEM, back off the floor while no mystic is awake and the
    -- blowpipe is on (a drop lies 300 ticks): into a free slot, or one an
    -- empty vial makes; given up only once the room is clear with no slot
    if not fighting and m.lent ~= nil and not m.lent.done and QD.raid._tob_worn(S, ids.pipe)
        and not intents.drop and not intents.drink then
        if m.room == nil then
            m.lent.done = true
        elseif free > 0 then
            if QD.COX.retake_lent(S, F, intents, m.room, m.lent) then
                intents.mode = "retake"
                return intents
            end
        elseif QD.raid._tob_count(S, ids.vial_empty) > 0 then
            intents.drop = ids.vial_empty
            intents.mode = "make-room"
            return intents
        elseif m.portal_seen then
            m.lent.done = true
            QD.raid._tob_trace(S, d, "lent item left down: no slot at the clear")
        end
    end
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
        elseif m.portal_seen and m.mark_seen and m.mark_at then
            -- (the portals are up, so the mark is gone; a mark still listed is
            -- a zone outside my 7x7 window, whose change I was never sent --
            -- myst3 stood 1800 ticks 27 tiles off it. Walking at it brings the
            -- zone back into view.)
            B.pull({ name = "mark", x = m.mark_at.x - 1, z = m.mark_at.z - 1, size = 3, weight = 1, t0 = d + 1, t1 = d + B.h })
            intents.mode = "exit"
        else
            intents.mode = "clear"
        end
        return intents
    end
    local key = QD.T2.npc_key(row)
    m.target = key
    if not QD.raid._tob_worn(S, ids.pipe) then
        intents.mode = intents.drop and "make-room" or "gear"
        return intents
    end
    B.target({ name = "mystic", npc = row, size = V.SIZE, range = V.RANGE, speed = V.SPEED, value = V.SWING_VALUE })
    -- danger: beside a mystic (its punch, 25 through the magic prayer), or
    -- one walking at me (melee style) within three; near: any within three
    -- (the click runs two tiles a tick on a route I do not see: from three
    -- tiles of a mystic its next step can end beside it, my22/my13 t63-65)
    local danger, near = false, false
    for _, other in ipairs(m.rows) do
        local gap = QD.COXM.gap(other, F.me.x, F.me.z)
        local walking = other.st.moved_at ~= nil and other.st.moved_at >= d - 1
        if QD.COXM.beside2(other, F.me.x, F.me.z) or (walking and gap <= 3) then danger = true end
        if gap <= 3 then near = true end
    end
    if F.my_swing then
        m.last_swing = d
        m.dog_run = 0
    end
    -- NO SWING FOR 12 TICKS with the target awake: the tile is wrong (out of
    -- reach, or my shot's sight differs from its): banned 40 ticks; the third
    -- in a row hands over to the click (what a player does: the server walks
    -- the maze into reach and keeps shooting -- through any sleeper's hunt,
    -- so only then). raid4 stood at gap 6 for 472 ticks, 122 casts at me.
    if row.st.awake and not danger
        and d - math.max(m.last_swing or -100, row.st.awake_at or d, m.dog_at or -100) > V.NO_SWING then
        if m.ftile ~= nil then m.ban[key .. "@" .. m.ftile.x .. "," .. m.ftile.z] = d + 40 end
        m.ftile = nil
        m.dog_at = d
        m.dog_run = (m.dog_run or 0) + 1
        if m.dog_run >= 3 then
            m.click, m.click_at = key, d
            m.dog = (m.dog or 0) + 1
        end
        QD.raid._tob_trace(S, d, string.format("no swing in %d ticks at %d,%d on %s at %d,%d (%d)", V.NO_SWING,
            F.me.x, F.me.z, key, row.x, row.z, m.dog_run))
    end
    if m.click ~= nil and (m.click ~= key or danger or F.my_swing or d - m.click_at > 15) then m.click = nil end
    if m.click == nil then QD.COXM.fight_tile(S, F, row) end
    -- (never a click on a sleeper: the server's walk to it is a straight
    -- line through the others' hunt -- raid13 and raid29 woke two at once)
    -- (with a sleeper left too: held back, the target was never reached --
    -- 450-660 ticks with one awake, w4 raid2/raid5)
    if m.click == nil and m.ftile == nil and row.st.awake and not danger and not near then m.click, m.click_at = key, d end
    if m.click ~= nil then
        intents.world_op = { kind = "npc", id = row.npc_id, op = 2, element = row.element_id }
        intents.mode = row.st.awake and "fight" or "pull"
    elseif m.ftile ~= nil then
        B.pull({ name = "fight", x = m.ftile.x, z = m.ftile.z, size = 1, weight = 3, t0 = d + 1, t1 = d + B.h })
        intents.mode = "fighttile"
    else
        intents.mode = danger and "clear" or "shoot"
        if danger then m.dangers = (m.dangers or 0) + 1 end
    end
    return intents
end

-- The best fight tile for `row` of the cheapest 6 (QD.COXM.tiles), with
-- the WALK priced too: 1000 for each other sleeper whose mask a tick of the
-- route ends in (raid13: the cheapest tile, 28, lay past two sleepers, and
-- the walk there woke both). `f`, the tile held, is 15 cheaper (re-choosing
-- walked me between two). -> tile, cost, route wakes | nil
function QD.COXM.best(S, F, row, f)
    local key = QD.T2.npc_key(row)
    local cands = QD.COXM.tiles(S, F, row)
    local best, bc, brw = nil, nil, 0
    for i = 1, math.min(#cands, 6) do
        local c = cands[i]
        local ok, rw = QD.COXM.reach(S, c.x, c.z, key)
        if ok then
            local cost = c.cost + 1000 * rw
            if f ~= nil and f.key == key and f.x == c.x and f.z == c.z then cost = cost - 15 end
            if bc == nil or cost < bc then best, bc, brw = c, cost, rw end
        end
    end
    -- (none reached exactly -- across the room, a route the client cuts
    -- short, a walk past a side: the cheapest is walked at, priced by the
    -- masks and the forbids, re-read every 8 ticks)
    -- (an awake one too: w1 raid34 stood 1000 ticks at gap 11 with no tile)
    if best == nil and #cands > 0 then best, bc = cands[1], cands[1].cost end
    return best, bc, brw
end

-- The fight tile for `row`, kept in m.ftile = { key, x, z, cost, rx, rz, t }:
-- re-read when the target moves or every 8 ticks.
function QD.COXM.fight_tile(S, F, row)
    local m = S.m
    local d = F.tick
    if api_drive.npc_sees == nil then return end
    local key = QD.T2.npc_key(row)
    local f = m.ftile
    -- (held while the target stands still, for 8 ticks, and out of every
    -- sleeper's mask -- a wanderer's comes over it)
    local held = f ~= nil and f.key == key and f.rx == row.x and f.rz == row.z and d - f.t < 8
    if held then
        for _, o in ipairs(m.rows) do
            if QD.T2.npc_key(o) ~= key and not o.st.awake and QD.COXM.mask(o).cells[f.x * 65536 + f.z] then held = false end
        end
    end
    if held then return end
    m.ftile = nil
    -- STAND AND SHOOT: an awake target in reach of where I stand (gap 3..5,
    -- it sees me), my tile in no sleeper's mask and seen by no other awake
    -- one -- the tile is mine; a melee one walking at me is shot until the
    -- plan's forbid (gap 2) moves me. (A new tile each time it stepped walked
    -- me the length of the room without a swing: raid35 t414-440.)
    if row.st.awake then
        local g = QD.COXM.gap(row, F.me.x, F.me.z)
        local r0, s0 = api_drive.npc_sees(row.x, row.z, QD.COXM.V.SIZE, F.me.x, F.me.z)
        if g >= 3 and g <= QD.COXM.V.RANGE and r0 == "ok" and s0 then
            local clean = true
            for _, o in ipairs(m.rows) do
                if QD.T2.npc_key(o) ~= key then
                    if o.st.awake then
                        local r2, s2 = api_drive.npc_sees(o.x, o.z, QD.COXM.V.SIZE, F.me.x, F.me.z)
                        if (r2 ~= "ok" or s2) and QD.COXM.cheb(o.x, o.z, F.me.x, F.me.z) <= QD.COXM.V.SIGHT then clean = false end
                    elseif QD.COXM.mask(o).cells[F.me.x * 65536 + F.me.z] then
                        clean = false
                    end
                end
            end
            if clean then
                m.ftile = { key = key, x = F.me.x, z = F.me.z, cost = 0, rx = row.x, rz = row.z, t = d, stand = true }
                m.stands = (m.stands or 0) + 1
                return
            end
        end
    end
    local c, cost, rw = QD.COXM.best(S, F, row, f)
    if c == nil then return end
    local same = f ~= nil and f.key == key and f.x == c.x and f.z == c.z
    m.ftile = { key = key, x = c.x, z = c.z, cost = cost, rx = row.x, rz = row.z, t = d }
    if not same then
        m.ftiles = (m.ftiles or 0) + 1
        if cost >= 1000 then m.wake_tiles = (m.wake_tiles or 0) + 1 end
        local aw = {}
        for _, o in ipairs(m.rows) do aw[#aw + 1] = tostring(QD.T2.npc_key(o)) .. (o.st.awake and "A" or "s") end
        QD.raid._tob_trace(S, d, string.format("fight tile %d,%d for %s at %d,%d cost %d (walk wakes %d) [%s]",
            c.x, c.z, key, row.x, row.z, cost, rw, table.concat(aw, " ")))
    end
end

-- Can I walk to (x, z) exactly, with no tick of the walk ending on a
-- mystic's side (npcs read the tile I end a tick on; a side is a punch)?
-- -> ok, the sleepers but `key` whose wake mask a tick of the walk ends in
function QD.COXM.reach(S, x, z, key)
    local rr, route = api_drive.route(x, z)
    local a = route and route.arrive
    if rr ~= "ok" or a == nil or (a.x or a[1]) ~= x or (a.z or a[2]) ~= z then return false, 0 end
    local woken = {}
    for _, tk in ipairs(route.ticks or {}) do
        for _, o in ipairs(S.m.rows) do
            if QD.COXM.beside2(o, tk.x, tk.z) then return false, 0 end
            local ok = QD.T2.npc_key(o)
            if ok ~= key and not o.st.awake and QD.COXM.mask(o).cells[tk.x * 65536 + tk.z] then woken[ok] = true end
        end
    end
    local n = 0
    for _ in pairs(woken) do n = n + 1 end
    return true, n
end

-- A tile a 1x1 can step into from some side.
function QD.COXM.open(x, z)
    for _, dd in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        if QD.COXM.step_ok(x - dd[1], z - dd[2], dd[1], dd[2]) then return true end
    end
    return false
end

-- One 1x1 step from (x, z) by (ix, iz), one axis: open? (npc_stall's step
-- rule over this lane's collision: the walk flags of the tile entered)
function QD.COXM.step_ok(x, z, ix, iz)
    local r, st = api_drive.npc_stall(x, z, 1, x + 2 * ix, z + 2 * iz, 1)
    return r == "ok" and st.x == x + ix and st.z == z + iz
end

-- The LINE OF WALK from my tile to a mystic's south-west tile -- the test its
-- melee re-roll asks (~cox_mystic_can_walk_to_target: huntall checkvis 2,
-- player -> npc_coord; collision_map_line_of_walk, the ray cast with walk
-- flags): the same ray, stepped one axis at a time. (A 1x1 naive walk said
-- yes where the ray said no: 4 casts on a trap tile and never a step, three
-- tiles running, r1 mystics0.)
function QD.COXM.walk_line(sx, sz, tx, tz)
    local dx, dz = tx - sx, tz - sz
    local ax, az = math.abs(dx), math.abs(dz)
    if ax == 0 and az == 0 then return true end
    local function trunc(a, b)
        local q = a // b
        if q < 0 and q * b ~= a then q = q + 1 end
        return q
    end
    if ax > az then
        local ix = dx > 0 and 1 or -1
        local tan = trunc(dz << 16, ax)
        local zt = (sz << 16) + 0x8000
        if dz < 0 then zt = zt - 1 end
        local x = sx
        while x ~= tx do
            local cz = zt >> 16
            if not QD.COXM.step_ok(x, cz, ix, 0) then return false end
            x = x + ix
            zt = zt + tan
            local nz = zt >> 16
            if nz ~= cz and not QD.COXM.step_ok(x, cz, 0, nz - cz) then return false end
        end
    else
        local iz = dz > 0 and 1 or -1
        local tan = trunc(dx << 16, az)
        local xt = (sx << 16) + 0x8000
        if dx < 0 then xt = xt - 1 end
        local z = sz
        while z ~= tz do
            local cx = xt >> 16
            if not QD.COXM.step_ok(cx, z, 0, iz) then return false end
            z = z + iz
            xt = xt + tan
            local nx = xt >> 16
            if nx ~= cx and not QD.COXM.step_ok(cx, z, nx - cx, 0) then return false end
        end
    end
    return true
end

-- Orthogonally beside a 2x2 at p (its punch tiles).
function QD.COXM.beside2(p, x, z)
    local n = QD.COXM.V.SIZE
    local dx, dz = x - p.x, z - p.z
    if dx >= 0 and dx < n and (dz == -1 or dz == n) then return true end
    if dz >= 0 and dz < n and (dx == -1 or dx == n) then return true end
    return false
end

-- The room's drinks: a prayer enhance in the fight, the restore for a
-- drained Ranged. NO OVERLOAD: the owner (2026-10-19) saves any overload for
-- Olm (the round-4 room sipped it at 85+ hp, and in the relay's idle wait).
function QD.COXM.drinks(S, F, intents, fighting)
    local m, ids = S.m, S.ids
    local e = 0
    for i, obj in ipairs(ids.enhances) do e = e + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    if m.penh_n ~= nil and e < m.penh_n then m.penh_until = F.tick + 458 end
    m.penh_n = e
    -- (the shared floor: dropped once no restore is held, QD.COX.brew_floor)
    local floor = QD.COX.brew_floor(S, intents.brew_below or QD.COXM.V.BREW_BELOW)
    intents.brew_below, intents.eat_below = floor, floor
    if intents.drink or intents.eat or intents.drop or F.tick - (S.last_drink or -10) < 3 then return end
    local pick = nil
    -- THE PRAYER: Rigour is 20% of every shot's accuracy, and the supplies
    -- press it only at 40+ prayer points. The whole raid sends me in low
    -- (raid2 room 10: 33 points, Protect from Magic alone, 13 of 70 shots
    -- landed): a prayer enhance (+) -- a point a prayer tick for 458 ticks --
    -- under 85, as the Olm solver drinks it.
    if fighting and (m.penh_until or -1) < F.tick and (F.prayer or 0) < 85 then
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
    if F.hp > (intents.brew_below or 45)
        and QD.COX.restore_due(F, { "ranged" }, m.restore_gap) then
        local pick2 = QD.raid._tob_first(S, ids.stat_restores)
        if pick2 then
            intents.drink = pick2
            S.last_drink = F.tick
            m.restores = (m.restores or 0) + 1
        end
    end
end

-- ================================================================ THE ROOM

QD.T2.register("cox_mystics", {
    ids = function(opts) return QD.COXM.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_mystics_solve"
        S.max_ticks = opts.max_ticks or 1500
        S.t2.plan_opts = { h = opts.h or QD.COXM.V.H, beam = opts.beam or QD.COXM.V.BEAM }
        S.m = { st = {}, rows = {}, seen_n = 0, casts = 0, punches = 0, target = nil, ban = {}, tcache = {},
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
            S.m.room = room
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
        -- THE BLOWPIPE ON FIRST, then the style: Rapid is pressed on the
        -- weapon worn (com_mode read 1 at the door on the tentacle, and 0 --
        -- accurate, a swing every 3 ticks for 2 -- once the blowpipe went on in
        -- the loop: s5 raid24, every relay fight). A full pack lends a spare
        -- raid item to the floor for the defender's slot (QD.COX.spare), taken
        -- back between fights.
        if not QD.raid._tob_worn(S, ids.pipe) and QD.raid._tob_count(S, ids.pipe) > 0 then
            QD.raid._tob_tab(S, ids.inv_tab)
            if QD.T2.exec.free_slots(S) == 0 then
                local spare, lend = QD.COX.spare(S)
                if spare ~= nil then
                    if QD.T2.drop_guard then QD.T2.drop_guard(spare) end
                    local dr = QD.raid._tob_held(S, spare, QD.TOBS.OP_DROP)
                    QD.raid._tob_trace(S, api_drive.tick(), "lend " .. tostring(spare) .. " for the blowpipe: " .. tostring(dr))
                    if dr == "ok" then
                        S.m.lent = lend
                        S.m.lends = (S.m.lends or 0) + 1
                    end
                    await({ event = "server_tick", match = function() return true end, note = S.who .. ": lend" }, 3)
                end
            end
            local wr = QD.raid._tob_held(S, ids.pipe, QD.TOBS.OP_WIELD)
            QD.raid._tob_trace(S, api_drive.tick(), "blowpipe on at the door: " .. tostring(wr))
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": wield" }, 3)
            end
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
        -- (Rigour is the room's own press: the shared boost channel, gated on
        -- the raid's prayer pool, would click it off)
        S.t2.want.boost = nil
        return intents
    end,

    -- done: every mystic down, the Mark of Power gone and the portals up
    done = function(S, F)
        local m = S.m
        if #m.rows == 0 and m.seen_n > 0 and m.mark_ever and not m.mark_seen and m.portal_seen
            and (m.lent == nil or m.lent.done) then return "ok" end
        return nil
    end,

    summary = function(S)
        local m = S.m
        return string.format("%s; mystics: fight tiles %d (waking %d), no-swing clicks %d, lends %d%s, max awake %d, "
            .. "ticks 2+ awake %d, adds %d, seen %d, casts %d, punches %d, restores %d, mark seen %s now %s, portals %s",
            tostring(m.arrival), m.ftiles or 0, m.wake_tiles or 0, m.dog or 0, m.lends or 0,
            m.lent and (m.lent.done and " (done)" or " (pending)") or "", m.max_awake or 0, m.multi_ticks or 0,
            m.adds or 0, m.seen_n, m.casts, m.punches, m.restores or 0, tostring(m.mark_ever), tostring(m.mark_seen),
            tostring(m.portal_seen))
    end,
})

-- A probe, not the solver: the room's walk map as text lines (a 1x1 step into
-- each tile from a side, npc_stall over this lane's collision), the mystics'
-- 2x2s (M at the south-west tile), the Mark of Power (X) and me (@).
-- -> "ok", { lines }
QD.raid.cox_mystics_map = function()
    local here = QD.COX.here()
    assert(here, "cox_mystics_map: not in a room")
    if here.name ~= "mystics" then
        local _, _, cx, cz = QD.COX.next_origin(here)
        here = QD.COX.room_at(cx, cz, here.level)
        assert(here, "cox_mystics_map: no room ahead")
    end
    local _, me = api_drive.player_tile()
    local ox, oz = here.ox, here.oz
    local function open(x, z)
        for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
            local r, st = api_drive.npc_stall(x - d[1], z - d[2], 1, x + d[1], z + d[2], 1)
            if r == "ok" and st.x == x and st.z == z then return true end
        end
        return false
    end
    local mark, out = {}, {}
    local _, npcs = api_drive.npcs(0)
    for _, row in ipairs(npcs or {}) do
        if QD.COXM.V.MYSTIC[row.npc_id] then
            local x, z = row.server_x or row.x, row.server_z or row.z
            mark[x .. "," .. z] = "M"
            mark[(x + 1) .. "," .. z], mark[x .. "," .. (z + 1)], mark[(x + 1) .. "," .. (z + 1)] = "m", "m", "m"
            out[#out + 1] = string.format("npc %d at %d,%d", row.npc_id, x, z)
        end
    end
    local _, locs = api_drive.locs(40)
    for _, l in ipairs(locs or {}) do
        if l.loc_id == 29741 or l.resolved_loc_id == 29741 then
            mark[l.x .. "," .. l.z] = "X"
            out[#out + 1] = "mark at " .. l.x .. "," .. l.z
        end
    end
    mark[me.x .. "," .. me.z] = "@"
    out[#out + 1] = string.format("room %s v%s turn %s origin %d,%d me %d,%d", here.name, tostring(here.variant),
        tostring(here.turn), ox, oz, me.x, me.z)
    for z = oz + 33, oz - 2, -1 do
        local s = {}
        for x = ox - 2, ox + 33 do s[#s + 1] = mark[x .. "," .. z] or (open(x, z) and "." or "#") end
        out[#out + 1] = string.format("%4d %s", z, table.concat(s))
    end
    return "ok", out
end
