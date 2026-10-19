-- quest-driver / td_solver: ONE TORMENTED DEMON, SOLO, on the v2 layers.
--
-- docs/rsprox-analysis/tormented_demons/solver.md is the plan (the threat
-- field, the modes and the PASS row), README.md beside it the recorded
-- mechanics. A QD.T2 room (tob2_run.lua): PREDICT and STRATEGY here; the
-- planner, the executive, the monitor and the supplies are the shared layers.
--
-- THE IDEA (the wiki's Max Melee/Range): the emberlight while the demon prays
-- anything but melee, the scorching bow while it prays melee; it swaps to the
-- last style that hurt it every 150 damage, so the weapon follows its
-- headicon. Two recorded rules decide the prayer (solver.md section 1):
--
--   - my protect prayer is read when the demon SWINGS, so it must be right a
--     tick before, never in answer to the animation;
--   - the demon holds one style from an explosion (the engage, each bomb) to
--     the next, and a melee demon walks to me the tick after it, so standing
--     out of its reach on that tick tells melee from a thrower. A thrower's
--     first swing is a guess (the wiki's "guess and/or correct").
--
-- The fire bomb binds me for B .. B + 2 and lands on B + 4 on my tile at the
-- end of B + 3: both bomb tiles are forbidden then, and the step off goes out
-- of the demon's reach so the new segment tells.
--
-- Perception is only what the client is sent: the demon's npc row (tile,
-- seq and its start stamp, headicon, health bar), the bomb projectiles, my
-- own tile, stats, prayers and inventory.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}
QD.TD = QD.TD or {}
local TD = QD.TD

TD.V = {
    H = 10, BEAM = 32,
    SIZE = 3,
    -- the scorching bow (weapon_attackrange 10, attackrate 5) and the
    -- emberlight (attackrate 4). One combat style for both, style slot 2:
    -- the emberlight's Lunge, its only stab style (a slash sword: Chop, Slash,
    -- Lunge, Block; "Set to stab", wiki -- the demon's stab defence is 75, its
    -- slash 175), and the bow's Longrange (5 ticks, not Rapid's 4: the style
    -- varp is shared, and the bow is out only while it prays melee)
    BOW_RANGE = 10, MELEE_RANGE = 1, MELEE_SPEED = 4, BOW_SPEED = 5, STYLE = 2,
    SWING_VALUE = 3,
    -- the engage goes out from at least this gap, so a melee segment walks
    HOLD_OFF = 2,
    -- the bind holds my feet B .. B + 2; the bombs land on B + 4 and read my
    -- tile at the end of B + 3. After a bomb the demon walks off B + 1..B + 3
    -- and takes the fight back on B + 4 (docs/rsprox-analysis/tormented_demons,
    -- "The walk off"), so a bomb segment tells on B + 5
    BIND_LAST = 2, BOMB_READ = 3, BOMB_LAND = 4, BOMB_WALK = 4, BOMB_TELL = 5, BOMB_SWING = 6,
    -- sprite group 440's frames: the demon's protection prayer
    ICON_GROUP = 440, ICON = { [0] = "melee", [1] = "ranged", [2] = "magic" },
    EAT_BELOW = 60,
}

-- ======================================================================== IDS

