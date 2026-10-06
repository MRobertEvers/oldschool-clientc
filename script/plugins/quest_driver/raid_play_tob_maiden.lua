-- quest-driver / raid_play_tob_maiden: the Maiden of Sugadinti plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files).
-- Written by raid seam30 play_tob_maiden: the Entry solo plan, from the sources
-- line by line.  PLAY_NOTES.md "Maiden" is its strategy table; each decision
-- below names its source.
--   W   docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Strategies.wikitext
--   ET  docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md
--   K   test/raids/tob_maiden.lua (the kept Entry room) and its kept run
--       build/quest_gate/tob_maiden (ledger + ticklog.tsv)

-- The twisted bow's row for the library (raid_play.lua QD.RAID_PLAY_WEAPONS
-- has only the scythe): K's kept run shows player_anim seq 426 every 6 ticks
-- (ticklog t212 218 224 ..., the bow on its accurate style).  Guarded, so a
-- second room part that needs the bow cannot collide at load.
if QD.RAID_PLAY_WEAPONS.twisted_bow == nil then
    QD.RAID_PLAY_WEAPONS.twisted_bow = { speed = 6, seqs = { [426] = true } }
end

QD.raid._play_plan("tob_maiden", {
    room = "maiden",
    boss = { entry = "tob_maiden_100_story", normal = "tob_maiden_100", hard = "tob_maiden_100_hard" },
    -- her four forms (W:592 "her appearance visibly changes" at 70/50/30;
    -- K boss_symbols): the npc row changes type, so the plan follows it
    forms = {
        entry = { "tob_maiden_100_story", "tob_maiden_70_story", "tob_maiden_50_story", "tob_maiden_30_story" },
        normal = { "tob_maiden_100", "tob_maiden_70", "tob_maiden_50", "tob_maiden_30" },
        hard = { "tob_maiden_100_hard", "tob_maiden_70_hard", "tob_maiden_50_hard", "tob_maiden_30_hard" },
    },
    crab = { entry = "maiden_elemental_story", normal = "maiden_elemental", hard = "maiden_elemental_hard" },
    slug = { entry = "maiden_blood_slug_story", normal = "maiden_blood_slug", hard = "maiden_blood_slug_hard" },
    -- spec maiden.cad / maiden.first (grade B): first attack tick 9, every 10.
    attack_first = 9, attack_every = 10,
    -- ET 2.1: seq 8091 blood throw (projectile 1578, pool graphic 1579),
    -- seq 8092 blackstorm (projectile 1577); trails are loc 32984 (ET 2.4).
    seq_blood = 8091, seq_storm = 8092, proj_blood = 1578, trail_loc = 32984,
    -- the pools are the library's markers (v.shadows): graphic 1579 alone
    -- (K: "pools are the 1579 graphic alone since seam9 (no loc under them)")
    shadow_lo = 1579, shadow_hi = 1579,
    -- blackstorm impact 5 ticks after the aim (K spec auto_impact_offset 5)
    storm_impact = 5,
    -- "two additional blood splats thrown randomly in a 5x5 area centred
    -- around them" (W:596): a tile 3 from the aimed tile is out of reach.
    scatter = 2, dodge = 3,
    -- "The Maiden cannot use this attack again for the next two attacks
    -- following the blood splats being thrown" (W:595; K spec blood_cooldown 2)
    blood_cooldown = 2,
    -- Geometry (absolute; the instance keeps the room's tiles, K's kept run):
    -- her footprint 6426..6431 x 92..97 (npc_spawn 6426,92, size 6); the
    -- floor K dodged on, 6432..6446 x 84..104; the bow reaches 10 tiles
    -- (the twisted bow's range), so a stand tile has x <= 6441; home is the
    -- middle of that strip, level with her.
    floor = { 6432, 84, 6446, 104 }, reach_x = 6441, home = { 6437, 94 },
    -- Matomenos: freeze them with Ice Barrage ("Ice Barrage is essentially
    -- mandatory", W:594), in the magic set K measured at +140 magic attack,
    -- the bonus at which every cast froze (K spec freeze_full_bonus, ET 2.3
    -- "hitting 100 % at +140"); back to the ranged set in one block after.
    magic_set = { "kodai_wand", "ancestral_hat", "ancestral_robe_top", "ancestral_robe_bottom", "arcane" },
    ranged_set = { "twisted_bow", "masori_mask", "masori_body", "masori_chaps" },
    freeze_spell = "ice_barrage", freeze_level = 94, cast_every = 5,
    flick_weapon = "abyssal_whip",
    modes = {
        -- Entry: the pool hits 10 (K spec pool_damage_entry), a protected
        -- blackstorm 9-14 (K tech.protect_magic, auto_prayed_entry), an
        -- unprotected one 18 (K auto_max_entry).
        -- `prove_protect`, `presteps`, `flicks`: the kept room's technique rows
        -- (tech.protect_magic, tech.sidestep_scan, tech.bow_flick, copied into
        -- test/raids/_play_maiden.lua unchanged) are MEASUREMENTS a play has
        -- to make on purpose: one blackstorm taken unprotected before Protect
        -- from Magic goes up (W:590 itself says pray from the start), a few
        -- steps on her attack tick (W:594 "players in melee distance will need
        -- to move before she attacks"; a ranger "can react to it"), and the
        -- whip put on after her aim until one drain is seen (W:591).
        -- `storm` is the largest blackstorm a plan must survive: the protected
        -- hit grows with every Matomenos that reached her (36.5 + 3.5c, W:590)
        -- and s30 run mz30a took 27 through the prayer at c = 6 (t256)
        entry = { storm = 28, storm_raw = 30, pool = 12, prove_protect = true, presteps = 12, flicks = 12 },
        normal = { storm = 25, storm_raw = 50, pool = 20, prove_protect = false, presteps = 0, flicks = 0 },
        hard = { storm = 25, storm_raw = 50, pool = 20, prove_protect = false, presteps = 0, flicks = 0 },
    },
    walk_prayers = { "protectfrommagic" },
    down_prayers = {},
    decide = "_play_maiden_decide",
})

-- SEE, the room's part (what a person at the screen sees beyond the
-- library's read): every npc in one pool read -- her current form, the
-- Matomenos and the blood spawns; the blood projectiles in flight (their
-- destination tiles: the splat under the player and the two extras, W:595-596);
-- the trails a blood spawn leaves (loc 32984, ET 2.4).  The pools themselves
-- are the library's markers (graphic 1579).
function QD.raid._play_maiden_ids(st)
    local P = st.plan
    local ids = { boss = {}, crab = {}, slug = {} }
    for _, sym in ipairs(P.forms[st.mode]) do
        local r, id = api_drive.symbol("npc", sym)
        if r == "ok" then ids.boss[id] = sym end
    end
    local cr, cid = api_drive.symbol("npc", P.crab[st.mode])
    if cr == "ok" then ids.crab[cid] = P.crab[st.mode] end
    local sr, sid = api_drive.symbol("npc", P.slug[st.mode])
    if sr == "ok" then ids.slug[sid] = P.slug[st.mode] end
    return ids
end

function QD.raid._play_maiden_see(st, v)
    local P, m = st.plan, st.m
    v.crabs, v.slugs = {}, {}
    local nr, rows = api_drive.npcs(0)
    if nr == "ok" then
        for _, row in ipairs(rows) do
            -- a row with no health bar yet reads -1/-1 (s30 mz30b t107): alive;
            -- an empty bar (0) is a dead one
            local alive = row.health_ratio == nil or row.health_ratio ~= 0
            local bsym = m.ids.boss[row.npc_id] or m.ids.boss[row.base_npc_id]
            if bsym ~= nil then
                -- her form changed (W:592): the library's reads follow the new symbol
                if st.boss_symbol ~= bsym then
                    st.boss_symbol = bsym
                    m.forms[#m.forms + 1] = { tick = v.tick, symbol = bsym }
                end
                v.boss = row
            elseif (m.ids.crab[row.npc_id] or m.ids.crab[row.base_npc_id]) and alive then
                v.crabs[#v.crabs + 1] = row
            elseif (m.ids.slug[row.npc_id] or m.ids.slug[row.base_npc_id]) and alive then
                v.slugs[#v.slugs + 1] = row
            end
        end
    end
    -- the blood in flight: every 1578 destination is a tile that will be a pool
    v.incoming = {}
    local pr, projs = QD.world.projectiles(0)
    if pr == "ok" and type(projs) == "table" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == P.proj_blood then
                v.incoming[#v.incoming + 1] = { x = p.dst_x, z = p.dst_z, cycles = p.cycles_left }
            end
        end
    end
    -- the trails (ET 2.4: "Standing on a trail damages the player and heals
    -- the Maiden"; W:600) join the pools as marked tiles
    v.marks = {}
    for k, on in pairs(v.shadows) do v.marks[k] = on end
    if api_drive.loc_copies ~= nil then
        local lr, locs = api_drive.loc_copies(P.trail_loc, 0)
        if lr == "ok" and type(locs) == "table" then
            for _, l in ipairs(locs) do v.marks[l.x * 100000 + l.z] = true end
        end
    end
    -- her attacks as the client draws them: a new seq on a new tick (the row's
    -- seq_tick, raid seam1) is one attack; a blood throw restarts the cooldown
    local b = v.boss
    if b ~= nil and (b.seq_id == P.seq_blood or b.seq_id == P.seq_storm) and b.seq_tick ~= m.last_attack then
        m.last_attack = b.seq_tick
        local blood = b.seq_id == P.seq_blood
        m.attacks[#m.attacks + 1] = { tick = b.seq_tick, blood = blood, seen = v.tick }
        if blood then
            m.autos_since = 0
            -- a throw on the tick after a step: the step the technique row reads
            if m.prestep_tick ~= nil and b.seq_tick == m.prestep_tick + 1 then m.prestep_seen = true end
        else
            m.autos_since = m.autos_since + 1
            if m.first_storm == nil then m.first_storm = b.seq_tick end
        end
    end
end

-- A block of the plan's own (a loadout swap: "a gear swap is one tick",
-- PLAY_NOTES "Loadouts"; the library's SEND has no gear list), counted into
-- the record's inputs like the library's own blocks.
function QD.raid._play_maiden_block(st, v, label, items, extra)
    local n = #items + (extra and 1 or 0)
    local r, d = QD.together(function()
        if extra ~= nil then QD.player.drink(extra) end
        for _, item in ipairs(items) do QD.player.equip(item) end
    end)
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + n
    st.blocks[r] = (st.blocks[r] or 0) + 1
    if extra ~= nil then
        st.last_drink = v.tick
        st.drinks[#st.drinks + 1] = { tick = v.tick, item = extra, hp = v.hp, prayer = v.prayer }
    end
    if r ~= "ok" and r ~= "split" then
        st.refusals = st.refusals + 1
        if #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " " .. label .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 160) end
    end
    st.m.swaps[#st.m.swaps + 1] = { tick = v.tick, label = label, result = r }
    return r
end

-- The first super restore dose carried (wiki Super restore; "You should
-- always repot ... if you ever get drained at maiden", the advanced guide
-- wiki_Guide_Advanced_Theatre_of_Blood.wikitext:111).
function QD.raid._play_maiden_restore()
    for _, name in ipairs(QD.RAID_PLAY_RESTORES) do
        local cr, n = QD.inv.count(name)
        if cr == "ok" and n > 0 then return name end
    end
    return nil
end

-- THE MAIDEN PLAN'S DECIDE (PLAY_NOTES.md "Maiden").
function QD.raid._play_maiden_decide(st, v)
    local P, N = st.plan, st.numbers
    if st.m == nil then
        st.m = { ids = QD.raid._play_maiden_ids(st), forms = {}, attacks = {}, autos_since = 99,
            last_attack = nil, first_storm = nil, move = nil, still = 0, presteps = 0, dodges = 0,
            far_moves = {}, flicks = {}, flick = nil, flicked = {}, drain_seen = false,
            fz = nil, fz_cast = {}, casts = {}, last_cast = -1000, fz_tick = -1000, waves = 0,
            swaps = {}, add_slot = nil, add_press = -1000, add_presses = 0, prayer_on_tick = nil }
        st.far_moves = st.m.far_moves
        st.flicks = st.m.flicks
        st.casts = st.m.casts
    end
    local m = st.m
    QD.raid._play_maiden_see(st, v)
    -- every tile that is or will be blood: pools, trails, splats in flight
    for _, p in ipairs(v.incoming) do v.marks[p.x * 100000 + p.z] = true end
    v.shadows = v.marks
    local intent = { want = {}, walk = nil, attack = false }
    local me = v.me
    if m.prayer_on_tick == nil and v.lit.protectfrommagic == true then
        m.prayer_on_tick = v.tick
        st.prayer_on_tick = v.tick
    end
    -- PRAYER: Protect from Magic halves the blackstorm (W:590); it always
    -- lands, so it is up for the whole fight.  Entry's `prove_protect`
    -- holds it until her first blackstorm has landed (impact 5 ticks after
    -- the aim, K spec auto_impact_offset): the kept technique row compares
    -- that hit with the protected ones.
    local prayed = (not N.prove_protect) or (m.first_storm ~= nil and v.tick >= m.first_storm + P.storm_impact)
    if prayed then intent.want.protectfrommagic = true end
    local b = v.boss
    if b == nil then
        return intent
    end
    local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
    local function floor_ok(x, z)
        return x >= P.floor[1] and x <= P.floor[3] and z >= P.floor[2] and z <= P.floor[4]
    end
    local function near_blood(x, z, r)
        for _, p in ipairs(v.incoming) do
            if cheb(x, z, p.x, p.z) <= r then return true end
        end
        return false
    end
    -- A dodge tile: three from where she aimed (out of the 5x5 the extras
    -- land in, W:596), on the floor, in bow reach, on no blood and out of the
    -- 5x5 of any splat in flight; the run's first tick-end tile (two tiles,
    -- diagonal first: the library's measured route) on no pool either.  Of
    -- those, the one nearest home with the fewest marked neighbours.
    local function far_tile(fx, fz)
        local best, bx, bz = nil, nil, nil
        local d = P.dodge
        for dx = -d, d do
            for dz = -d, d do
                local x, z = fx + dx, fz + dz
                if cheb(x, z, fx, fz) == d and floor_ok(x, z) and x <= P.reach_x and not v.marks[x * 100000 + z]
                    and not near_blood(x, z, P.scatter) then
                    local sx = dx > 0 and 1 or (dx < 0 and -1 or 0)
                    local sz = dz > 0 and 1 or (dz < 0 and -1 or 0)
                    local mx, mz = fx + 2 * sx, fz + 2 * sz
                    if math.abs(dx) < 2 then mx = x end
                    if math.abs(dz) < 2 then mz = z end
                    if not v.marks[mx * 100000 + mz] then
                        local crowd = 0
                        for ax = -1, 1 do
                            for az = -1, 1 do
                                if v.marks[(x + ax) * 100000 + z + az] then crowd = crowd + 1 end
                            end
                        end
                        local score = cheb(x, z, P.home[1], P.home[2]) * 10 + crowd * 4
                        if best == nil or score < best then best, bx, bz = score, x, z end
                    end
                end
            end
        end
        return bx, bz
    end

    -- THE FLICK'S END: six ticks after her aim the blackstorm has landed
    -- (impact +5); the bow goes back on and the drain is read off the
    -- player's own stats (W:591 "drain a player's combat stats").
    if m.flick ~= nil and v.tick >= m.flick.a + 6 then
        local _, rl = QD.skill.read("ranged")
        local _, al = QD.skill.read("attack")
        local _, sl = QD.skill.read("strength")
        local f = m.flick
        local row = { a = f.a, equip_tick = f.equip_tick, dr = f.before.r - rl.level, da = f.before.a - al.level, ds = f.before.s - sl.level }
        m.flicks[#m.flicks + 1] = row
        if row.dr > 0 then m.drain_seen = true end
        QD.raid._play_maiden_block(st, v, "flick back", { "twisted_bow" })
        st.engaged = false
        m.flick = nil
    end

    -- MOVEMENT.  A walk in progress is left alone until it arrives (a press
    -- of Attack would cancel the rest of it, and the run's last tile is the
    -- one out of the 5x5), unless it stalls or its tile turns to blood.
    local here = me.x * 100000 + me.z
    if m.move ~= nil then
        if me.x == m.move.x and me.z == m.move.z then
            m.move = nil
        else
            if st.last_me ~= nil and st.last_me.x == me.x and st.last_me.z == me.z then m.still = m.still + 1 else m.still = 0 end
            if m.still >= 2 or v.marks[m.move.x * 100000 + m.move.z] or near_blood(m.move.x, m.move.z, P.scatter) then
                m.move = nil
            end
        end
    end
    local threatened = near_blood(me.x, me.z, P.scatter) or v.marks[here] == true
    local new_walk = nil
    if m.move == nil then
        local next_attack = m.last_attack ~= nil and (m.last_attack + P.attack_every) or nil
        if near_blood(me.x, me.z, P.scatter) then
            -- "Those standing away can react to it" (W:595): the throw is seen
            -- in flight, and three tiles clear the splat and both extras
            -- (W:596) before it lands (flight 50 cycles + 15 a tile, K spec
            -- blood_flight_base: two ticks or more).
            local fx, fz = far_tile(me.x, me.z)
            if fx ~= nil then
                new_walk = { x = fx, z = fz, why = "dodge" }
                m.far_moves[#m.far_moves + 1] = { tick = v.tick, fx = me.x, fz = me.z, kind = "dodge" }
            end
        elseif v.marks[here] then
            -- on a pool or a trail: "Never stand on a splat or a trail"
            -- (PLAY_NOTES; W:597, W:600), the nearest safe tile (library skill)
            local sx, sz = QD.raid._play_hazard(st, v, me.x, me.z, function(x, z) return floor_ok(x, z) and x <= P.reach_x end)
            if sx ~= me.x or sz ~= me.z then new_walk = { x = sx, z = sz, why = "pool" } end
        elseif m.presteps < N.presteps and not m.prestep_seen and next_attack ~= nil and v.tick == next_attack - 1
            and m.autos_since >= P.blood_cooldown and m.flick == nil then
            -- the step on the tick before her attack (the T-1 rule, ET 1.1;
            -- "players in melee distance will need to move before she
            -- attacks", W:595), only when a throw can come (W:595 cooldown)
            local fx, fz = far_tile(me.x, me.z)
            if fx ~= nil then
                new_walk = { x = fx, z = fz, why = "prestep" }
                m.presteps = m.presteps + 1
                m.prestep_tick = v.tick
                m.far_moves[#m.far_moves + 1] = { tick = v.tick, fx = me.x, fz = me.z, kind = "prestep" }
            end
        end
        if new_walk ~= nil then
            m.move = new_walk
            m.still = 0
            m.dodges = m.dodges + 1
            st.dodges = st.dodges + 1
            intent.walk = { x = new_walk.x, z = new_walk.z }
        end
    end
    local moving = m.move ~= nil

    -- THE MATOMENOS (W:592-594, W:637-643): when a wave is seen, the magic
    -- set goes on in one block, each nylocas is Ice Barraged once ("freeze
    -- ... on the first tick possible", W:639; a barrage every 5 ticks, the
    -- spell's cast speed), the ranged set comes back in one block, and any
    -- that is still standing frozen is shot ("if it is necessary to leave a
    -- single nylocas, the DPS roles should attack it", W:643).
    local _, mg = QD.skill.read("magic")
    local add = nil
    if m.fz == nil and #v.crabs > 0 and m.flick == nil then
        local restore = nil
        if mg.level < P.freeze_level and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY then restore = QD.raid._play_maiden_restore() end
        QD.raid._play_maiden_block(st, v, "magic set", P.magic_set, restore)
        m.fz, m.fz_tick, m.fz_cast = "magic", v.tick, {}
        m.waves = m.waves + 1
        st.engaged = false
    elseif m.fz == "magic" then
        local target = nil
        for _, c in ipairs(v.crabs) do
            if not m.fz_cast[c.slot] and (target == nil or cheb(c.x, c.z, me.x, me.z) < cheb(target.x, target.z, me.x, me.z)) then target = c end
        end
        if (target == nil and v.tick - m.last_cast >= 2) or v.tick - m.fz_tick > 40 then
            QD.raid._play_maiden_block(st, v, "ranged set", P.ranged_set)
            m.fz = "ranged"
            st.engaged = false
        elseif target ~= nil and not moving and v.tick - m.last_cast >= P.cast_every then
            local cr, cd = QD.player.cast(P.freeze_spell, P.crab[st.mode], 1, 2, { slot = target.slot, quick = true })
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
            m.last_cast = v.tick
            m.fz_cast[target.slot] = true
            m.casts[#m.casts + 1] = { tick = v.tick, slot = target.slot, result = tostring(cr), wave = m.waves, magic = mg.level }
            if cr ~= "ok" and #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " cast " .. tostring(cr) .. ": " .. string.sub(tostring(cd), 1, 120) end
        end
    elseif m.fz == "ranged" and #v.crabs == 0 then
        m.fz = nil
    end
    if m.fz == "ranged" and #v.crabs > 0 then
        add = v.crabs[1]
        for _, c in ipairs(v.crabs) do
            if cheb(c.x, c.z, b.x, b.z) < cheb(add.x, add.z, b.x, b.z) then add = c end
        end
        add = { row = add, symbol = P.crab[st.mode] }
    elseif m.fz == nil and #v.slugs > 0 then
        -- the blood spawns: "Kill or avoid" (PLAY_NOTES, W:598-600): they lay
        -- the trails, so the nearest is shot (Entry: one arrow, K slug hp)
        local s = v.slugs[1]
        for _, c in ipairs(v.slugs) do
            if cheb(c.x, c.z, me.x, me.z) < cheb(s.x, s.z, me.x, me.z) then s = c end
        end
        if cheb(s.x, s.z, me.x, me.z) <= 10 then add = { row = s, symbol = P.slug[st.mode] } end
    end

    -- THE FLICK (W:591; the advanced guide :92 "Bow flicking"): Entry's
    -- `flicks` puts the whip on after her blackstorm is aimed, until one
    -- drain has been seen, so the kept row tech.bow_flick can read where the
    -- drain went.  Never with blood about, a wave on, or the prayer down.
    if m.flick == nil and #m.flicks < N.flicks and not m.drain_seen and m.fz == nil and not moving and not threatened
        and m.prayer_on_tick ~= nil and b.seq_id == P.seq_storm and b.seq_tick == m.last_attack
        and v.tick <= m.last_attack + 2 and not m.flicked[m.last_attack] then
        local _, rl = QD.skill.read("ranged")
        local _, al = QD.skill.read("attack")
        local _, sl = QD.skill.read("strength")
        m.flicked[m.last_attack] = true
        QD.raid._play_maiden_block(st, v, "flick", { P.flick_weapon })
        local _, eq_tick = QD.tick()
        m.flick = { a = m.last_attack, equip_tick = eq_tick, before = { r = rl.level, a = al.level, s = sl.level } }
        st.engaged = false
    end

    -- ATTACK on cooldown (library skill): her, or the add the plan named;
    -- never mid-walk, with the whip on, or in the magic set.
    local may_attack = not moving and m.flick == nil and m.fz ~= "magic"
    if may_attack and add ~= nil then
        if m.add_slot ~= add.row.slot or v.tick - m.add_press >= st.weapon.speed + 2 then
            local ar = QD.player.attack(add.symbol, 2, 1, { quick = true, slot = add.row.slot })
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
            m.add_slot, m.add_press, m.add_presses = add.row.slot, v.tick, m.add_presses + 1
            if ar ~= "ok" and #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " add attack " .. tostring(ar) end
        end
        st.engaged = false
    elseif may_attack then
        m.add_slot = nil
        intent.attack = true
    end

    -- SUPPLIES (library skill): the most that can land in h ticks is her
    -- blackstorms on the 10-tick clock (impact 5 after each aim; protected
    -- or not), plus the pool's hit while blood is under or about the player.
    local function threat(h)
        local total = 0
        local storm = prayed and N.storm or N.storm_raw
        local base = m.last_attack or (st.start_tick - 1)
        for k = 0, 8 do
            local impact = base + k * P.attack_every + P.storm_impact
            if impact > v.tick and impact <= v.tick + h then total = total + storm end
        end
        if threatened then total = total + 2 * N.pool end
        return total
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    -- a drained Ranged (or Magic before a wave) is restored ("You should
    -- always repot ... if you ever get drained at maiden", advanced guide :111);
    -- never inside a flick, whose reading is the drain itself
    if intent.drink == nil and m.flick == nil and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY then
        local _, rg = QD.skill.read("ranged")
        if rg.level < 88 then intent.drink = QD.raid._play_maiden_restore() end
    end
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    return intent
end
