-- quest-driver / cox2_crabs: the crabs room solver (COX_FINISH_PLAN.md round 2).
-- Owned by the crabs unit; a QD.T2 room registered as "cox_crabs".
--
-- Plan: docs/minigames/cox/rooms/crabs_solver.md; spec crabs_spec.md.
--
-- THE METHOD (Synq 0:20:40, ThePerfectG's bad-crabs solve): park and paint.
-- For each crystal a CHAIN of crab tiles turns the orb from the sculpture onto
-- it (each crab one tile past a turn point; the orb turns clockwise at it).
-- I lure a crab onto each chain tile -- a crab follows me and stops one tile
-- behind me, so walking a straight line S -> Q through the tile P leaves it on
-- P -- and smash it there (50-60 ticks of stun). Then, when an orb is a few
-- ticks short of the first bounce, I paint that crab the crystal's colour
-- (blue: the trident; green: the crossbow; red: a smash; white: leave it dim).
--
-- Everything is planned in the room's TEMPLATE frame (the spec's tables) and
-- turned to the world through QD.COX.world; the walkable tiles come from the
-- client's own route (api_drive.route), surveyed on entry. Perception:
-- api_drive.npcs (crabs: tile, colour by npc id, the stun spotanim 245; the
-- orb: tile and colour by npc id), api_drive.locs (the crystals' colour).
-- ==========================================================================

QD.COXCR = QD.COXCR or {}

QD.COXCR.V = {
    H = 8, BEAM = 24,
    STUN_SURE = 48,
    STUN_MAX = 61,           -- a stun is over by this age (content: 50 + random(11))
    IMMUNE = 5,              -- npc_freeze's immunity after a stun ends
    STALL_Q = 10,            -- stage q gives up an approach after this many ticks short of Q
    HOLD_FOR = 47,           -- a held crab is planned around while its stun is younger than this          -- a smashed crab holds at least this long (Mod Ash: 50-60 solo; margin)
    PAINT_TICKS = 8,         -- a paint's colour lasts (wiki "roughly five seconds"; content 8)
    PAINT_LEAD_MIN = 2,      -- paint when the orb is this many ..
    PAINT_LEAD_MAX = 6,      -- .. to this many ticks short of the first bounce
    MAX_CHAIN = 3,           -- solo: three crabs
    PLAN_NODES = 1200,       -- search nodes a tick (the Lua budget)
    SURVEY_PER_TICK = 96,    -- route calls a tick while surveying
    PULL = 10,
}

-- The template tables (crabs_spec.md rows 1-10). Directions N=0 E=1 S=2 W=3.
QD.COXCR.T = {
    [0] = { focus = { 13, 9 }, head = 1, crystals = { { 19, 12 }, { 15, 22 }, { 18, 18 }, { 23, 15 } } },
    [1] = { focus = { 22, 19 }, head = 2, crystals = { { 15, 12 }, { 24, 16 }, { 10, 17 }, { 9, 12 } } },
    [2] = { focus = { 18, 14 }, head = 3, crystals = { { 13, 11 }, { 9, 10 }, { 12, 19 }, { 12, 23 } } },
}
-- THE FIXED SOLUTION PER VARIANT (crabs_solver.md "Round 3 plan"): the crab
-- spawns (~cox_crab_spawn_tile, the solo's three) and, in crystal order, each
-- step's chain -- { x, z, colour } in the orb's path order, colour -1 dim, 1
-- red (the smash), 2 green (the crossbow), 3 blue (the trident) -- in
-- template tiles. Derived offline from the surveyed walk map with a copy of
-- CR.sim; each step adds crabs to the ones held, or moves only the crab parked
-- first (its stun lapses first). Checked against the room at run time
-- (CR.table_check); a room that disagrees falls back to the search.
QD.COXCR.PLAN = {
    -- (variant 0: every crystal needs all three crabs and the only cyan
    -- chain is in the east wall's corner. Measured, round 3: the table 16/19
    -- green, median 786 ticks; the search 14/19, median 466 -- green wins.
    -- `off = "<why>"` on a variant sends it to the search.)
    [0] = { spawn = { { 22, 10 }, { 16, 14 }, { 17, 21 } }, steps = {
        -- (round 4: a stun is 50-60 ticks and never renews, so no crab
        -- carries from one step to the next; each later step is the most
        -- compact chain -- orb paths 13-19 ticks, not 21-35 -- parked
        -- last-first, the crab on the orb's line last)
        { ci = 4, crabs = { { 25, 9, 1 }, { 24, 7, -1 }, { 22, 8, -1 } } },
        { ci = 1, crabs = { { 21, 9, -1 }, { 20, 7, -1 }, { 18, 8, -1 } } },
        { ci = 3, crabs = { { 20, 9, 2 }, { 19, 7, -1 }, { 17, 8, -1 } } },
        { ci = 2, crabs = { { 17, 9, 3 }, { 16, 7, -1 }, { 14, 8, -1 } } },
    } },
    [1] = { spawn = { { 10, 14 }, { 15, 9 }, { 17, 17 } }, steps = {
        { ci = 4, crabs = { { 22, 10, 1 }, { 8, 11, -1 } } },
        { ci = 1, crabs = { { 22, 10, -1 }, { 14, 11, -1 } } },
        { ci = 3, crabs = { { 22, 16, 2 } } },
        { ci = 2, crabs = { { 22, 14, 3 }, { 20, 15, -1 }, { 21, 17, -1 } } },
    } },
    [2] = { spawn = { { 9, 12 }, { 7, 18 }, { 13, 22 } }, steps = {
        { ci = 3, crabs = { { 11, 14, 2 } } },
        { ci = 1, crabs = { { 11, 14, -1 }, { 12, 16, -1 }, { 14, 15, -1 } } },
        { ci = 4, crabs = { { 7, 14, 1 }, { 8, 24, -1 } } },
        { ci = 2, crabs = { { 7, 14, 3 }, { 8, 16, -1 }, { 10, 15, -1 } } },
    } },
}
-- crystal position -> the orb colour it needs (^cox_crab_*: 0 white, 1 red, 2 green, 3 blue)
QD.COXCR.NEED = { 0, 3, 2, 1 }
QD.COXCR.DX = { [0] = 0, [1] = 1, [2] = 0, [3] = -1 }
QD.COXCR.DZ = { [0] = 1, [1] = 0, [2] = -1, [3] = 0 }

local CR = QD.COXCR

function CR.key(x, z) return x * 64 + z end

function CR.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_crabs", opts)
    local room = QD.raid._tob_symbols("cox_crabs", {
        { "crab_grey", "npc", "raids_lasercrabs_crab_grey" }, { "crab_red", "npc", "raids_lasercrabs_crab_red" },
        { "crab_green", "npc", "raids_lasercrabs_crab_green" }, { "crab_blue", "npc", "raids_lasercrabs_crab_blue" },
        { "orb_white", "npc", "raids_lasercrabs_energy_white" }, { "orb_red", "npc", "raids_lasercrabs_energy_red" },
        { "orb_green", "npc", "raids_lasercrabs_energy_green" }, { "orb_blue", "npc", "raids_lasercrabs_energy_blue" },
        { "cry_white", "loc", "raids_lasercrabs_smallcrystal_white" },
        { "stun", "spotanim", "stunned_thieving" },
        { "wave_white", "spotanim", "windwave_impact" }, { "wave_red", "spotanim", "firewave_impact" },
        { "wave_green", "spotanim", "earthwave_impact" }, { "wave_blue", "spotanim", "waterwave_impact" },
        { "trident", "obj", "toxic_tots_charged" }, { "xbow", "obj", "xbows_crossbow_dragon" },
        { "tentacle", "obj", "abyssal_tentacle" }, { "hammer", "obj", "hammer" },
        { "dwh", "obj", "dragon_warhammer" }, { "elder", "obj", "elder_maul" },
        { "defender", "obj", "dragon_parryingdagger" },
        { "hammer_spawn", "loc", "raids_lasercrabs_hammer" },
        { "retaliate", "component", "combat_interface:retaliate" }, { "nodef", "varp", "varp172_option_nodef" },
    })
    for k, v in pairs(room) do ids[k] = v end
    QD.COX.raid_restores(ids)
    ids.spares = QD.COX.spare_ids()
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_crabs: no combat tab")
    ids.combat_tab = combat_tab
    ids.waves = { [ids.wave_white] = true, [ids.wave_red] = true, [ids.wave_green] = true, [ids.wave_blue] = true }
    ids.crab_colour = { [ids.crab_grey] = -1, [ids.crab_red] = 1, [ids.crab_green] = 2, [ids.crab_blue] = 3 }
    ids.orb_colour = { [ids.orb_white] = 0, [ids.orb_red] = 1, [ids.orb_green] = 2, [ids.orb_blue] = 3 }
    ids.crab_by_colour = { [-1] = ids.crab_grey, [1] = ids.crab_red, [2] = ids.crab_green, [3] = ids.crab_blue }
    return ids
end

-- ================================================================== THE ORB

-- What an orb of colour `orb` becomes off a crab of colour `crab` (-1 dim);
-- -2: destroyed (cox_crabs.rs2 ~cox_crab_bounce_colour).
function CR.bounce(orb, crab)
    if crab == -1 then return orb end
    if orb == 0 then return crab end
    if orb == crab then return orb end
    return -2
end

-- Simulate an orb (template frame) from tile (x, z) heading `d`, colour `col`,
-- past the crabs `crabs` (key -> colour). -> { tiles = { {x, z}, ... } (the
-- tile it is on at the end of each tick from 1), stop = "crystal"|"wall"|
-- "abort"|"bad", crystal = index, colour, bounce1 = the tick it turns at the
-- first crab, bounce1_crab = key }
function CR.sim(S, x, z, d, col, crabs, maxsteps)
    local cr = S.cr
    local out = { tiles = {}, stop = "long" }
    for step = 1, maxsteps or 50 do
        local bounces = 0
        local moved = false
        for _ = 1, 10 do
            local nx, nz = x + CR.DX[d], z + CR.DZ[d]
            local k = CR.key(nx, nz)
            local c = crabs[k]
            if c ~= nil then
                bounces = bounces + 1
                local nc = CR.bounce(col, c)
                if nc == -2 then out.stop = "bad"; return out end
                col = nc
                if bounces >= 2 then out.stop = "abort"; return out end
                if out.bounce1 == nil then out.bounce1 = step; out.bounce1_crab = k end
                out.bounces = out.bounces or {}
                if out.bounces[k] == nil then out.bounces[k] = step end
                d = (d + 1) % 4
            else
                local ci = cr.crystal_at[k]
                if ci ~= nil then
                    out.tiles[#out.tiles + 1] = { nx, nz }
                    out.stop, out.crystal, out.colour = "crystal", ci, col
                    return out
                end
                if not cr.walk[k] then out.stop = "wall"; return out end
                x, z = nx, nz
                out.tiles[#out.tiles + 1] = { x, z }
                moved = true
                break
            end
        end
        if not moved then out.stop = "abort"; return out end
    end
    return out
end

-- ================================================================ THE PLAN

-- Can a crab be led onto P (template)? An approach `a` with Q = P + a (where
-- I stop), P - a and S = P - 2a (where I start) all walkable for me.
function CR.approach(S, px, pz)
    local cr = S.cr
    local best = nil
    for a = 0, 3 do
        local qx, qz = px + CR.DX[a], pz + CR.DZ[a]
        local bx, bz = px - CR.DX[a], pz - CR.DZ[a]
        local sx, sz = px - 2 * CR.DX[a], pz - 2 * CR.DZ[a]
        if cr.reach[CR.key(qx, qz)] and cr.reach[CR.key(bx, bz)] and cr.reach[CR.key(sx, sz)] then
            best = best or {}
            best[#best + 1] = { a = a, q = { qx, qz }, s = { sx, sz } }
        end
    end
    return best
end

-- The fewest-crab chain for crystal `ci`: a breadth-first search over the
-- orb's path from the focus (fewest crabs first, then fewest steps), placing a
-- crab (up to MAX_CHAIN) one tile ahead of the orb to turn it, never twice in
-- one tick (the double-bounce abort); only the first crab is painted. The
-- chain found is checked by CR.sim. -> { crabs = { {x, z, colour}, ... in
-- path order }, steps } or nil
function CR.plan_crystal(S, ci, fixed, job)
    local cr, V = S.cr, CR.V
    fixed = fixed or {}
    job = job or {}
    local T = CR.T[cr.room.variant]
    local need = CR.NEED[ci]
    cr.approach_ok = cr.approach_ok or {}
    local function can_park(k, x, z)
        local v = cr.approach_ok[k]
        if v == nil then
            v = cr.reach[k] == true and cr.crystal_at[k] == nil and CR.approach(S, x, z) ~= nil
            cr.approach_ok[k] = v
        end
        return v
    end
    local seen = job.seen or {}
    local queues = job.queues or { [0] = {}, [1] = {}, [2] = {}, [3] = {} }
    local fresh = job.seen == nil
    job.seen, job.queues = seen, queues
    job.n = job.n or 0
    job.pos = job.pos or { [0] = 1, [1] = 1, [2] = 1, [3] = 1 }
    local budget = V.PLAN_NODES
    local function push(n, node)
        local sk = ((node.x * 32 + node.z) * 4 + node.d) * 8 + (node.col + 1) * 2 + (node.bounced and 1 or 0)
        local prev = seen[sk]
        if prev ~= nil and prev <= n then return end
        seen[sk] = n
        local q = queues[n]
        q[#q + 1] = node
    end
    if fresh then
        push(0, { x = T.focus[1], z = T.focus[2], d = T.head, col = 0, n = 0, steps = 0, bounced = false })
    end
    for n = job.n, V.MAX_CHAIN do
        job.n = n
        local q = queues[n]
        local i = job.pos[n]
        while i <= #q do
            budget = budget - 1
            if budget <= 0 then
                job.pos[n] = i
                return "more"
            end
            local node = q[i]
            i = i + 1
            if node.steps <= 60 then
                local nx, nz = node.x + CR.DX[node.d], node.z + CR.DZ[node.d]
                local k = CR.key(nx, nz)
                local ci2 = cr.crystal_at[k]
                local fc = fixed[k]
                if fc ~= nil then
                    -- a crab already held here: the orb turns (free), or a
                    -- second turn this tick ends it
                    if not node.bounced then
                        local nc = CR.bounce(node.col, fc)
                        if nc ~= -2 then
                            push(n, { x = node.x, z = node.z, d = (node.d + 1) % 4, col = nc, n = n,
                                      steps = node.steps, bounced = true, parent = node,
                                      crab = { nx, nz, fc, held = true } })
                        end
                    end
                    goto continue
                end
                -- a crab here: turn (not straight after a turn)
                if not node.bounced and n < V.MAX_CHAIN and can_park(k, nx, nz) then
                    for _, c in ipairs({ -1, 1, 2, 3 }) do
                        if c == -1 or n == 0 then
                            local nc = CR.bounce(node.col, c)
                            if nc ~= -2 then
                                push(n + 1, { x = node.x, z = node.z, d = (node.d + 1) % 4, col = nc, n = n + 1,
                                              steps = node.steps, bounced = true, parent = node, crab = { nx, nz, c } })
                            end
                        end
                    end
                end
                if ci2 ~= nil then
                    if ci2 == ci and node.col == need then
                        local chain = {}
                        local p = node
                        while p ~= nil do
                            if p.crab then table.insert(chain, 1, p.crab) end
                            p = p.parent
                        end
                        -- check it end to end
                        local m = {}
                        for kk, cc in pairs(fixed) do m[kk] = cc end
                        for _, c in ipairs(chain) do m[CR.key(c[1], c[2])] = c[3] end
                        local sim = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, m, 80)
                        if sim.stop == "crystal" and sim.crystal == ci and sim.colour == need then
                            local new = 0
                            for _, c in ipairs(chain) do if not c.held then new = new + 1 end end
                            return { crabs = chain, steps = node.steps + 1, new = new }
                        end
                    end
                elseif cr.walk[k] then
                    push(n, { x = nx, z = nz, d = node.d, col = node.col, n = n, steps = node.steps + 1,
                              bounced = false, parent = node })
                end
            end
            ::continue::
        end
        job.pos[n] = i
    end
    return nil
end

-- ============================================================== PERCEPTION

function CR.see(S, F)
    local cr, ids = S.cr, S.ids
    local room = cr.room
    cr.prev_keys = {}
    for _, c in ipairs(cr.crabs or {}) do cr.prev_keys[c.key] = true end
    cr.me_prev = cr.me
    cr.crabs, cr.orb = {}, nil
    local mx0, mz0 = QD.COX.template(room, F.me.x, F.me.z)
    for _, row in ipairs(F.npcs) do
        local col = ids.crab_colour[row.npc_id]
        local oc = ids.orb_colour[row.npc_id]
        if col ~= nil or oc ~= nil then
            local lx, lz = QD.COX.template(room, row.x, row.z)
            if lx >= 0 and lx < 32 and lz >= 0 and lz < 32 then
                if col ~= nil then
                    local k = CR.key(lx, lz)
                    -- THE STUN, per crab (its npc slot): spotanim 245 sent at a
                    -- tick it was not sent at before; it holds while the crab
                    -- stays on the tile it was stunned on (the plugin's own
                    -- reading). A crab keeps its last graphic as it walks, so
                    -- a tile cannot carry this.
                    -- (npc_freeze neither RENEWS a frozen crab nor freezes
                    -- one inside its 5-tick immunity: a 245 re-sent then --
                    -- the fire's red paint, a smash at 48-60 -- is no new
                    -- stun. Round 3 restarted the timer on every graphic.)
                    local sl = cr.slots[row.slot]
                    if sl == nil then
                        sl = { mark = row.spotanim_tick }
                        cr.slots[row.slot] = sl
                    elseif row.spotanim_sent_id == ids.stun and row.spotanim_tick ~= nil and sl.mark ~= row.spotanim_tick then
                        sl.mark = row.spotanim_tick
                        if CR.smashable(sl, F.tick) then
                            sl.at, sl.x, sl.z = F.tick, lx, lz
                            sl.moved_at, sl.offn = nil, 0
                        end
                    end
                    -- (the stun is over once the crab is seen off its tile two
                    -- ticks running)
                    if sl.at ~= nil and sl.moved_at == nil then
                        if lx ~= sl.x or lz ~= sl.z then
                            sl.offn = (sl.offn or 0) + 1
                            if sl.offn >= 2 then sl.moved_at = F.tick - 1 end
                        else
                            sl.offn = 0
                        end
                    end
                    -- (woken: within its 3-tile aggression range of me)
                    if math.max(math.abs(lx - mx0), math.abs(lz - mz0)) <= 3 then sl.near_at = F.tick end
                    local age = sl.at and (F.tick - sl.at) or 999
                    cr.crabs[#cr.crabs + 1] = { x = lx, z = lz, col = col, row = row, key = k, tkey = k, tx = lx, tz = lz,
                        slot = sl, smashable = CR.smashable(sl, F.tick),
                        maybe = sl.at ~= nil and sl.moved_at == nil and age >= CR.V.STUN_SURE and age < CR.V.STUN_MAX }
                else
                    cr.orb = { x = lx, z = lz, col = oc, row = row }
                end
            end
        end
    end
    -- the stuns by tile, this tick
    -- (keyed by the tile it was stunned on: a held crab is seen a tile off
    -- it now and then -- a free crab pressing on it -- and is back the next)
    cr.stun = {}
    for _, c in ipairs(cr.crabs) do
        local sl = cr.slots[c.row.slot]
        if sl ~= nil and sl.at ~= nil and sl.moved_at == nil and math.max(math.abs(sl.x - c.x), math.abs(sl.z - c.z)) <= 1
            and F.tick - sl.at < CR.V.STUN_MAX then
            if sl.x ~= c.x or sl.z ~= c.z then
                sl.off = (sl.off or 0) + 1
            else
                sl.off = 0
            end
            if (sl.off or 0) <= 2 then
                -- (held on its stun tile; its true tile stays in tkey for the orb)
                c.key, c.x, c.z = CR.key(sl.x, sl.z), sl.x, sl.z
                cr.stun[c.key] = { at = sl.at }
            end
        end
    end
    -- the orb's heading: its last step; a fresh orb at the focus heads the
    -- template heading
    local T = CR.T[room.variant]
    local o = cr.orb
    if o ~= nil then
        local last = cr.orb_last
        if o.x == T.focus[1] and o.z == T.focus[2] then
            cr.orb_dir = T.head
        elseif last ~= nil and (last.x ~= o.x or last.z ~= o.z) then
            local dx, dz = o.x - last.x, o.z - last.z
            if math.abs(dx) + math.abs(dz) == 1 then
                for d = 0, 3 do
                    if CR.DX[d] == dx and CR.DZ[d] == dz then cr.orb_dir = d end
                end
            else
                cr.orb_dir = T.head
            end
        end
        cr.orb_last = { x = o.x, z = o.z }
    end
    -- the crystals lit: a white crystal loc on the tile
    local lr, locs = api_drive.locs(40)
    if lr == "ok" then
        for _, l in ipairs(locs) do
            if (l.resolved_loc_id or l.loc_id) == ids.cry_white then
                local lx, lz = QD.COX.template(room, l.x, l.z)
                local ci = cr.crystal_at[CR.key(lx, lz)]
                if ci ~= nil then cr.lit[ci] = true end
            end
        end
    end
    local mx, mz = QD.COX.template(room, F.me.x, F.me.z)
    cr.me = { x = mx, z = mz }
    -- an orb that burst on me, by its damage: my hitpoints fell by exactly
    -- ceil(min(99, hp) / 4) the tick an orb that was within a step of me went
    -- (cox_crabs.rs2 ~cox_crab_beam_damage)
    if cr.hp_prev ~= nil and F.hp < cr.hp_prev and cr.orb_near_prev then
        local want = (math.min(99, cr.hp_prev) * 25 + 99) // 100
        if cr.hp_prev - F.hp == want and cr.hit_seen ~= F.tick then
            cr.orb_hits = cr.orb_hits + 1
            cr.hit_seen = F.tick
        end
    end
    cr.hp_prev = F.hp
    cr.orb_near_prev = cr.orb ~= nil and math.max(math.abs(cr.orb.x - mx), math.abs(cr.orb.z - mz)) <= 2
    -- an orb that burst on me: its wave impact on my tile (counted once a tick)
    local sr, spots = api_drive.spotanims(2)
    if sr == "ok" then
        for _, sp in ipairs(spots) do
            if ids.waves[sp.spotanim_id] and sp.tile_x == F.me.x and sp.tile_z == F.me.z and cr.hit_seen ~= F.tick then
                local key = sp.tile_x .. "," .. sp.tile_z .. ":" .. tostring(sp.cycles_left)
                if cr.last_wave ~= key then
                    cr.orb_hits = cr.orb_hits + 1
                    cr.hit_seen = F.tick
                end
                cr.last_wave = key
            end
        end
    end
end

-- May I stand still this tick (an op holds me where I am)? Not when the orb
-- reaches my tile within `ticks` ticks.
function CR.safe_here(S, ticks)
    local cr = S.cr
    local d = cr.danger[CR.key(cr.me.x, cr.me.z)]
    return d == nil or d > (ticks or 3)
end

function CR.stunned(S, F, k)
    local st = S.cr.stun[k]
    return st ~= nil and F.tick - st.at < CR.V.STUN_SURE
end

-- Would a smash freeze this crab (its slot) now? Never stunned; or seen off
-- its stun tile IMMUNE ticks ago; or the stun so old it is over and its
-- immunity too (a crab beside me attacks and never steps, so it may not move).
function CR.smashable(sl, tick)
    if sl.at == nil then return true end
    if sl.moved_at ~= nil then return tick >= sl.moved_at + CR.V.IMMUNE end
    return tick - sl.at >= CR.V.STUN_MAX + CR.V.IMMUNE
end

function CR.crab_map(S, override)
    local m = {}
    -- (where each crab IS: the orb meets the true tile)
    for _, c in ipairs(S.cr.crabs) do m[c.tkey] = c.col end
    for k, col in pairs(override or {}) do m[k] = col end
    return m
end

-- ================================================================ HAZARDS

-- The orb's tiles over the next ticks (and the next orb's from the focus) as
-- forbidden tiles for me (NR EnergyFocus.checkPlayers: the tile it is on and
-- the tile it steps to).
function CR.predict(S, F, B)
    local cr = S.cr
    if cr.room == nil or not cr.planned then return end
    -- (perception first: this tick's crabs and orb)
    CR.see(S, F)
    cr.seen_tick = F.tick
    cr.nhits = 0
    cr.danger = {}
    local room, T = cr.room, CR.T[cr.room.variant]
    local crabs = CR.crab_map(S)
    local start, n = 0, 0
    local minlen = nil
    local function forbid(path, t_from)
        if t_from == 0 and (minlen == nil or #path.tiles < minlen) then minlen = #path.tiles end
        for i, tile in ipairs(path.tiles) do
            local wx, wz = QD.COX.world(room, tile[1], tile[2])
            local t = F.tick + t_from + i
            B.hit({ name = "orb", x = wx, z = wz, t0 = t - 1, t1 = t, forbid = true, dmg_exp = 20, dmg_max = 25 })
            local dk = CR.key(tile[1], tile[2])
            if cr.danger[dk] == nil or cr.danger[dk] > t - F.tick then cr.danger[dk] = t - F.tick end
            cr.nhits = cr.nhits + 1
            if cr.nhits <= 3 then cr.hit_text = (cr.hit_text or "") .. string.format(" %d,%d@%d", tile[1], tile[2], t - F.tick) end
            if t - F.tick > B.h + 1 then break end
        end
    end
    cr.hit_text = ""
    -- (a free crab is where it is now, or gone: both paths are forbidden)
    -- (held: stunned long enough ago to be sure it still is; an older stun
    -- may end any tick)
    local held_only = {}
    for _, c in ipairs(cr.crabs) do
        if CR.stunned(S, F, c.key) then held_only[c.key] = c.col end
    end
    if cr.orb ~= nil and cr.orb_dir ~= nil then
        cr.danger[CR.key(cr.orb.x, cr.orb.z)] = 0
        local p = CR.sim(S, cr.orb.x, cr.orb.z, cr.orb_dir, cr.orb.col, crabs, 30)
        local q = CR.sim(S, cr.orb.x, cr.orb.z, cr.orb_dir, cr.orb.col, held_only, 30)
        local wx, wz = QD.COX.world(room, cr.orb.x, cr.orb.z)
        B.hit({ name = "orb", x = wx, z = wz, t0 = F.tick + 1, t1 = F.tick + 1, forbid = true, dmg_exp = 20, dmg_max = 25 })
        forbid(p, 0)
        forbid(q, 0)
        -- (a free crab moves: the path without each one near the orb's line,
        -- and with one stepped onto each of its next tiles)
        local o = cr.orb
        for _, c in ipairs(cr.crabs) do
            if not CR.stunned(S, F, c.key) and math.max(math.abs(c.tx - o.x), math.abs(c.tz - o.z)) <= 6 then
                local m = {}
                for k, col in pairs(crabs) do if k ~= c.tkey then m[k] = col end end
                forbid(CR.sim(S, o.x, o.z, cr.orb_dir, o.col, m, 30), 0)
            end
        end
        -- (a crab reaches the orb's i-th tile in i ticks, and the rows lag a
        -- tick: cr43, a crab freed two tiles off the sculpture's doorstep
        -- turned the newborn orb onto me)
        for i = 1, math.min(3, #q.tiles) do
            local tx, tz = q.tiles[i][1], q.tiles[i][2]
            for _, c in ipairs(cr.crabs) do
                if (not CR.stunned(S, F, c.key) or c.tkey ~= c.key) and math.max(math.abs(c.tx - tx), math.abs(c.tz - tz)) <= i + 1 then
                    local m = {}
                    for k, col in pairs(crabs) do m[k] = col end
                    m[CR.key(tx, tz)] = c.col
                    forbid(CR.sim(S, o.x, o.z, cr.orb_dir, o.col, m, 30), 0)
                    break
                end
            end
        end
        start = math.max(#p.tiles, #q.tiles) + 1
    end
    -- the next orb, from the focus (spawned the tick this one ends, moves a
    -- tick later): at the end this orb's longest and shortest futures give
    local function newborn(st)
        local p2 = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, crabs, 30)
        local fx, fz = QD.COX.world(room, T.focus[1], T.focus[2])
        B.hit({ name = "orb", x = fx, z = fz, t0 = F.tick + st, t1 = F.tick + st + 1, forbid = true, dmg_exp = 20, dmg_max = 25 })
        forbid(p2, st + 1)
        local q2 = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, held_only, 30)
        forbid(q2, st + 1)
        -- (and a free crab stepping onto one of its first tiles -- the
        -- sculpture's doorstep turns the newborn orb straight at whoever
        -- stands beside it)
        for i = 1, math.min(3, #q2.tiles) do
            local tx, tz = q2.tiles[i][1], q2.tiles[i][2]
            for _, c in ipairs(cr.crabs) do
                if (not CR.stunned(S, F, c.key) or c.tkey ~= c.key) and math.max(math.abs(c.tx - tx), math.abs(c.tz - tz)) <= i + 1 then
                    local m = {}
                    for k, col in pairs(crabs) do m[k] = col end
                    m[CR.key(tx, tz)] = c.col
                    forbid(CR.sim(S, T.focus[1], T.focus[2], T.head, 0, m, 30), st + 1)
                    break
                end
            end
        end
    end
    newborn(start)
    if minlen ~= nil and minlen + 1 < start then newborn(minlen + 1) end
end

-- ================================================================ STRATEGY

function CR.survey(S, F)
    local cr = S.cr
    local done = 0
    while cr.survey_i < 1024 and done < CR.V.SURVEY_PER_TICK do
        local lx, lz = cr.survey_i // 32, cr.survey_i % 32
        local x, z = QD.COX.world(cr.room, lx, lz)
        local r, route = api_drive.route(x, z)
        if r == "ok" and route and route.arrive then
            local ax = route.arrive.x or route.arrive[1]
            local az = route.arrive.z or route.arrive[2]
            if ax == x and az == z then cr.reach[CR.key(lx, lz)] = true end
        end
        cr.survey_i = cr.survey_i + 1
        done = done + 1
    end
    if cr.survey_i < 1024 then return false end
    if not cr.walk_built then
        -- the orb walks every tile I can, the crystals and the focus
        local T = CR.T[cr.room.variant]
        for k in pairs(cr.reach) do cr.walk[k] = true end
        for ci, c in ipairs(T.crystals) do cr.crystal_at[CR.key(c[1], c[2])] = ci end
        cr.walk[CR.key(T.focus[1], T.focus[2])] = true
        cr.walk_built = true
        if api_drive.ticklog_mark ~= nil then
            for lz = 0, 31 do
                local row = {}
                for lx = 0, 31 do row[#row + 1] = cr.walk[CR.key(lx, lz)] and "#" or "." end
                api_drive.ticklog_mark(string.format("crmap %02d %s", lz, table.concat(row)))
            end
        end
    end
    cr.planned = true
    return true
end

-- The next crystal to do: unlit, planned, fewest crabs first.
function CR.next_crystal(S)
    local cr = S.cr
    local best, bn = nil, nil
    for ci = 1, 4 do
        local p = cr.plans[ci]
        if not cr.lit[ci] and p ~= nil then
            local n = #p.crabs * 100 + p.steps
            if bn == nil or n < bn then best, bn = ci, n end
        end
    end
    return best
end

function CR.walk_to(S, F, B, lx, lz, weight)
    local wx, wz = QD.COX.world(S.cr.room, lx, lz)
    B.pull({ name = "go", x = wx, z = wz, size = 1, weight = weight or CR.V.PULL, t0 = F.tick + 1, t1 = F.tick + B.h })
    return wx, wz
end

-- A tile cardinally beside P to stand on while its crab's stun runs out:
-- walkable, no crab on it, off the current step's planned orb path and not
-- reached by the live orb within 3 ticks; nearest me. -> {x, z} or nil
function CR.beside(S, F, P)
    local cr = S.cr
    local T = CR.T[cr.room.variant]
    local onpath = {}
    if cr.cur ~= nil then
        local m = CR.held(S, F)
        for _, c in ipairs(cr.cur.plan.crabs) do m[CR.key(c[1], c[2])] = c[3] end
        local sim = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, m, 80)
        for _, t in ipairs(sim.tiles) do onpath[CR.key(t[1], t[2])] = true end
    end
    local occupied = {}
    for _, c in ipairs(cr.crabs) do occupied[c.tkey] = true end
    local best, bd = nil, nil
    for a = 0, 3 do
        local x, z = P[1] + CR.DX[a], P[2] + CR.DZ[a]
        local k = CR.key(x, z)
        local dg = cr.danger[k]
        if cr.reach[k] and not occupied[k] and not onpath[k] and cr.crystal_at[k] == nil and (dg == nil or dg > 3) then
            local d = math.abs(x - cr.me.x) + math.abs(z - cr.me.z)
            if bd == nil or d < bd then best, bd = { x, z }, d end
        end
    end
    return best
end

-- Lead a free crab onto chain tile P and smash it there.
function CR.park(S, F, B, intents, P, wake)
    local cr = S.cr
    local L = cr.lure
    if L == nil or L.px ~= P[1] or L.pz ~= P[2] then
        L = { px = P[1], pz = P[2], ap = nil, stage = "s", since = F.tick, tried = {} }
        cr.lure = L
    end
    -- the approach: whose start is nearest me, off the focus's doorstep (a
    -- crab led there stands in front of the sculpture and turns every orb),
    -- not one that got me stuck
    if L.ap == nil then
        local T = CR.T[cr.room.variant]
        -- (the orb's line now, from the sculpture past the crabs held: an
        -- approach standing on it is chased off by every orb)
        local line = {}
        local held = CR.held(S, F)
        local base = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, held, 60)
        for _, t in ipairs(base.tiles) do line[CR.key(t[1], t[2])] = true end
        local best, bd = nil, nil
        for _, a in ipairs(CR.approach(S, P[1], P[2]) or {}) do
            local d = math.abs(a.s[1] - cr.me.x) + math.abs(a.s[2] - cr.me.z)
            if line[CR.key(a.q[1], a.q[2])] then d = d + 30 end
            if line[CR.key(a.s[1], a.s[2])] then d = d + 15 end
            if math.max(math.abs(a.s[1] - T.focus[1]), math.abs(a.s[2] - T.focus[2])) <= 2 then d = d + 50 end
            -- (Q beside the sculpture: the crab trailing the one led onto P
            -- stops on the orb's first tile and turns the newborn orb onto
            -- me -- cr104, (14,8) from the west, Q (13,8))
            if math.max(math.abs(a.q[1] - T.focus[1]), math.abs(a.q[2] - T.focus[2])) <= 1 then d = d + 250 end
            if L.tried[a.a] then d = d + 100 end
            if L.tried_all then d = d - (L.tried[a.a] and 100 or 0) end
            if bd == nil or d < bd then best, bd = a, d end
        end
        L.ap = best
        L.moved_at = F.tick
    end
    -- stuck (not moved for 10 ticks short of S): another approach
    if cr.me_prev and (cr.me_prev.x ~= cr.me.x or cr.me_prev.z ~= cr.me.z) then L.moved_at = F.tick end
    -- (or waiting at S 15 ticks for a clear window that does not come: a
    -- free crab beside me keeps turning the orb across the line)
    if L.ap ~= nil and L.stage == "s" and F.tick - (L.moved_at or F.tick) > 10
        and ((cr.me.x ~= L.ap.s[1] or cr.me.z ~= L.ap.s[2]) or F.tick - (L.moved_at or F.tick) > 15) then
        L.tried[L.ap.a] = true
        local n = 0
        for _ in pairs(L.tried) do n = n + 1 end
        if n >= 4 then L.tried = {} end
        L.ap = nil
        intents.mode = "park-stuck"
        return
    end
    -- (stage s with no lead for 24 ticks: the window never comes -- a free
    -- crab on the orb's line beside S turns every orb across P, cr131 sat
    -- 1,000 ticks dodging between S and the tile beside it)
    if L.stage ~= "s" then L.s_at = nil end
    L.s_at = L.s_at or F.tick
    if cr.waking ~= nil then L.s_at = F.tick end
    if L.ap ~= nil and L.stage == "s" and F.tick - L.s_at > 24 then
        L.tried[L.ap.a] = true
        local n = 0
        for _ in pairs(L.tried) do n = n + 1 end
        if n >= #(CR.approach(S, P[1], P[2]) or {}) then L.tried = {} end
        L.ap, L.s_at = nil, F.tick
        intents.mode = "park-stalls"
        return
    end
    intents.mode = "park" .. P[1] .. "," .. P[2] .. ":" .. L.stage
    local pk = CR.key(P[1], P[2])
    -- a free crab SETTLED on P (there last tick too, and me still beside it):
    -- smash it. A crab still moving takes the smash wherever it steps to.
    for _, c in ipairs(cr.crabs) do
        if c.key == pk and not CR.stunned(S, F, pk) and cr.prev_keys[pk] then
            local d = math.max(math.abs(c.x - cr.me.x), math.abs(c.z - cr.me.z))
            local still = cr.me_prev ~= nil and cr.me_prev.x == cr.me.x and cr.me_prev.z == cr.me.z
            if d <= 1 and still and (c.x == cr.me.x or c.z == cr.me.z) and CR.safe_here(S) and c.smashable then
                if CR.smash(S, intents, c) then L.smashed = F.tick end
                return
            end
        end
    end
    -- RE-PARK IN PLACE: a crab still on P, never off it since its stun,
    -- whose stun is running out, or ran out inside its immunity (a smash now
    -- is void): stand beside it,
    -- cardinal, and smash it the tick it is smashable -- a crab beside its
    -- target attacks and does not step
    for _, c in ipairs(cr.crabs) do
        if c.tx == P[1] and c.tz == P[2] and not CR.stunned(S, F, c.key) and not c.smashable
            and c.slot.at ~= nil and c.slot.moved_at == nil then
            local nb = CR.beside(S, F, P)
            if nb ~= nil then
                CR.walk_to(S, F, B, nb[1], nb[2])
                L.moved_at, L.s_at = F.tick, F.tick
                if cr.smash_kind == "dwh" and not QD.raid._tob_worn(S, S.ids.dwh) then intents.gear = { S.ids.dwh } end
                intents.mode = "park-hold" .. P[1] .. "," .. P[2]
                return
            end
        end
    end
    -- stage q short of Q too long (an orb keeps Q forbidden -- a free crab
    -- on the sculpture's doorstep turns every orb onto it, relay r15 raid1
    -- stood there 3,300 ticks): that approach is tried, start over
    if L.ap ~= nil and L.stage == "q" and F.tick - L.since > CR.V.STALL_Q
        and (cr.me.x ~= L.ap.q[1] or cr.me.z ~= L.ap.q[2]) then
        L.tried[L.ap.a] = true
        local n = 0
        for _ in pairs(L.tried) do n = n + 1 end
        if n >= #(CR.approach(S, P[1], P[2]) or {}) then L.tried = {} end
        L.ap, L.stage = nil, "s"
        intents.mode = "park-stallq"
        return
    end
    -- a free crab near me to lead? (not one still frozen: it cannot follow)
    local free, fd = nil, nil
    for _, c in ipairs(cr.crabs) do
        if not CR.stunned(S, F, c.key) and not c.maybe then
            local d = math.max(math.abs(c.x - cr.me.x), math.abs(c.z - cr.me.z))
            if fd == nil or d < fd then free, fd = c, d end
        end
    end
    if free == nil then
        -- (waiting for a stun to run out is not being stuck)
        L.s_at, L.moved_at = F.tick, F.tick
        intents.mode = "park-none"
        return
    end
    -- WAKE FIRST (a step with two or more crabs to park): a free crab far
    -- off that has not been near me lately wanders and never follows --
    -- fetch it before the first lead, not after the first stun is spent
    -- (committed: the one being fetched is walked to until it has been
    -- within 3 of me -- a wandering crab drifts back past 6 and back)
    if wake and L.stage ~= "q" then
        local W = cr.waking
        if W ~= nil and (W.slot.near_at == F.tick or F.tick - W.since > 40) then
            cr.waking, W = nil, nil
        end
        if W == nil then
            for _, c in ipairs(cr.crabs) do
                if not CR.stunned(S, F, c.key) and not c.maybe
                    and math.max(math.abs(c.x - cr.me.x), math.abs(c.z - cr.me.z)) > 6
                    and (c.slot.near_at == nil or F.tick - c.slot.near_at > 40) then
                    W = { slot = c.slot, since = F.tick }
                    cr.waking = W
                    break
                end
            end
        end
        if W ~= nil then
            for _, c in ipairs(cr.crabs) do
                if c.slot == W.slot then
                    CR.walk_to(S, F, B, c.x, c.z, CR.V.PULL)
                    L.stage = "s"
                    L.moved_at = F.tick
                    intents.mode = "park-wake"
                    return
                end
            end
            cr.waking = nil
        end
    end
    -- (a crab following me lags while I run: fetch only one that is not)
    if fd > 5 and L.stage ~= "q" then
        -- go and wake it: a tile beside it
        CR.walk_to(S, F, B, free.x, free.z, CR.V.PULL)
        L.stage = "s"
        return
    end
    if L.ap == nil then intents.mode = "park-noapproach" return end
    if L.stage == "s" then
        -- (dragging the crabs off the orb's line: two tiles away from the
        -- one that stands on it, then back to S with them behind me)
        if L.drag ~= nil then
            if F.tick > L.drag.untl or (cr.me.x == L.drag.x and cr.me.z == L.drag.z) then
                L.drag = nil
            else
                CR.walk_to(S, F, B, L.drag.x, L.drag.z)
                intents.mode = "park-drag"
                return
            end
        end
        CR.walk_to(S, F, B, L.ap.s[1], L.ap.s[2])
        if cr.me.x == L.ap.s[1] and cr.me.z == L.ap.s[2] then
            -- (go only when no orb crosses P, P - a or Q in the walk's time:
            -- else the planner pulls me off Q as the crab arrives)
            local clear = true
            local a = L.ap.a
            for _, t in ipairs({ { P[1], P[2] }, { P[1] - CR.DX[a], P[2] - CR.DZ[a] }, L.ap.q }) do
                local d = cr.danger[CR.key(t[1], t[2])]
                if d ~= nil and d <= 5 then clear = false end
            end
            -- (and a crab has caught up with me)
            if clear and fd <= 2 then L.stage = "q"; L.since = F.tick end
            -- NOT CLEAR, A FREE CRAB BESIDE ME ON THE ORB'S LINE: it turns
            -- every orb across P (cr18, cr131: at (16,8) for (14,8) a crab
            -- on (16,9) sent each newborn orb down x=15 for 300+ ticks).
            -- Drag it off: two tiles away from it, then back.
            if not clear and F.tick - (L.drag_at or -99) > 12 then
                local T = CR.T[cr.room.variant]
                local base = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, CR.held(S, F), 60)
                local line = {}
                for _, t in ipairs(base.tiles) do line[CR.key(t[1], t[2])] = true end
                for _, c in ipairs(cr.crabs) do
                    if not CR.stunned(S, F, c.key) and line[c.tkey]
                        and math.max(math.abs(c.tx - cr.me.x), math.abs(c.tz - cr.me.z)) <= 1 then
                        local dx = (cr.me.x > c.tx and 1) or (cr.me.x < c.tx and -1) or 0
                        local dz = (cr.me.z > c.tz and 1) or (cr.me.z < c.tz and -1) or 0
                        for n = 2, 1, -1 do
                            local x, z = cr.me.x + n * dx, cr.me.z + n * dz
                            local k = CR.key(x, z)
                            if cr.reach[k] and not line[k] and L.drag == nil then
                                L.drag = { x = x, z = z, untl = F.tick + 6 }
                            end
                        end
                        if L.drag ~= nil then
                            L.drag_at, L.s_at = F.tick, F.tick
                            intents.mode = "park-drag"
                            return
                        end
                    end
                end
            end
        end
        return
    end
    -- stage q: through P to Q; a crab trailing me stops on P (the smash
    -- weapon on as I go)
    CR.walk_to(S, F, B, L.ap.q[1], L.ap.q[2])
    if cr.smash_kind == "dwh" and not QD.raid._tob_worn(S, S.ids.dwh) then intents.gear = { S.ids.dwh } end
    if cr.me.x == L.ap.q[1] and cr.me.z == L.ap.q[2] and F.tick - L.since > 6 then
        -- arrived and nothing came onto P: start over (a crab on P inside
        -- its immunity is waited for, here)
        local onp = false
        for _, c in ipairs(cr.crabs) do if c.tx == P[1] and c.tz == P[2] then onp = true end end
        if not onp or F.tick - L.since > 6 + CR.V.IMMUNE + 2 then L.stage = "s" end
    end
end

-- The chain built: wait beside the first crab, off the orb's path, and paint
-- it at the right moment.
function CR.fire(S, F, B, intents, ci, plan)
    local cr, V, ids = S.cr, CR.V, S.ids
    -- the crab to paint: the chain's coloured one (else the first; all dim)
    local c1 = plan.crabs[1]
    for _, c in ipairs(plan.crabs) do if c[3] ~= -1 then c1 = c break end end
    local k1 = CR.key(c1[1], c1[2])
    local want = c1[3]
    intents.mode = "fire" .. ci
    -- the planned path: the held crabs and the chain, no free crab (a free
    -- crab is wherever I lead it)
    local held = CR.held(S, F)
    local m = {}
    for k, c in pairs(held) do m[k] = c end
    for _, c in ipairs(plan.crabs) do m[CR.key(c[1], c[2])] = c[3] end
    m[k1] = want
    local T = CR.T[cr.room.variant]
    local planned = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, m, 80)
    local onpath = {}
    for _, t in ipairs(planned.tiles) do onpath[CR.key(t[1], t[2])] = true end
    onpath[CR.key(T.focus[1], T.focus[2])] = true
    -- THE STANDING TILE: off the path, and every tile beside it too (a free
    -- crab stands beside me); beside c1 when the paint is a smash, within six
    -- of it when it is a spell or a bolt; for a dim chain, near.
    -- (a stand where a free crab, following me, stops on the path and
    -- breaks the chain is given up after 8 ticks: cr33, a crab cornered on
    -- (12,15) by the one I held beside me aborted every orb for 1,000 ticks)
    if cr.stand_for ~= k1 then cr.bad_stand = {} end
    if cr.stand ~= nil and cr.me.x == cr.stand[1] and cr.me.z == cr.stand[2] then
        local live = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, CR.crab_map(S, { [k1] = want }), 80)
        local home = planned.stop == "crystal" and planned.crystal == ci
        if home and not (live.stop == "crystal" and live.crystal == ci) then
            cr.fire_block = (cr.fire_block or 0) + 1
        else
            cr.fire_block = 0
        end
        if cr.fire_block >= 8 then
            cr.bad_stand[CR.key(cr.stand[1], cr.stand[2])] = true
            cr.stand, cr.fire_block = nil, 0
            intents.mode = "fire" .. ci .. "-restand"
        end
    end
    if cr.stand == nil or cr.stand_for ~= k1 then
        cr.stand, cr.stand_for = nil, k1
        local best, bs = nil, nil
        for dx = -6, 6 do
            for dz = -6, 6 do
                local sx, sz = c1[1] + dx, c1[2] + dz
                local sk = CR.key(sx, sz)
                local cheb = math.max(math.abs(dx), math.abs(dz))
                local ok = cr.reach[sk] and not onpath[sk] and not m[sk] and cheb >= 1 and not cr.bad_stand[sk]
                if want == 1 then ok = ok and (math.abs(dx) + math.abs(dz) == 1) end
                if ok then
                    -- (the path beside me is where a crab following me
                    -- stops: the 4-neighbours most, the ring of 2 too --
                    -- a bolt or a spell reaches six, so stand off)
                    local bad = 0
                    for a = 0, 3 do
                        if onpath[CR.key(sx + CR.DX[a], sz + CR.DZ[a])] then bad = bad + 1 end
                    end
                    local ring = 0
                    if want ~= 1 then
                        for ex = -2, 2 do
                            for ez = -2, 2 do
                                if math.max(math.abs(ex), math.abs(ez)) >= 1 and onpath[CR.key(sx + ex, sz + ez)] then
                                    ring = ring + 1
                                end
                            end
                        end
                    end
                    local score = bad * 100 + ring * 20 + cheb + math.abs(sx - cr.me.x) * 0.1 + math.abs(sz - cr.me.z) * 0.1
                    if bs == nil or score < bs then best, bs = { sx, sz }, score end
                end
            end
        end
        cr.stand = best
    end
    if cr.stand then CR.walk_to(S, F, B, cr.stand[1], cr.stand[2], V.PULL / 2) end
    -- a free crab beside me, off the path: hold it there
    if cr.stand and cr.me.x == cr.stand[1] and cr.me.z == cr.stand[2] then
        for _, c in ipairs(cr.crabs) do
            if not CR.stunned(S, F, c.key) and not onpath[c.key] and cr.prev_keys[c.key] and c.smashable
                and math.abs(c.x - cr.me.x) + math.abs(c.z - cr.me.z) == 1 and c.key ~= k1 and CR.safe_here(S) then
                CR.smash(S, intents, c)
                return
            end
        end
    end
    -- paint when the live orb, given c1 painted, reaches the crystal and is
    -- PAINT_LEAD ticks short of the bounce
    if want == -1 or cr.orb == nil or cr.orb_dir == nil then return end
    local sim = CR.sim(S, cr.orb.x, cr.orb.z, cr.orb_dir, cr.orb.col, CR.crab_map(S, { [k1] = want }), 50)
    if sim.stop ~= "crystal" or sim.crystal ~= ci or sim.bounces == nil or sim.bounces[k1] == nil then return end
    local lead = sim.bounces[k1]
    local c1now = nil
    for _, c in ipairs(cr.crabs) do if c.key == k1 then c1now = c end end
    if c1now == nil then return end
    if c1now.col == want and (cr.painted[k1] or -100) + V.PAINT_TICKS - 1 > F.tick + lead then return end
    if lead < V.PAINT_LEAD_MIN or lead > V.PAINT_LEAD_MAX then return end
    if not CR.safe_here(S) then return end
    -- (only where the op needs no step: beside it for a smash, within six for
    -- a spell or a bolt -- an op that walks me crosses the path)
    local adx, adz = math.abs(c1[1] - cr.me.x), math.abs(c1[2] - cr.me.z)
    if want == 1 and adx + adz ~= 1 then return end
    if want ~= 1 and math.max(adx, adz) > 6 then return end
    if want == 1 then
        if not CR.smash(S, intents, c1now) then return end
    else
        local weapon = (want == 3) and ids.trident or ids.xbow
        if not QD.raid._tob_worn(S, weapon) then
            intents.gear = { weapon }
            return
        end
        intents.world_op = { kind = "npc", id = c1now.row.npc_id, op = 2, x = c1now.row.x, z = c1now.row.z }
    end
    cr.painted[k1] = F.tick
end

-- THE SMASH (op3): a wielded blunt weapon or a plain hammer in the pack
-- (cox_crabs.rs2 ~cox_crab_is_smash_weapon). The relay's kit carries the
-- dragon warhammer: it is WIELDED for the smash (one-handed: the defender
-- stays, the tentacle takes its pack slot). -> true when the op went in.
function CR.smash(S, intents, c)
    local cr, ids = S.cr, S.ids
    if cr.smash_kind == "dwh" and not QD.raid._tob_worn(S, ids.dwh) then
        intents.gear = { ids.dwh }
        intents.mode = (intents.mode or "") .. "-dwh"
        return false
    end
    intents.world_op = { kind = "npc", id = c.row.npc_id, op = 3, x = c.row.x, z = c.row.z }
    return true
end

-- How this kit smashes: "hammer" (one in the pack), "worn" (a blunt weapon
-- wielded), "dwh" (the dragon warhammer in the pack, wielded to smash), or
-- nil (none: the room's spawn is taken).
function CR.smash_kind(S)
    local ids = S.ids
    local ir, n = api_drive.inv_count(ids.inv, ids.hammer)
    if ir == "ok" and n > 0 then return "hammer" end
    if QD.raid._tob_worn(S, ids.dwh) or QD.raid._tob_worn(S, ids.elder) then return "worn" end
    local dr, dn = api_drive.inv_count(ids.inv, ids.dwh)
    if dr == "ok" and dn > 0 then return "dwh" end
    return nil
end

-- Does the room agree with the variant's table? A loc on every crystal tile, every chain
-- CR.sim-checked to its crystal on the surveyed map, every chain tile one a
-- crab can be led onto. -> true, or false and the reason
function CR.table_check(S, F)
    local cr = S.cr
    local P = CR.PLAN[cr.room.variant]
    if P == nil then return false, "no table" end
    if P.off then return false, P.off end
    local T = CR.T[cr.room.variant]
    -- (no spawn-tile test: the crabs wander while the raid plays the rooms
    -- before, and one may be out of view -- round 3's check sent 6 of 7
    -- relay entries to the search, r15)
    local lr, locs = api_drive.locs(48)
    if lr ~= "ok" then return false, "no locs" end
    for ci, c in ipairs(T.crystals) do
        local wx, wz = QD.COX.world(cr.room, c[1], c[2])
        local seen = false
        for _, l in ipairs(locs) do if l.x == wx and l.z == wz then seen = true break end end
        if not seen then return false, "no crystal loc at " .. c[1] .. "," .. c[2] .. " (c" .. ci .. ")" end
    end
    for i, st in ipairs(P.steps) do
        local m = {}
        for _, c in ipairs(st.crabs) do
            m[CR.key(c[1], c[2])] = c[3]
            if CR.approach(S, c[1], c[2]) == nil then
                return false, string.format("step %d: no approach to %d,%d", i, c[1], c[2])
            end
        end
        local sim = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, m, 80)
        if not (sim.stop == "crystal" and sim.crystal == st.ci and sim.colour == CR.NEED[st.ci]) then
            return false, string.format("step %d: the orb %s", i, tostring(sim.stop))
        end
    end
    return true, "ok"
end

-- THE TABLE PATH: the first unlit step; its missing crabs parked last-first;
-- all held -> fire (CR.fire stands off the live path and paints only when the
-- live orb, as the crabs stand, comes home; a held crab off the table that
-- turns the orb first is waited out there).
function CR.table_strategy(S, F, B, intents)
    local cr = S.cr
    local P = CR.PLAN[cr.room.variant]
    local step = nil
    for i, st in ipairs(P.steps) do
        if not cr.lit[st.ci] then step = st cr.step_i = i break end
    end
    if step == nil then return end
    cr.target = step.ci
    local plan = { crabs = step.crabs, steps = 0 }
    cr.cur = { ci = step.ci, plan = plan, sig = "table" }
    local held = CR.held(S, F)
    -- (the park order: the step's own, else last-first -- until the first
    -- crab of a chain stands, the orb keeps its old line)
    local order = step.park
    if order == nil then
        order = {}
        for i = #step.crabs, 1, -1 do order[#order + 1] = i end
    end
    local missing = 0
    for _, c in ipairs(step.crabs) do
        if held[CR.key(c[1], c[2])] == nil then missing = missing + 1 end
    end
    for _, i in ipairs(order) do
        local c = step.crabs[i]
        if held[CR.key(c[1], c[2])] == nil then
            CR.park(S, F, B, intents, c, missing >= 2)
            intents.mode = "t" .. cr.step_i .. "-" .. intents.mode
            return
        end
    end
    cr.lure = nil
    CR.fire(S, F, B, intents, step.ci, plan)
    intents.mode = "t" .. cr.step_i .. "-" .. intents.mode
end

function CR.strategy(S, F, B)
    local cr = S.cr
    local intents = { mode = "crabs" }
    if cr.room == nil then
        local room, inside = QD.COX.lookup_room(F, "crabs", cr)
        if room == nil then
            intents.mode = "no-room"
            return intents
        end
        if not inside then
            intents.mode = "walk-in"
            B.pull({ name = "in", x = room.ox + 16, z = room.oz + 16, size = 3, weight = 10, t0 = F.tick + 1, t1 = F.tick + B.h })
            return intents
        end
        cr.room = room
        cr.t0 = F.tick
    end
    -- THE PRAYER BILL (reported by the test): points lost, ticks lit
    if cr.pray_prev ~= nil and (F.prayer or 0) < cr.pray_prev then
        cr.pray_spent = (cr.pray_spent or 0) + cr.pray_prev - (F.prayer or 0)
    end
    cr.pray_prev = F.prayer or 0
    for _, on in pairs(F.lit or {}) do
        if on then cr.pray_lit = (cr.pray_lit or 0) + 1 break end
    end
    cr.path = cr.path or "search"
    if not cr.planned then
        intents.mode = "survey"
        if cr.first_crabs == nil then
            -- (the crabs where they stand on entry: the table's spawn check)
            local first = {}
            for _, row in ipairs(F.npcs) do
                if S.ids.crab_colour[row.npc_id] ~= nil then
                    local lx, lz = QD.COX.template(cr.room, row.x, row.z)
                    if lx >= 0 and lx < 32 and lz >= 0 and lz < 32 then first[#first + 1] = { lx, lz } end
                end
            end
            if #first > 0 then cr.first_crabs = first end
        end
        CR.survey(S, F)
        return intents
    end
    if cr.seen_tick ~= F.tick then CR.see(S, F) end
    if cr.table_ok == nil then
        local ok, why = CR.table_check(S, F)
        cr.table_ok = ok
        cr.path = ok and "table" or ("search(" .. why .. ")")
        api_drive.ticklog_mark(string.format("crabs table: %s v%d t%d (%s)", ok and "on" or "off",
            cr.room.variant, cr.room.turn, why))
    end
    -- A HAMMER (the raid kit has none): the room's own spawn, op Take
    -- (Synq: "there's a hammer spawn in this room")
    if not cr.armed then
        local kind = CR.smash_kind(S)
        if kind ~= nil then
            cr.armed = true
            cr.smash_kind = kind
        else
            CR.take_hammer(S, F, B, intents)
            return intents
        end
    end
    -- PROTECT FROM MELEE only while a crab can swing at me (attackrange 1): a
    -- free crab within 2 tiles (it closes in a tick), a stunned one beside
    -- me (a stunned crab still swings, RC-S2); OFF once none is within 3 /
    -- beside me (round 2 never turned it off: 425 lit ticks, 45-60 points)
    local near_free, near_held = 99, 99
    for _, c in ipairs(cr.crabs) do
        local d = math.max(math.abs(c.tx - cr.me.x), math.abs(c.tz - cr.me.z))
        if CR.stunned(S, F, c.key) then
            near_held = math.min(near_held, d)
        else
            near_free = math.min(near_free, d)
        end
    end
    if near_free <= 2 or near_held <= 1 then
        intents.pray = "protectfrommelee"
    elseif F.lit and F.lit.protectfrommelee and near_free > 3 then
        intents.pray_off = { "protectfrommelee" }
    end
    -- THE CRAB LOG: one mark a tick (crabs: tile colour, s = stunned age)
    if api_drive.ticklog_mark ~= nil then
        local parts = {}
        for _, c in ipairs(cr.crabs) do
            local st = cr.stun[c.key]
            parts[#parts + 1] = string.format("%d,%d/%d%s", c.x, c.z, c.col, st and ("s" .. (F.tick - st.at)) or "")
        end
        local o = cr.orb
        api_drive.ticklog_mark(string.format("cr t%d me %d,%d crabs %s orb %s hits %d%s", F.tick, cr.me.x, cr.me.z,
            table.concat(parts, " "), o and string.format("%d,%d/%d d%s", o.x, o.z, o.col, tostring(cr.orb_dir)) or "-",
            cr.nhits or -1, cr.hit_text or ""))
    end
    -- SOLVED: put the room's hammer down (raiders leave it; the pack's slot
    -- is wanted for the grubs and the scavengers' drops)
    if cr.lit[1] and cr.lit[2] and cr.lit[3] and cr.lit[4] then
        intents.mode = "solved"
        intents.pray = nil
        intents.pray_off = nil
        for name, on in pairs(F.lit or {}) do
            if on and S.ids.prayers[name] then
                intents.pray_off = intents.pray_off or {}
                intents.pray_off[#intents.pray_off + 1] = name
            end
        end
        -- (only the room's: a hammer brought in -- the relay kit's, for the
        -- storage unit -- stays; QD.COX.may_drop)
        local r, n = api_drive.inv_count(S.ids.inv, S.ids.hammer)
        if r == "ok" and n > 0 and QD.COX.may_drop("hammer") then
            intents.drop = S.ids.hammer
        elseif CR.retake(S, F, intents) then
            intents.mode = "solved-retake"
        elseif not CR.base_gear_on(S) then
            -- (and the raid's base gear back on: the next room starts from it)
            intents.mode = "solved-gear"
            intents.gear = { S.ids.tentacle, S.ids.defender }
        end
        return intents
    end
    if cr.table_ok then
        CR.table_strategy(S, F, B, intents)
        return intents
    end
    -- THE PLAN for the next crystal, against the crabs held now (a held
    -- crab turns the orb whether I want it or not): re-made when a held crab
    -- is lost or a new one appears off the plan; one crystal a tick
    local held, sig = CR.held(S, F)
    local cur = cr.cur
    if cur ~= nil then
        if cr.lit[cur.ci] then cur = nil end
    end
    if cur ~= nil then
        for _, c in ipairs(cur.plan.crabs) do
            if c.held and held[CR.key(c[1], c[2])] == nil then cur = nil break end
        end
    end
    if cur ~= nil and cur.sig ~= sig then
        -- the held crabs changed: does the plan still bring the orb home?
        if not CR.plan_holds(S, cur, held) then cur = nil end
        if cur ~= nil then cur.sig = sig end
    end
    cr.cur = cur
    if cur == nil then
        intents.mode = "replan"
        local rp = cr.rp
        if rp == nil or rp.sig ~= sig then
            local free = 0
            for _, c in ipairs(cr.crabs) do if not CR.stunned(S, F, c.key) then free = free + 1 end end
            rp = { sig = sig, i = 0, best = nil, free = free }
            cr.rp = rp
        end
        if rp.job == nil then rp.i = rp.i + 1 end
        if rp.i <= 4 then
            if not cr.lit[rp.i] then
                rp.job = rp.job or {}
                local p = CR.plan_crystal(S, rp.i, held, rp.job)
                if p == "more" then return intents end
                rp.job = nil
                if p ~= nil then
                    -- (a plan I can build with the crabs free now first)
                    local score = (p.new > rp.free and 1000 or 0) + p.new * 100 + p.steps
                    if rp.best == nil or score < rp.best.score then
                        rp.best = { ci = rp.i, plan = p, score = score }
                    end
                end
            end
            return intents
        end
        cr.rp = nil
        if rp.best == nil then
            intents.mode = "noplan"
            cr.target = nil
            return intents
        end
        cr.cur = { ci = rp.best.ci, plan = rp.best.plan, sig = sig }
        cur = cr.cur
    end
    cr.target = cur.ci
    local plan = cur.plan
    -- every new chain tile held by a stunned crab? Parked LAST first: until
    -- the first crab of the chain stands, the orb keeps its old line and does
    -- not cross the tiles the others are led to
    for i = #plan.crabs, 1, -1 do
        local c = plan.crabs[i]
        local k = CR.key(c[1], c[2])
        if held[k] == nil then
            CR.park(S, F, B, intents, c)
            return intents
        end
    end
    cr.lure = nil
    cur.firing = true
    CR.fire(S, F, B, intents, cur.ci, plan)
    return intents
end

-- Does `cur`'s chain, with every crab held now, still take an orb from the
-- focus to its crystal in its colour?
function CR.plan_holds(S, cur, held)
    local cr = S.cr
    local T = CR.T[cr.room.variant]
    local m = {}
    for k, c in pairs(held) do m[k] = c end
    for _, c in ipairs(cur.plan.crabs) do m[CR.key(c[1], c[2])] = c[3] end
    local sim = CR.sim(S, T.focus[1], T.focus[2], T.head, 0, m, 80)
    return sim.stop == "crystal" and sim.crystal == cur.ci and sim.colour == CR.NEED[cur.ci]
end

-- The potion put down for the hammer, taken back from the floor of this room
-- (Take by its tile, re-pressed every 8 ticks); given up when it is gone (a
-- drop lies 300 ticks) or after 40 ticks. -> true while taking it
function CR.retake(S, F, intents)
    return QD.COX.retake_lent(S, F, intents, S.cr.room, S.cr.lent)
end

-- The raid's base weapons worn (the tentacle, the dragon defender)?
function CR.base_gear_on(S)
    return QD.raid._tob_worn(S, S.ids.tentacle) and QD.raid._tob_worn(S, S.ids.defender)
end

-- The pack's least-needed dose, to make room (Synq: "Drop a potion if you
-- need to"): the stamina first (the energy wells refill run), then an empty
-- vial, then a revitalisation, then a xeric's aid (the scavengers' juice
-- fills the kit's free slot; a potion put down is taken back after the
-- hammer, while it lies there). -> obj id or nil
function CR.spare(S)
    return QD.COX.spare(S)
end

-- Take the room's hammer: a free slot first (the pack the raid brings has one,
-- and the scavengers' drops fill it), then Take on the spawn IN THIS ROOM by
-- its tile (an op on the type alone may pick a copy anywhere), re-pressed
-- every 8 ticks (the executive sends an op again only when the intention
-- changes).
function CR.take_hammer(S, F, B, intents)
    local cr = S.cr
    intents.mode = "hammer"
    if QD.T2.exec.free_slots(S) == 0 then
        local spare, lend = CR.spare(S)
        if spare ~= nil then
            intents.mode = "hammer-room"
            intents.drop = spare
            cr.lent = cr.lent or lend
        else
            intents.mode = "hammer-full"
        end
        return
    end
    local lr, rows = api_drive.locs(40)
    local spawn = nil
    if lr == "ok" then
        for _, row in ipairs(rows) do
            if (row.resolved_loc_id or row.loc_id) == S.ids.hammer_spawn
                and row.x >= cr.room.ox and row.x < cr.room.ox + 32 and row.z >= cr.room.oz and row.z < cr.room.oz + 32 then
                spawn = row
            end
        end
    end
    if spawn == nil then
        intents.mode = "hammer-unseen"
        return
    end
    cr.hammer_t0 = cr.hammer_t0 or F.tick
    if (F.tick - cr.hammer_t0) % 8 == 7 then
        intents.mode = "hammer-gap"
        return
    end
    intents.world_op = { kind = "loc", id = spawn.loc_id, op = 1, x = spawn.x, z = spawn.z }
end

-- The crabs held now (stunned, with time left to be used): key -> colour
-- (dim: a smash's red is gone before the orb comes), and their signature.
function CR.held(S, F)
    local cr = S.cr
    local held, keys = {}, {}
    for _, c in ipairs(cr.crabs) do
        local st = cr.stun[c.key]
        if st ~= nil and F.tick - st.at < CR.V.HOLD_FOR then
            held[c.key] = -1
            keys[#keys + 1] = c.key
        end
    end
    table.sort(keys)
    return held, table.concat(keys, ",")
end

QD.T2.register("cox_crabs", {
    ids = function(opts) return CR.ids(opts) end,

    init = function(S, opts)
        S.who = "cox_crabs_solve"
        S.t2.plan_opts = { h = (opts and opts.h) or CR.V.H, beam = (opts and opts.beam) or CR.V.BEAM }
        S.cr = { room = nil, reach = {}, walk = {}, crystal_at = {}, survey_i = 0, planned = false, plans = {},
                 crabs = {}, orb = nil, orb_dir = nil, orb_last = nil, stun = {}, slots = {}, danger = {}, lit = {}, painted = {},
                 lure = nil, stand = nil, target = nil, orb_hits = 0 }
    end,

    -- Auto Retaliate off (the combat tab's toggle): a swing back at a crab
    -- paints it, and a chain's dim crab must stay dim
    enter = function(S)
        local ids = S.ids
        for press = 1, 3 do
            local vr, v = api_drive.varp(ids.nodef)
            if vr ~= "ok" or v == 1 then break end
            QD.raid._tob_tab(S, ids.combat_tab)
            api_drive.if_click(ids.retaliate, 1)
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": retaliate" }, 3)
            end
        end
        QD.raid._tob_tab(S, ids.inv_tab)
    end,

    predict = function(S, F, B) CR.predict(S, F, B) end,

    strategy = function(S, F, B) return CR.strategy(S, F, B) end,

    done = function(S, F)
        local cr = S.cr
        if cr.no_room ~= nil then return "no_room" end
        if cr.planned and cr.lit[1] and cr.lit[2] and cr.lit[3] and cr.lit[4] then
            cr.solved_at = cr.solved_at or F.tick
            -- (and the room's hammer put down: the pack's slot is the
            -- thieving grubs' and the scavengers' drops')
            local r, n = api_drive.inv_count(S.ids.inv, S.ids.hammer)
            local room_hammer = r == "ok" and n > 0 and QD.COX.may_drop("hammer")
            local lit = false
            for name, on in pairs(F.lit or {}) do if on and S.ids.prayers[name] then lit = true end end
            if F.tick - cr.solved_at >= 3 and not room_hammer and CR.base_gear_on(S) and not lit
                and (cr.lent == nil or cr.lent.done) then
                if not cr.end_marked and api_drive.ticklog_mark ~= nil then
                    cr.end_marked = true
                    api_drive.ticklog_mark(string.format("crabs end v%d t%d ticks %d orb_hits %d prayer %d path %s",
                        cr.room.variant, cr.room.turn, cr.solved_at - (cr.t0 or cr.solved_at), cr.orb_hits,
                        cr.pray_spent or 0, tostring(cr.path)))
                end
                return "ok"
            end
        end
        return nil
    end,

    summary = function(S)
        local cr = S.cr
        if cr.no_room ~= nil then return "crabs: " .. cr.no_room end
        local plans = {}
        for ci = 1, 4 do
            local p = (cr.cur and cr.cur.ci == ci) and cr.cur.plan or nil
            local s = "none"
            if p then
                local t = {}
                for _, c in ipairs(p.crabs) do t[#t + 1] = string.format("%d,%d/%d", c[1], c[2], c[3]) end
                s = table.concat(t, ";") .. " s" .. p.steps
            end
            plans[#plans + 1] = string.format("c%d[%s]%s", ci, s, cr.lit[ci] and "*" or "")
        end
        return string.format("crabs: v%s t%s, orb hits %d, plans %s, target %s", tostring(cr.room and cr.room.variant),
            tostring(cr.room and cr.room.turn), cr.orb_hits, table.concat(plans, " "), tostring(cr.target))
    end,
})

-- Probe helpers (tests): the room funnels and the client's route.
function QD.raid.cox_room_world(room, lx, lz) return QD.COX.world(room, lx, lz) end
function QD.raid.cox_room_template(room, x, z) return QD.COX.template(room, x, z) end
function QD.raid.cox_route(x, z) return api_drive.route(x, z) end
