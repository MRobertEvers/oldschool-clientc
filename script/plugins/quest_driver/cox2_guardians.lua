-- quest-driver / cox2_guardians: THE GUARDIANS, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/guardians_solver.md is the plan,
-- docs/minigames/cox/rooms/guardians_spec.md the mechanics. A QD.T2 room
-- (tob2_run.lua): PREDICT and STRATEGY here; the planner, the executive, the
-- monitor and the supplies are the shared layers.
--
-- THE IDEA (the guides' flinch: Synq 0:31:12, wiki CoX/Strategies, CA Dancing
-- with Statues). A statue never moves and only swings at a raider orthogonally
-- beside its 2x2 at the end of the tick before. My hit landing on it (the npc
-- phase after my swing) pushes its next swing two ticks on, so: arrive beside
-- it on the tick the pickaxe is ready, swing, and be two tiles from that tile
-- by the end of the next tick (the rock, if one comes, lands on the tile I
-- swung from). Being beside it at any other tick end is FORBIDDEN; so is the
-- other statue's side and the exit's knockback tiles while either stands.
--
-- Perception: npc rows (the statues 7569/7570 and their demolished forms
-- 7571/7572, their seqs: 1203 cleave, 4278 stomp), the rock's projectile 645,
-- my own swings (the monitor's stamp). The room's template (QD.COX.room_at)
-- gives the exit tiles.
-- ==========================================================================

QD.COXG = QD.COXG or {}

QD.COXG.V = {
    H = 10, BEAM = 32,
    STATUE = { 7569, 7570 }, DEAD = { 7571, 7572 }, SIZE = 2,
    ATTACK_RATE = 4, FLINCH = 2, PICK_SPEED = 5,
    SWING_VALUE = 6,
    EAT_BELOW = 55, BREW_BELOW = 55,
    -- the knockback's blocked tiles, template room-local, by variant
    -- (CCW, THRU, CW): guardians_spec.md Geometry
    BLOCKED = { { { 11, 19 }, { 11, 20 } }, { { 14, 25 }, { 15, 25 } }, { { 18, 23 }, { 18, 22 } } },
}

function QD.COXG.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_guardians", opts)
    local room = QD.raid._tob_symbols("cox_guardians", {
        { "left", "npc", "raids_stoneguardians_left" }, { "right", "npc", "raids_stoneguardians_right" },
        { "left_dead", "npc", "raids_stoneguardians_left_dead" }, { "right_dead", "npc", "raids_stoneguardians_right_dead" },
        { "cleave", "seq", "dragon_halberd_special_attack" }, { "stomp", "seq", "emote_stampfeet" },
        { "rock", "spotanim", "motherlode_rockfall" },
        { "pickaxe", "obj", "dragon_pickaxe" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "iron_pickaxe", "obj", "iron_pickaxe" }, { "pick_loc", "loc", "raids_stoneguardians_pickaxe" },
        { "com_mode", "varp", "varp43_com_mode" }, { "smash", "component", "combat_interface:style_slot_2" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_guardians: no combat tab")
    ids.combat_tab = combat_tab
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_guardians: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    ids.brews = QD.COX.supply_ids("heal")
    ids.restores = QD.COX.supply_ids("restore")
    ids.stat_restores = ids.restores
    ids.overloads = QD.COX.supply_ids("overload")
    ids.combats, ids.rangings = {}, {}
    -- every pickaxe, best first (the damage term is the pickaxe's Mining
    -- requirement, crystal capped at dragon's: guardians_spec.md #12)
    ids.picks = {}
    for _, name in ipairs({ "dragon_pickaxe", "rune_pickaxe", "adamant_pickaxe", "mithril_pickaxe", "black_pickaxe",
                            "steel_pickaxe", "iron_pickaxe", "bronze_pickaxe" }) do
        local r, id = api_drive.symbol("obj", name)
        assert(r == "ok", "cox_guardians: no obj " .. name)
        ids.picks[#ids.picks + 1] = { id = id, name = name }
    end
    return ids
end

-- The best pickaxe I hold or wear: { id, name } or nil.
function QD.COXG.best_pick(S)
    for _, p in ipairs(S.ids.picks) do
        if QD.raid._tob_worn(S, p.id) or QD.raid._tob_count(S, p.id) > 0 then return p end
    end
    return nil
end

-- The guardians room: the one I stand in, or the one ahead of it (the whole
-- raid and ::coxtest both leave me past the room before's Passage, which can
-- still be that room's cell). nil when neither is.
function QD.COXG.find_room()
    local here = QD.COX.here()
    if here == nil then return nil end
    if here.name == "guardians" then return here end
    local _, _, cx, cz = QD.COX.next_origin(here)
    local ahead = QD.COX.room_at(cx, cz, here.level)
    if ahead and ahead.name == "guardians" then return ahead end
    return nil
end

-- The 2x2 footprint's 4-neighbourhood box (the tiles a swing reads; the
-- corners are not reach but are never worth standing on either).
function QD.COXG.box(row)
    return row.x - 1, row.z - 1, row.x + QD.COXG.V.SIZE, row.z + QD.COXG.V.SIZE
end

-- Orthogonally beside the 2x2 at (row.x, row.z)?
function QD.COXG.beside(row, x, z)
    local n = QD.COXG.V.SIZE
    local dx, dz = x - row.x, z - row.z
    if dx >= 0 and dx < n and (dz == -1 or dz == n) then return true end
    if dz >= 0 and dz < n and (dx == -1 or dx == n) then return true end
    return false
end

-- ================================================================ OBSERVE

function QD.COXG.observe(S, F)
    local g, V, ids = S.g, QD.COXG.V, S.ids
    g.statues, g.dead = {}, {}
    for _, row in ipairs(F.npcs) do
        for i = 1, 2 do
            if row.npc_id == V.STATUE[i] then g.statues[i] = row end
            if row.npc_id == V.DEAD[i] then g.dead[i] = row end
        end
    end
    if g.room == nil then
        for i = 1, 2 do
            local row = g.statues[i] or g.dead[i]
            if row and g.room == nil then
                local _, me = api_drive.player_tile()
                g.room = QD.COX.room_at(row.x, row.z, me.level)
            end
        end
    end
    -- the statues' swings, first seen (a seq start a tick: its seq_tick)
    for i = 1, 2 do
        local row = g.statues[i]
        local st = g.st[i]
        if row then
            local seq, stick = row.seq_id, row.seq_tick
            if seq ~= nil and stick ~= nil and stick ~= st.seq_tick and (seq == ids.cleave or seq == ids.stomp) then
                st.seq_tick = stick
                QD.COXG.swing_seen(S, F, i, seq == ids.stomp and "stomp" or "cleave")
            end
            -- my hits landing on it (its splats): the flinch
            for _, sp in ipairs(row.splats or {}) do
                local key = tostring(sp.start)
                if not st.splats[key] then
                    st.splats[key] = true
                    if F.tick - st.flinch_at >= V.ATTACK_RATE then st.flinch_at = F.tick end
                    st.hits = st.hits + 1
                end
            end
        end
    end
    -- the rocks: where and when
    local pr, projs = api_drive.projectiles(0)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == ids.rock then
                local key = p.dst_x .. ":" .. p.dst_z .. ":" .. F.tick
                if not g.rocks_seen[p.dst_x .. ":" .. p.dst_z .. ":" .. (F.tick - 1)] then
                    g.rocks_seen[key] = true
                    g.rocks[#g.rocks + 1] = { x = p.dst_x, z = p.dst_z, land = F.tick + 1 }
                end
            end
        end
    end
end

-- A statue's swing seen on tick d: the monitor's clock check (the OSRS order:
-- it swings at d only if I stood beside it at the end of d-1, and only once
-- its clock -- the last swing + 4, a flinch + 2 -- allows).
function QD.COXG.swing_seen(S, F, i, kind)
    local g, V = S.g, QD.COXG.V
    local st = g.st[i]
    local d = F.tick
    local ready = math.max(st.last_swing + V.ATTACK_RATE, st.flinch_at + V.FLINCH)
    local was = QD.T2.tile_at(S, d - 1)
    local R = S.mon.rules.guard_swing
    R.checked = R.checked + 1
    if d < ready then
        QD.T2.monitor.note(S, "guard_swing", d, string.format("statue %d %s at t%d before its clock t%d (swing %d, flinch %d)",
            i, kind, d, ready, st.last_swing, st.flinch_at))
    end
    local row = g.statues[i]
    if was and row and not QD.COXG.beside(row, was.x, was.z) then
        QD.T2.monitor.note(S, "guard_swing", d, string.format("statue %d %s at t%d with me at %d,%d not beside it",
            i, kind, d, was.x, was.z))
    end
    st.last_swing = d
    g.swings[kind] = g.swings[kind] + 1
end

-- ================================================================ PREDICT

-- The rocks in the air: a 3x3 on the tile, read at the end of the swing tick
-- (already seen by now), so there is nothing to plan -- the monitor's threat
-- rule gets them. The forbids are the plan.
function QD.COXG.predict(S, F, B)
    local g = S.g
    for _, r in ipairs(g.rocks) do
        if r.land >= F.tick + 1 then
            B.hit({ name = "rock", x0 = r.x - 1, z0 = r.z - 1, x1 = r.x + 1, z1 = r.z + 1,
                    t0 = r.land - 1, t1 = r.land - 1, dmg_exp = 7.5, dmg_max = 15 })
        end
    end
end

-- ================================================================ STRATEGY

function QD.COXG.target_index(g)
    if g.statues[1] then return 1 end
    if g.statues[2] then return 2 end
    return nil
end

function QD.COXG.strategy(S, F, B)
    local g, V, ids = S.g, QD.COXG.V, S.ids
    local d = F.tick
    local intents = { mode = "idle", eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    -- my swing: the strike tile, and the rock-dodge it owes on the next tick end
    if F.my_swing then
        g.strike = { t = F.my_swing_stamp or d, tile = QD.T2.tile_at(S, F.my_swing_stamp or d) or { x = F.me.x, z = F.me.z } }
        g.strikes = g.strikes + 1
    end
    -- the pickaxe in hand first (one backpack op), then everything else
    if not QD.raid._tob_worn(S, g.pick_id) then
        intents.gear = { { obj = g.pick_id, two_hand = false } }
    end
    -- AN ADD (something that followed me in -- the whole raid's first probe
    -- had scavengers chase in from the room before): within three tiles, it
    -- is the target, with the pickaxe, before the statues
    local add = nil
    for _, row in ipairs(F.npcs) do
        local own = false
        for i = 1, 2 do if row.npc_id == V.STATUE[i] or row.npc_id == V.DEAD[i] then own = true end end
        if not own and (row.health_ratio == nil or row.health_ratio > 0) and math.max(math.abs(row.x - F.me.x), math.abs(row.z - F.me.z)) <= 3 then
            add = row
        end
    end
    if add and QD.raid._tob_worn(S, g.pick_id) then
        if g.add_key ~= QD.T2.npc_key(add) then
            g.add_key = QD.T2.npc_key(add)
            g.adds = (g.adds or 0) + 1
        end
        B.target({ name = "add", npc = add, size = add.size or 1, range = 1, speed = V.PICK_SPEED, value = V.SWING_VALUE * 2 })
    end
    local ti = QD.COXG.target_index(g)
    -- the exit's knockback tiles while either stands: forbidden, always
    if g.room and (g.statues[1] or g.statues[2]) then
        for k, t in ipairs(V.BLOCKED[g.room.variant + 1]) do
            local x, z = QD.COX.world(g.room, t[1], t[2])
            B.hit({ name = "push" .. k, x0 = x - 1, z0 = z - 1, x1 = x + 1, z1 = z + 1, t0 = d + 1, t1 = d + B.h,
                    forbid = true, dmg_exp = 22, dmg_max = 45 })
        end
    end
    if ti == nil then
        -- (once, 30 ticks in with no statue row: what the client does list)
        g.unseen = (g.unseen or 0) + 1
        if g.unseen == 30 then
            local seen = {}
            for _, row in ipairs(F.npcs) do seen[#seen + 1] = row.npc_id .. "@" .. row.x .. "," .. row.z end
            api_core.log("guardians: no statue row 30 ticks in; me " .. F.me.x .. "," .. F.me.z
                .. " centre " .. tostring(g.centre and (g.centre.x .. "," .. g.centre.z)) .. " npcs " .. table.concat(seen, " ") .. " t" .. d)
        end
        -- not in sight yet (rows come within the client's view): into the room
        if g.dead[1] == nil and g.dead[2] == nil and g.centre then
            B.pull({ name = "centre", x = g.centre.x - 1, z = g.centre.z - 1, size = 3, weight = 1, t0 = d + 1, t1 = d + B.h })
            intents.mode = "enter"
        else
            intents.mode = "done"
        end
        return intents
    end
    local row = g.statues[ti]
    local other = g.statues[3 - ti]
    -- the other statue's side: never (it aggroes whoever stands there, and a
    -- hit on its partner turns it on me)
    if other then
        local x0, z0, x1, z1 = QD.COXG.box(other)
        B.hit({ name = "partner", x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = d + 1, t1 = d + B.h, forbid = true,
                dmg_exp = 10, dmg_max = 20 })
    end
    -- THE STRIKE: the pickaxe's next swing W; beside the statue only then
    local W = QD.T2.exec.plan_state(S, F, B).swing_ready
    local x0, z0, x1, z1 = QD.COXG.box(row)
    local from = d + 1
    if g.strike and g.strike.t + 1 > from then from = g.strike.t + 1 end
    if W - 1 >= from then
        B.hit({ name = "beside", x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = from, t1 = W - 1, forbid = true,
                dmg_exp = 10, dmg_max = 20 })
    end
    -- after a swing: two tiles off the swing tile by the end of the next tick
    if g.strike and g.strike.t + 1 >= d + 1 then
        B.hit({ name = "dodge", x0 = g.strike.tile.x - 1, z0 = g.strike.tile.z - 1, x1 = g.strike.tile.x + 1,
                z1 = g.strike.tile.z + 1, t0 = g.strike.t + 1, t1 = g.strike.t + 1, forbid = true, dmg_exp = 7.5, dmg_max = 15 })
    end
    -- far off (the room's way in is ~20 tiles from the statues): toward it
    -- (a plan's horizon does not reach a swing from there, so nothing else
    -- moves me)
    if math.max(math.abs(F.me.x - row.x), math.abs(F.me.z - row.z)) > 5 then
        B.pull({ name = "approach", x = row.x - 2, z = row.z - 2, size = 6, weight = 1, t0 = d + 1, t1 = d + B.h })
    end
    if QD.raid._tob_worn(S, g.pick_id) then
        B.target({ name = "statue" .. ti, npc = row, size = V.SIZE, range = 1, speed = V.PICK_SPEED,
                   value = V.SWING_VALUE, t_ok0 = math.max(W, d + 1) })
        intents.mode = "flinch" .. ti
    else
        intents.mode = "wield"
    end
    g.seen_n = (g.seen_n or 0) + 1
    if g.seen_n == 30 and g.strikes == 0 then
        api_core.log("guardians: no strike 30 ticks after the statues showed; me " .. F.me.x .. "," .. F.me.z
            .. " statue" .. ti .. " " .. row.x .. "," .. row.z .. " room " .. tostring(g.room and g.room.name) .. " v"
            .. tostring(g.room and g.room.variant) .. " origin " .. tostring(g.room and (g.room.ox .. "," .. g.room.oz))
            .. " mode " .. intents.mode .. " W " .. tostring(W) .. " t" .. d)
    end
    -- prayers: Protect from Melee (it halves the cleave) and Piety
    if not F.lit["protectfrommelee"] and (F.prayer or 0) > 0 then intents.pray = "protectfrommelee" end
    intents.pray_boost = ((F.prayer or 0) >= 20) and "piety" or nil
    QD.COXG.drinks(S, F, intents)
    return intents
end

-- The overload (+) once at the start (five hits of 10 as it boosts), at 85+.
function QD.COXG.drinks(S, F, intents)
    local g, ids = S.g, S.ids
    local n = 0
    for i, obj in ipairs(ids.overloads) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    if g.ovl_n ~= nil and n < g.ovl_n then g.ovl_until = F.tick + 501 end
    g.ovl_n = n
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    if (g.ovl_until or -1) <= F.tick and F.hp >= 85 then
        local pick = QD.raid._tob_first(S, ids.overloads)
        if pick then
            intents.drink = pick
            S.last_drink = F.tick
        end
    end
end

-- ================================================================ THE ROOM

QD.T2.register("cox_guardians", {
    ids = function(opts) return QD.COXG.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_guardians_solve"
        S.max_ticks = opts.max_ticks or 1500
        S.t2.plan_opts = { h = opts.h or QD.COXG.V.H, beam = opts.beam or QD.COXG.V.BEAM }
        S.g = { st = {}, statues = {}, dead = {}, rocks = {}, rocks_seen = {}, room = nil,
                swings = { cleave = 0, stomp = 0 }, strikes = 0, strike = nil, style = nil }
        for i = 1, 2 do
            S.g.st[i] = { seq_tick = nil, last_swing = -100, flinch_at = -100, splats = {}, hits = 0 }
        end
        S.mon.rules.guard_swing = { n = 0, checked = 0, traced = 0 }
        -- the room from where I stand (the whole raid passes nothing; a room
        -- test's ::coxtest centre is only a fallback)
        local room = QD.COXG.find_room()
        if room then
            S.g.centre = { x = room.ox + 16, z = room.oz + 16 }
        elseif opts.centre then
            S.g.centre = { x = opts.centre.cx, z = opts.centre.cz }
        end
        -- the overload's own shocks (human_killerwatt_electricshock) are not
        -- my swings: counted as one they reset the weapon clock
        local kr, ks = api_drive.symbol("seq", "human_killerwatt_electricshock")
        assert(kr == "ok", "cox_guardians: no seq human_killerwatt_electricshock")
        S.mon.not_swing[ks] = true
    end,

    -- The pickaxe in hand and SMASH (crush -10 vs slash +180; the wiki's
    -- "set the pickaxe to a crushing option"), before the loop.
    enter = function(S)
        local ids = S.ids
        -- into the room: the client's route to its middle (the way in is
        -- ~20 tiles and a turn or two from the statues; the plan's horizon is
        -- ten ticks of straight steps)
        if S.g.centre then
            local wr, wd = QD.raid.cox_walk_near(S.g.centre.x, S.g.centre.z, 40)
            QD.raid._tob_trace(S, api_drive.tick(), "guardians walk in: " .. tostring(wr) .. " " .. tostring(wd))
        end
        -- then to the statues by the client's route: in some turns the room's
        -- middle is in a wall pocket and the route to it ends where I stand,
        -- and the statues are a loop round the west wall away -- past the
        -- plan's ten straight steps (r35s raid4: 4000 ticks on 6446,85, the
        -- statues 8 tiles off). The route to a statue's own tile arrives
        -- beside it, wherever the way in goes.
        local nr, npcs = api_drive.npcs(0)
        local statue = nil
        for _, row in ipairs((nr == "ok" and npcs) or {}) do
            if row.npc_id == QD.COXG.V.STATUE[1] or row.npc_id == QD.COXG.V.STATUE[2] then statue = statue or row end
        end
        if statue then
            local _, me = api_drive.player_tile()
            if math.max(math.abs(me.x - statue.x), math.abs(me.z - statue.z)) > 4 then
                local wr, wd = QD.raid.cox_walk_near(statue.x, statue.z, 60)
                QD.raid._tob_trace(S, api_drive.tick(), "guardians walk to the statues: " .. tostring(wr) .. " " .. tostring(wd))
            end
        end
        -- THE PICKAXE: the best one I hold; with none, the room's own iron
        -- pickaxe (raids_stoneguardians_pickaxe, Take: guardians_spec.md #17
        -- -- "a permanent iron pickaxe spawn ... next to the guardians")
        local pick = QD.COXG.best_pick(S)
        if pick == nil then
            local tr, at = QD.raid.tob_loc_op("raids_stoneguardians_pickaxe", 1, nil, 40)
            for _ = 1, 30 do
                if QD.raid._tob_count(S, ids.iron_pickaxe) > 0 then break end
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": pickaxe" }, 3)
            end
            pick = QD.COXG.best_pick(S)
            QD.raid._tob_trace(S, api_drive.tick(), "guardians took the room's pickaxe: " .. tostring(tr)
                .. " -> " .. tostring(pick and pick.name))
            assert(pick, S.who .. ": no pickaxe held and the room's Take gave none (" .. tostring(tr) .. ")")
        end
        S.g.pick_id, S.g.pick_name = pick.id, pick.name
        if not QD.raid._tob_worn(S, pick.id) then
            QD.player.equip(pick.name, { quick = true })
        end
        -- (the pickaxe's styles are Spike, Impale, SMASH, Block: slot 2,
        -- com_mode 2. Pressed by slot: scriptrun has no clientscripts to name
        -- the buttons, and QD.ui.style reads the names.)
        for press = 1, 4 do
            local mr, mode = api_drive.varp(ids.com_mode)
            assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
            S.g.style = "com_mode " .. tostring(mode)
            if mode == 2 then break end
            assert(press <= 3, S.who .. ": three style presses and com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.smash, 1)
            assert(pr == "ok", S.who .. ": the Smash press answered " .. tostring(pr) .. " " .. tostring(pd))
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": style" }, 3)
            end
        end
        QD.raid._tob_trace(S, api_drive.tick(), "guardians style: " .. S.g.style .. ", pickaxe " .. S.g.pick_name)
        QD.raid._tob_tab(S, ids.inv_tab)
    end,

    predict = function(S, F, B)
        QD.COXG.observe(S, F)
        QD.COXG.predict(S, F, B)
    end,

    strategy = function(S, F, B)
        local intents = QD.COXG.strategy(S, F, B)
        S.t2.want.boost = intents.pray_boost
        intents.pray_boost = nil
        return intents
    end,

    done = function(S, F)
        local g = S.g
        if g.statues[1] == nil and g.statues[2] == nil and g.dead[1] and g.dead[2] then return "ok" end
        return nil
    end,

    summary = function(S)
        local g = S.g
        return string.format("guardians: pickaxe %s, adds fought %d, strikes %d, statue swings cleave %d stomp %d, rocks %d, hits landed %d/%d, style %s, swing mismatches %d/%d",
            tostring(g.pick_name), g.adds or 0, g.strikes, g.swings.cleave, g.swings.stomp, #g.rocks, g.st[1].hits, g.st[2].hits, tostring(g.style),
            S.mon.rules.guard_swing.n, S.mon.rules.guard_swing.checked)
    end,
})