function TD.ids(opts)
    local ids = QD.raid._tob_common_ids("tormented_demon", opts)
    local room = QD.raid._tob_symbols("tormented_demon", {
        { "td1", "npc", "tormented_demon_1" }, { "td2", "npc", "tormented_demon_2" },
        { "seq_melee", "seq", "luc2_undead_demon_melee" },
        { "seq_magic", "seq", "luc2_undead_demon_firey_balls" },
        { "seq_ranged", "seq", "luc2_undead_demon_spare_ribs" },
        { "seq_boom", "seq", "luc2_undead_demon_explosion_fire" },
        { "seq_death", "seq", "luc2_undead_demon_death" },
        { "bomb_proj", "spotanim", "luc2_undead_demon_fireball_large_proj" },
        { "emberlight", "obj", "emberlight" }, { "defender", "obj", "infernal_defender" },
        { "bow", "obj", "scorching_bow" },
        { "style_slot", "component", "combat_interface:style_slot_2" },
        { "com_mode", "varp", "varp43_com_mode" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "tormented_demon: no combat tab")
    ids.combat_tab = combat_tab
    ids.sets = {
        melee = { { ids.emberlight }, { ids.defender } },
        bow = { { ids.bow, true } },
    }
    ids.swing_style = { [ids.seq_melee] = "melee", [ids.seq_magic] = "magic", [ids.seq_ranged] = "ranged" }
    return ids
end

-- ================================================================ GEOMETRY

-- Chebyshev gap between a tile and the 3x3 (0 = under it, 1 = touching).
function TD.gap(x, z, nx, nz)
    local s = TD.V.SIZE
    return math.max(nx - x, x - (nx + s - 1), 0, nz - z, z - (nz + s - 1))
end

-- The demon's melee reach (td_combat.rs2 ~td_in_melee_reach): under the
-- footprint, or edge-on to it; never a corner.
function TD.in_reach(x, z, nx, nz)
    local s = TD.V.SIZE
    local dx, dz = x - nx, z - nz
    if dx >= 0 and dx < s and dz >= 0 and dz < s then return true end
    if dx >= 0 and dx < s and (dz == -1 or dz == s) then return true end
    if dz >= 0 and dz < s and (dx == -1 or dx == s) then return true end
    return false
end

-- The fewest steps the demon at nx, nz needs to have me in its melee reach
-- (a diagonal step moves both axes; a corner is not reach).
function TD.reach_moves(x, z, nx, nz)
    local s = TD.V.SIZE
    local best = nil
    for ax = x - s, x + 1 do
        for az = z - s, z + 1 do
            if TD.in_reach(x, z, ax, az) then
                local moves = math.max(math.abs(ax - nx), math.abs(az - nz))
                if best == nil or moves < best then best = moves end
            end
        end
    end
    return best
end

-- ================================================================ OBSERVE

-- The demon I fight: the nearest Tormented Demon until the engage, then
-- that npc (its element) until it dies.
function TD.find(S, F)
    local m, ids = S.m, S.ids
    local best, bd = nil, nil
    for _, row in ipairs(F.npcs) do
        if row.npc_id == ids.td1 or row.npc_id == ids.td2 then
            if m.key ~= nil then
                if QD.T2.npc_key(row) == m.key then return row end
            else
                local d = TD.gap(F.me.x, F.me.z, row.x, row.z)
                if best == nil or d < bd then best, bd = row, d end
            end
        end
    end
    return best
end

-- A new segment starts on the demon's explosion (first seen on F.tick):
-- the engage, or a fire bomb.
function TD.segment(S, F, row, kind)
    local m, V = S.m, TD.V
    local old = m.seg
    local tell = (kind == "engage") and (F.tick + 1) or (F.tick + V.BOMB_TELL)
    -- the guess for a thrower's first swing: Protect from Magic at the engage
    -- (the melee armour's weakest defence); after a bomb, the throw it did
    -- not just use ("whichever wasn't used before", wiki)
    local guess = "magic"
    if old and old.style == "magic" then guess = "ranged" end
    m.seg = { kind = kind, start = F.tick, tell = tell, tile = { x = row.x, z = row.z }, old = old and old.style,
              style = nil, by = nil, guess = guess, swings = 0 }
    m.segments = m.segments + 1
    QD.raid._tob_trace(S, F.tick, string.format("td %s at %d,%d (old %s)", kind, row.x, row.z, tostring(old and old.style)))
end

-- A swing first seen on F.tick (its tick T): which style, and whether my
-- overhead at the end of T (the launch read) was the right one.
function TD.swing(S, F, style)
    local m = S.m
    local seg = m.seg
    local lit = S.t2.overheads[F.tick]
    local right = (lit == QD.T2.PROTECT[style])
    m.swings = m.swings + 1
    if seg then
        local known = seg.style ~= nil
        if not right then
            if style == "melee" then m.wrong_melee = m.wrong_melee + 1 end
            if known then m.wrong_known = m.wrong_known + 1 else m.wrong_guess = m.wrong_guess + 1 end
        end
        if not known and style ~= "melee" then m.guessed = m.guessed + 1 end
        if seg.style ~= style then
            QD.raid._tob_trace(S, F.tick, string.format("td swing %s (segment %s by %s), overhead %s", style,
                tostring(seg.style), tostring(seg.by), tostring(lit)))
        end
        seg.style, seg.by = style, "swing"
        seg.swings = seg.swings + 1
    end
end

function TD.observe(S, F)
    local m, ids, V = S.m, S.ids, TD.V
    local row = TD.find(S, F)
    m.row = row
    if row == nil then
        if m.seg and not m.dead then
            m.dead = F.tick
            QD.raid._tob_trace(S, F.tick, "td gone from view")
        end
        return
    end
    m.prays = (row.headicon_group == V.ICON_GROUP) and V.ICON[row.headicon_index] or nil
    -- its sequences, each keyed on its start stamp, timed on the tick first seen
    if row.seq_id and row.seq_tick then
        local key = tostring(row.seq_id) .. ":" .. tostring(row.seq_tick)
        if m.seq_key ~= key then
            m.seq_key = key
            local sq = row.seq_id
            if sq == ids.seq_boom then
                if m.seg == nil then
                    m.key = QD.T2.npc_key(row)
                    m.engaged = F.tick
                    TD.segment(S, F, row, "engage")
                else
                    m.bomb = { t = F.tick, tiles = {}, n = 0 }
                    m.bombs = m.bombs + 1
                    -- the bound bomb's first tile is mine (I cannot have left it)
                    TD.bomb_tile(S, F.me.x, F.me.z)
                    S.x2.stunned_until = F.tick + V.BIND_LAST
                    TD.segment(S, F, row, "bomb")
                end
            elseif sq == ids.seq_death then
                m.dead = F.tick
                QD.raid._tob_trace(S, F.tick, "td death")
            elseif ids.swing_style[sq] then
                TD.swing(S, F, ids.swing_style[sq])
            end
        end
    end
    -- the walk off after a bomb is no tell: the segment's tile follows it
    local seg = m.seg
    if seg and seg.kind == "bomb" and F.tick <= seg.start + V.BOMB_WALK then
        seg.tile = { x = row.x, z = row.z }
    end
    -- the tell: a step after the explosion (after the walk off) is a melee
    -- segment -- after a bomb only if it can still be edge-on to me by its
    -- swing on B + 6: from further off a melee segment throws instead
    -- (td_combat.rs2 ~td_swing_style), and a thrower that walked off out of
    -- sight steps back in to throw too
    local reachable = true
    if seg and seg.kind == "bomb" then
        reachable = TD.reach_moves(F.me.x, F.me.z, row.x, row.z) <= seg.start + V.BOMB_SWING - F.tick
    end
    if seg and seg.style == nil and reachable and F.tick > seg.start
        and (row.x ~= seg.tile.x or row.z ~= seg.tile.z) then
        seg.style, seg.by = "melee", "walk"
        QD.raid._tob_trace(S, F.tick, string.format("td walks: melee (%s + %d)", seg.kind, F.tick - seg.start))
    end
    seg = m.seg
    if seg and seg.walk_at == nil and F.tick > seg.start and (row.x ~= seg.tile.x or row.z ~= seg.tile.z) then
        seg.walk_at = F.tick - seg.start
    end
    -- the bomb tiles: every fireball_large in flight while the bomb is fresh
    local b = m.bomb
    if b and F.tick <= b.t + V.BOMB_READ then
        local pr, projs = api_drive.projectiles(0)
        if pr == "ok" then
            for _, p in ipairs(projs) do
                if p.spotanim_id == ids.bomb_proj then TD.bomb_tile(S, p.dst_x, p.dst_z) end
            end
        end
    end
    -- the landing: hp lost on B + 4 with me on a bomb tile at the end of B + 3
    if b and F.tick == b.t + V.BOMB_LAND then
        local t3 = QD.T2.tile_at(S, b.t + V.BOMB_READ)
        if t3 and b.tiles[t3.x .. "," .. t3.z] and (m.last_hp or F.hp) > F.hp then
            m.bomb_hits = m.bomb_hits + 1
            QD.raid._tob_trace(S, F.tick, string.format("td bomb hit me at %d,%d", t3.x, t3.z))
        end
        local n = 0
        for _ in pairs(b.tiles) do n = n + 1 end
        QD.raid._tob_trace(S, F.tick, string.format("td bomb landed: %d tiles, me at %s", n,
            t3 and (t3.x .. "," .. t3.z) or "?"))
    end
    m.last_hp = F.hp
end

function TD.bomb_tile(S, x, z)
    local b = S.m.bomb
    local k = x .. "," .. z
    if b.tiles[k] == nil then
        b.tiles[k] = { x = x, z = z }
        b.n = b.n + 1
    end
end

-- The style the demon's next swing will have, for my overhead now. Unknown
-- before the tell (melee is half the rolls); a demon that stood still on the
-- tell tick while I was out of its reach is a thrower: the guess.
function TD.expect(S, F)
    local seg = S.m.seg
    if seg == nil then return nil end
    if seg.style then return seg.style end
    if F.tick >= seg.tell then
        local read = QD.T2.tile_at(S, seg.tell - 1)
        if read and not TD.in_reach(read.x, read.z, seg.tile.x, seg.tile.z) then return seg.guess end
    end
    return seg.old or "melee"
end

-- ================================================================ PREDICT

-- The bomb tiles at the ends of B + 3 and B + 4 (forbid); while a new
-- segment has not told yet, the demon's reach at the end of the tell's read
-- tick (forbid: standing there hides the walk that names melee); before the
-- engage, its reach too (the engage goes out from range).
function TD.predict(S, F, B)
    TD.observe(S, F)
    local m, V = S.m, TD.V
    local b = m.bomb
    if b and F.tick < b.t + V.BOMB_LAND then
        for _, t in pairs(b.tiles) do
            B.hit({ name = "bomb", x = t.x, z = t.z, size = 1, t0 = b.t + V.BOMB_READ, t1 = b.t + V.BOMB_LAND,
                    dmg_exp = 42, dmg_max = 45, forbid = true })
        end
    end
    local row = m.row
    if row == nil or m.dead then return end
    local hold = nil
    if m.seg == nil then
        hold = { t0 = F.tick + 1, t1 = F.tick + B.h }
    elseif m.seg.style == nil and F.tick < m.seg.tell then
        hold = { t0 = m.seg.tell - 1, t1 = m.seg.tell - 1 }
    end
    if hold and hold.t1 >= F.tick + 1 then
        local g = V.HOLD_OFF - 1
        B.hit({ name = "hold_off", x0 = row.x - g, z0 = row.z - g, x1 = row.x + V.SIZE - 1 + g,
                z1 = row.z + V.SIZE - 1 + g, t0 = math.max(hold.t0, F.tick + 1), t1 = hold.t1, forbid = true })
    end
end

-- =============================================================== STRATEGY

function TD.gear(S, set)
    local out = {}
    for _, e in ipairs(S.ids.sets[set]) do
        local obj = e[1]
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = e[2] == true }
        end
    end
    return out
end

function TD.mode(S, F)
    local m = S.m
    if m.dead then return "done" end
    if m.row == nil then return "find" end
    if m.seg == nil then return "engage" end
    if m.bomb and F.tick <= m.bomb.t + TD.V.BOMB_LAND then return "bomb" end
    return "fight"
end

function TD.strategy(S, F, B)
    local m, V, ids = S.m, TD.V, S.ids
    local mode = TD.mode(S, F)
    m.mode = mode
    local intents = { mode = mode, eat_below = V.EAT_BELOW }
    if mode == "done" or mode == "find" then
        S.t2.want.boost, S.t2.want.boost_stat = nil, nil
        intents.pray_off = {}
        for _, name in pairs(QD.T2.PROTECT) do
            if F.lit[name] then intents.pray_off[#intents.pray_off + 1] = name end
        end
        for _, name in ipairs({ "piety", "rigour" }) do
            if F.lit[name] then intents.pray_off[#intents.pray_off + 1] = name end
        end
        if #intents.pray_off == 0 then intents.pray_off = nil end
        return intents
    end
    -- the weapon: the bow for the engage (it prays the engaging style next)
    -- and while it prays melee, else the emberlight
    local set = "melee"
    if mode == "engage" or m.prays == "melee" then set = "bow" end
    intents.gear = TD.gear(S, set)
    if #intents.gear == 0 then intents.gear = nil end
    local weapon = (set == "bow") and ids.bow or ids.emberlight
    local row = m.row
    if QD.raid._tob_worn(S, weapon) then
        B.target({ name = "td", npc = row, size = V.SIZE, range = (set == "bow") and V.BOW_RANGE or V.MELEE_RANGE,
                   speed = (set == "bow") and V.BOW_SPEED or V.MELEE_SPEED, value = V.SWING_VALUE })
        S.t2.want.boost = (set == "bow") and "rigour" or "piety"
        S.t2.want.boost_stat = (set == "bow") and "ranged" or "attack"
    end
    -- the overhead: the style its next swing will have
    local want = TD.expect(S, F)
    if want then
        local prot = QD.T2.PROTECT[want]
        if not F.lit[prot] then intents.pray = prot end
        if want ~= m.last_want then
            QD.raid._tob_trace(S, F.tick, "td pray " .. want)
            m.last_want = want
        end
    end
    -- a boost not wanted goes off
    local off = {}
    for _, name in ipairs({ "piety", "rigour" }) do
        if F.lit[name] and S.t2.want.boost ~= name then off[#off + 1] = name end
    end
    if #off > 0 then intents.pray_off = off end
    return intents
end

-- ================================================================ ENTER

-- Before the loop: style slot 2 (Lunge on the emberlight, Longrange on the
-- bow), and the demon in sight.
function TD.enter(S)
    local ids = S.ids
    for press = 1, 4 do
        local mr, cm = api_drive.varp(ids.com_mode)
        assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
        if cm == TD.V.STYLE then break end
        assert(press <= 3, S.who .. ": three style presses and varp43_com_mode reads " .. tostring(cm))
        QD.raid._tob_tab(S, ids.combat_tab)
        local pr, pd = api_drive.if_click(ids.style_slot, 1)
        assert(pr == "ok", S.who .. ": the style press answered " .. tostring(pr) .. " " .. tostring(pd))
        for _ = 1, 2 do
            await({ event = "server_tick", match = function() return true end, note = S.who .. ": style" }, 3)
        end
    end
    for _ = 1, 10 do
        local F = QD.raid._tob_measure(S)
        if TD.find(S, F) then return end
        QD.ticks(1)
    end
    assert(false, S.who .. ": no Tormented Demon in sight")
end

-- ================================================================ THE ROOM

QD.T2.register("tormented_demon", {
    ids = function(opts) return TD.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "tormented_demon_solve"
        S.t2.plan_opts = { h = opts.h or TD.V.H, beam = opts.beam or TD.V.BEAM }
        S.m = { segments = 0, swings = 0, bombs = 0, bomb_hits = 0, guessed = 0,
                wrong_melee = 0, wrong_known = 0, wrong_guess = 0 }
        -- my swings for the monitor: the emberlight's Lunge and the bow (a
        -- block animation is not one)
        S.mon.swing_seqs = {}
        for _, name in ipairs({ "human_sword_stab", "human_bow" }) do
            local r, id = api_drive.symbol("seq", name)
            assert(r == "ok", S.who .. ": no seq " .. name)
            S.mon.swing_seqs[id] = true
        end
    end,

    enter = function(S) TD.enter(S) end,

    predict = function(S, F, B) TD.predict(S, F, B) end,

    strategy = function(S, F, B)
        return TD.strategy(S, F, B)
    end,

    -- done: its death seen and my prayers down
    done = function(S, F)
        local m = S.m
        if m.dead and not F.lit[QD.T2.PROTECT.melee] and not F.lit[QD.T2.PROTECT.magic]
            and not F.lit[QD.T2.PROTECT.ranged] then
            return "ok"
        end
        return nil
    end,

    summary = function(S)
        local m = S.m
        return string.format("td: engaged %s dead %s (%s ticks), segments %d, swings %d, bombs %d, bomb hits %d, "
            .. "guessed %d, wrong melee %d, wrong known %d, wrong guess %d, mode %s",
            tostring(m.engaged), tostring(m.dead), (m.dead and m.engaged) and tostring(m.dead - m.engaged) or "-",
            m.segments, m.swings, m.bombs, m.bomb_hits, m.guessed, m.wrong_melee, m.wrong_known, m.wrong_guess,
            tostring(m.mode))
    end,
})
