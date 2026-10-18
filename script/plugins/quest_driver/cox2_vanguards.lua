-- quest-driver / cox2_vanguards: THE VANGUARDS, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/vanguards_solver.md is the plan, vanguards_spec.md
-- the mechanics (rows V1-V19), docs/CONTENT_SOLVER_MANUAL.md the method. A
-- QD.T2 room (tob2_run.lua): PREDICT and STRATEGY here, the planner
-- (api_drive.aplan), executive, monitor and supplies shared.
--
-- THE IDEA (Synq 1:38:49, the CoX Vanguards plugin): count every vanguard's
-- hitpoints exactly from its hitsplats, always hit the one with the MOST
-- (with the style it is weak to), and the gap that resets the room (72 of
-- 180) never opens: a hit on the highest can only leave the gap at most my
-- max hit. Draw one at a time (aggression is 5 tiles); pray against whoever
-- is swinging at me; keep out of a walking shell's 3x3.
--
-- Perception: npc rows (type = style, server tile, splats, facing, seq),
-- locs (the exit crystal). Nothing from the server.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}
QD.COXVG = QD.COXVG or {}
QD.COXVG.V = {
    H = 10, BEAM = 32,
    BASE = 180, GAP = 72,          -- floor(0.4 * 180): spec V11
    AGGRO = 5, AGGRO_COST = 15,     -- a tile within 5 of a vanguard's 3x3 (RSProx RV-A1)
    HEAL_SPLAT = 6,                -- all.hitsplat.compack: 6 = hitsplat_heal
    SIZE = 3,
    -- my weapon per vanguard style (the triangle, spec header)
    WEAK = { melee = "magic", ranged = "melee", magic = "ranged" },
    RANGE = { melee = 1, magic = 7, ranged = 5 },
    SPEED = { melee = 4, magic = 4, ranged = 2 },
    SWING_VALUE = 2.5,
    EAT_BELOW = 55, BREW_BELOW = 60,
    -- every vanguard form's attack animation -> its style (spec V5-V7)
    ATTACK_SEQ = { vanguard_attack_melee = "melee", vanguard_attack_ranged = "ranged", vanguard_attack_magic = "magic" },
    LOOT_TICKS = 20,
    -- the three holes' template SOUTH-WEST tiles by variant (cox_vanguards.rs2
    -- ~cox_vanguard_tile: thru and cw recorded, RSProx RV-R1; ccw Near-Reality's)
    HOLES = { [0] = { { 20, 16 }, { 12, 21 }, { 11, 11 } }, [1] = { { 20, 17 }, { 13, 21 }, { 12, 12 } },
              [2] = { { 19, 12 }, { 13, 19 }, { 9, 12 } } },
    WAKE = 6,
}

-- The tile to wake them from (template): within 6 of a hole's tile (spec V2),
-- in the aggression range of as FEW holes as can be (5 of a 3x3, RSProx
-- RV-A1: no waking tile is outside all three; the recorded raiders woke them
-- 2-5 out and fought all three), then nearest the room's centre.
function QD.COXVG.wake_tile(variant)
    local V = QD.COXVG.V
    local holes = V.HOLES[variant]
    local best, bn, bd = nil, nil, nil
    for lx = 6, 26 do
        for lz = 6, 26 do
            local near, wakes, under = 0, false, false
            for _, h in ipairs(holes) do
                local dx = math.max(h[1] - lx, lx - (h[1] + V.SIZE - 1), 0)
                local dz = math.max(h[2] - lz, lz - (h[2] + V.SIZE - 1), 0)
                local g = math.max(dx, dz)
                if g == 0 then under = true end
                if g <= V.AGGRO then near = near + 1 end
                if math.max(math.abs(lx - h[1]), math.abs(lz - h[2])) <= V.WAKE - 1 then wakes = true end
            end
            if wakes and not under then
                local d = math.abs(lx - 16) + math.abs(lz - 16)
                if best == nil or near < bn or (near == bn and d < bd) then best, bn, bd = { lx, lz }, near, d end
            end
        end
    end
    assert(best, "cox_vanguards: no wake tile for variant " .. tostring(variant))
    return best[1], best[2]
end

function QD.COXVG.ids(opts)
    local CV = QD.COXVG
    local ids = QD.raid._tob_common_ids("cox_vanguards", opts)
    local room = QD.raid._tob_symbols("cox_vanguards", {
        { "vg_dormant", "npc", "raids_vanguard_dormant" }, { "vg_walking", "npc", "raids_vanguard_walking" },
        { "vg_melee", "npc", "raids_vanguard_melee" }, { "vg_ranged", "npc", "raids_vanguard_ranged" },
        { "vg_magic", "npc", "raids_vanguard_magic" },
        { "crystal", "loc", "raids_blockage_orange" },
        { "pipe", "obj", "toxic_blowpipe_loaded" }, { "assembler", "obj", "avas_assembler" },
        { "trident", "obj", "toxic_tots_charged" },
        { "occult", "obj", "occult_necklace" }, { "imbued_cape", "obj", "ma2_saradomin_cape" },
        { "defender", "obj", "dragon_parryingdagger" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "fury", "obj", "enchanted_onyx_amulet" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "dhide_body", "obj", "black_dragonhide_body" }, { "torso", "obj", "barbassault_penance_fighter_torso" },
        { "chaps", "obj", "black_dragonhide_chaps" }, { "vial_empty", "obj", "raids_vial_empty" },
        { "mystic_top", "obj", "mystic_robe_top" }, { "mystic_bottom", "obj", "mystic_robe_bottom" },
        { "style_slot_1", "component", "combat_interface:style_slot_1" },
        { "com_mode", "varp", "varp43_com_mode" },
        { "retaliate", "component", "combat_interface:retaliate" }, { "nodef", "varp", "varp172_option_nodef" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_vanguards: no combat tab")
    ids.combat_tab = combat_tab
    ids.vg_kind = {
        [ids.vg_dormant] = "dormant", [ids.vg_walking] = "walking",
        [ids.vg_melee] = "melee", [ids.vg_ranged] = "ranged", [ids.vg_magic] = "magic",
    }
    ids.sets = {
        melee = { ids.tentacle, ids.fury, ids.fire_cape, ids.defender, ids.torso, ids.chaps },
        magic = { ids.trident, ids.occult, ids.imbued_cape, ids.defender, ids.mystic_top, ids.mystic_bottom },
        ranged = { ids.pipe, ids.fury, ids.assembler, ids.dhide_body, ids.chaps },
    }
    ids.weapon = { melee = ids.tentacle, magic = ids.trident, ranged = ids.pipe }
    ids.boost = { ranged = "rigour", melee = "piety", magic = "augury" }
    ids.close_seq = {}
    for _, name in ipairs({ "vanguard_idle_melee_idle_basic_transition", "vanguard_idle_ranged_idle_basic_transition",
                            "vanguard_idle_magic_idle_basic_transition" }) do
        local r, id = api_drive.symbol("seq", name)
        assert(r == "ok", "cox_vanguards: no seq " .. name)
        ids.close_seq[id] = true
    end
    ids.attack_seq = {}
    for name, style in pairs(CV.V.ATTACK_SEQ) do
        local r, id = api_drive.symbol("seq", name)
        assert(r == "ok", "cox_vanguards: no seq " .. name)
        ids.attack_seq[id] = style
    end
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_vanguards: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    ids.brews = QD.COX.supply_ids("heal")
    ids.restores = QD.COX.supply_ids("restore")
    ids.stat_restores = ids.restores
    ids.overloads = QD.COX.supply_ids("overload")
    ids.enhances = QD.COX.supply_ids("prayer")
    ids.combats, ids.rangings = {}, {}
    return ids
end

-- ================================================================ OBSERVE

-- Is world tile (x, z) inside my room?
function QD.COXVG.in_room(S, x, z)
    local rm = S.v.room
    return x >= rm.ox and x < rm.ox + 32 and z >= rm.oz and z < rm.oz + 32
end

-- Every vanguard row of the room into S.v.vg (keyed by the npc's element), its
-- hitpoints COUNTED as the plugin counts them: base at the first sight; minus
-- every damage splat (a splat is seen once: by start cycle, type and amount,
-- with a count for identical ones); back to base on a heal splat (a RESET);
-- +1 when it turns into the walking form (spec V14). The overhead bar
-- (1/30ths) re-syncs a count that drifted more than a notch.
function QD.COXVG.observe(S, F)
    local CV, v, ids = QD.COXVG, S.v, S.ids
    local V = CV.V
    local r, rows = api_drive.npcs(40)
    assert(r == "ok", "cox_vanguards: npcs answered " .. tostring(r))
    -- TRAMPLE (the GREEN zero): hp lost this tick while my tile at the end of
    -- the last one was inside a walking shell's 3x3 as it stood then
    if v.last_hp and F.hp < v.last_hp and v.last_me then
        for _, w in ipairs(v.last_walkers or {}) do
            if v.last_me.x >= w.x and v.last_me.x < w.x + V.SIZE and v.last_me.z >= w.z and v.last_me.z < w.z + V.SIZE then
                v.trample = v.trample + 1
                QD.raid._tob_trace(S, F.tick, string.format("vanguards: TRAMPLED at %d,%d by the shell at %d,%d",
                    v.last_me.x, v.last_me.z, w.x, w.z))
                break
            end
        end
    end
    v.last_hp, v.last_me = F.hp, { x = F.me.x, z = F.me.z }
    local seen = {}
    v.walking, v.active, v.dormant = 0, 0, 0
    for _, row in ipairs(rows) do
        local kind = ids.vg_kind[row.npc_id]
        local x, z = row.server_x or row.x, row.server_z or row.z
        if kind and CV.in_room(S, x, z) then
            local k = QD.T2.npc_key(row)
            seen[k] = true
            local s = v.vg[k]
            if s == nil then
                s = { hp = V.BASE, splats = {}, splat_t = {}, first = F.tick, swings = 0 }
                v.vg[k] = s
            end
            if kind == "walking" and s.kind ~= nil and s.kind ~= "walking" and s.kind ~= "dormant" then
                s.hp = math.min(V.BASE, s.hp + 1)
                v.shuffles_seen = (v.shuffles_seen or 0) + 1
                -- the shell's first tick, and where it closed (its walk is
                -- told from it: RSProx RV-S1..S3)
                s.shelled, s.shell_x, s.shell_z = F.tick, x, z
            end
            if kind ~= "dormant" and kind ~= "walking" then s.style = kind end
            if kind == "walking" then v.walking = v.walking + 1
            elseif kind == "dormant" then v.dormant = v.dormant + 1
            else v.active = v.active + 1 end
            if kind ~= "dormant" then v.risen = v.risen or F.tick end
            s.kind, s.row, s.x, s.z, s.last = kind, row, x, z, F.tick
            local counts = {}
            for _, sp in ipairs(row.splats or {}) do
                local key = sp.start .. ":" .. sp.type .. ":" .. sp.damage
                counts[key] = (counts[key] or 0) + 1
                if counts[key] > (s.splats[key] or 0) then
                    s.splats[key] = counts[key]
                    s.splat_t[key] = F.tick
                    if sp.type == V.HEAL_SPLAT then
                        if not s.reset_t or s.reset_t ~= F.tick then
                            v.resets = v.resets + 1
                            QD.raid._tob_trace(S, F.tick, string.format("vanguards: RESET (heal splat %d on %s)",
                                sp.damage, tostring(s.style)))
                        end
                        s.reset_t = F.tick
                        s.hp = V.BASE
                    elseif sp.damage > 0 then
                        s.hp = s.hp - sp.damage
                        if s.style then v.dealt[s.style] = (v.dealt[s.style] or 0) + sp.damage end
                    end
                end
            end
            -- (a living vanguard has 1 at least: a blow the rules turned into
            -- "left at 1" counted it under -- seed 2 t440 read the magic at -1
            -- and never picked it again)
            if s.hp < 1 then s.hp = 1 end
            for key, t in pairs(s.splat_t) do
                if F.tick - t > 10 then s.splats[key], s.splat_t[key] = nil, nil end
            end
            -- the bar: health_ratio of health_scale; a count off by more
            -- than a notch is re-synced (and counted)
            if (row.health_ratio or -1) >= 0 and (row.health_scale or 0) > 0 and row.health_active then
                local notch = V.BASE / row.health_scale
                local lo = (row.health_ratio - 1) * notch
                local hi = (row.health_ratio + 1) * notch
                if s.hp < lo - 1 or s.hp > hi + 1 then
                    v.resyncs = v.resyncs + 1
                    s.hp = math.floor(row.health_ratio * notch + 0.5)
                end
            end
            -- a swing: a new attack animation (seq_id with a new seq_tick)
            local style = ids.attack_seq[row.seq_id or -1]
            if style and row.seq_tick ~= s.seq_tick then
                s.swings = s.swings + 1
                s.swing_t = F.tick
            end
            -- the shells closing: the style -> basic animation (spec V13)
            if ids.close_seq[row.seq_id or -1] and row.seq_tick ~= s.close_tick then
                s.close_tick = row.seq_tick
                s.closing = F.tick
                QD.raid._tob_trace(S, F.tick, string.format("vanguards: %s closing (seq tick %s) at %d,%d",
                    tostring(s.style), tostring(row.seq_tick), x, z))
            end
            s.seq_tick = row.seq_tick
            s.engaged = (row.facing or -1) >= 0
            -- its hole: the one it stands on whenever it is a combat form
            -- standing still on one (the rise, every emergence)
            if kind ~= "walking" and kind ~= "dormant" then
                local h = QD.COXVG.hole_at(S, x, z)
                if h then s.hole = h end
                s.from = nil
            elseif kind == "walking" then
                s.from = s.from or { x = x, z = z }
            end
        end
    end
    v.last_walkers = {}
    for _, s in pairs(v.vg) do
        if seen[QD.T2.npc_key(s.row)] and s.kind == "walking" then v.last_walkers[#v.last_walkers + 1] = { x = s.x, z = s.z } end
    end
    -- THE WAY ROUND (all the same way, spec V14): read off the first walker
    -- that has stepped closer to one of its two candidate holes
    if v.walking == 0 then v.dir = nil end
    if v.walking > 0 and v.dir == nil then
        for _, s in pairs(v.vg) do
            if s.kind == "walking" and s.hole and s.from and (s.x ~= s.from.x or s.z ~= s.from.z) then
                local best, bd = nil, nil
                for d = 1, 2 do
                    local hx, hz = QD.COXVG.hole_world(S, (s.hole - 1 + d) % 3 + 1)
                    local dist = math.max(math.abs(hx - s.x), math.abs(hz - s.z))
                    local dist0 = math.max(math.abs(hx - s.from.x), math.abs(hz - s.from.z))
                    local gain = dist0 - dist
                    if best == nil or gain > bd then best, bd = d, gain end
                end
                if bd and bd > 0 then
                    v.dir = best
                    QD.raid._tob_trace(S, F.tick, "vanguards: shuffle way round " .. best)
                    break
                end
            end
        end
    end
    -- a vanguard gone from the rows after it rose: dead (a 1-hp "survive" plays
    -- the death animation but stays: spec E1, so the ROW is the test)
    for k, s in pairs(v.vg) do
        if not seen[k] and not s.dead and s.kind ~= "dormant" then
            s.dead = F.tick
            QD.raid._tob_trace(S, F.tick, string.format("vanguards: %s down (count %d)", tostring(s.style), s.hp))
        end
    end
    -- the exit crystal
    local lr, locs = api_drive.locs(40)
    v.crystal = false
    if lr == "ok" then
        for _, l in ipairs(locs) do
            if l.loc_id == ids.crystal and CV.in_room(S, l.x, l.z) then v.crystal = { x = l.x, z = l.z } end
        end
    end
    if v.crystal then v.crystal_seen = true end
end

-- My room: here, the room ahead, or a neighbouring cell -- whichever is
-- the Vanguards' (QD.COX.room_at from the template grid the client is sent).
function QD.COXVG.find_room()
    local here = QD.COX.here()
    if here and here.name == "vanguards" then return here end
    local _, me = api_drive.player_tile()
    local cands = {}
    if here then
        local _, _, cx, cz = QD.COX.next_origin(here)
        cands[#cands + 1] = { cx, cz }
    end
    local bx, bz = me.x - me.x % 32 + 16, me.z - me.z % 32 + 16
    for _, d in ipairs({ { 0, 32 }, { 32, 0 }, { 0, -32 }, { -32, 0 } }) do
        cands[#cands + 1] = { bx + d[1], bz + d[2] }
    end
    for _, c in ipairs(cands) do
        local rm = QD.COX.room_at(c[1], c[2], me.level)
        if rm and rm.name == "vanguards" then return rm end
    end
    return here
end

-- Hole i's south-west world tile (the 3x3's, turned with the room).
function QD.COXVG.hole_world(S, i)
    local V, rm = QD.COXVG.V, S.v.room
    local h = V.HOLES[rm.variant][i]
    return QD.COX.world_sized(rm, h[1], h[2], V.SIZE, V.SIZE)
end

-- The hole whose tile is (x, z), or nil.
function QD.COXVG.hole_at(S, x, z)
    for i = 1, 3 do
        local hx, hz = QD.COXVG.hole_world(S, i)
        if hx == x and hz == z then return i end
    end
    return nil
end

-- A walking shell's route from (x, z) to (tx, tz): `speed` tiles a tick (1
-- walks, 2 runs), diagonal while both axes differ, then straight (the npc
-- walk). -> { {x, z}, ... } (step k = the end of tick now + k)
function QD.COXVG.route(x, z, tx, tz, n, speed)
    local out = {}
    for _ = 1, n do
        for _ = 1, (speed or 1) do
            if x < tx then x = x + 1 elseif x > tx then x = x - 1 end
            if z < tz then z = z + 1 elseif z > tz then z = z - 1 end
        end
        out[#out + 1] = { x, z }
        if x == tx and z == tz then break end
    end
    return out
end

-- A shell's 3x3 at (x, z) over tick-ends [t0, t1]: the 3x3 itself
-- forbidden (the stomp reads it), the tile round it priced (the walk model's
-- slack) -- a forbidden ring boxed me in when three shells walked at once
-- (seed 40 t262: every tile in reach forbidden, the plan took a 3x3).
function QD.COXVG.sweep(B, name, x, z, t0, t1)
    local n = QD.COXVG.V.SIZE
    B.hit({ name = name, x0 = x, z0 = z, x1 = x + n - 1, z1 = z + n - 1,
            t0 = t0, t1 = t1, dmg_exp = 5, dmg_max = 6, forbid = true })
    B.hit({ name = name .. "_ring", x0 = x - 1, z0 = z - 1, x1 = x + n, z1 = z + n,
            t0 = t0, t1 = t1, dmg_exp = 4, dmg_max = 4 })
end

-- The living, risen vanguards (combat or walking forms).
QD.COXVG.ORDER = { melee = 1, ranged = 2, magic = 3 }

-- (in a fixed order: melee, ranged, magic -- `pairs` over the element keys
-- ran in a different order on the live client, and a tie at the rise picked
-- another target there: vanguards0 live vs scriptrun, 2026-10-18)
function QD.COXVG.living(S)
    local out = {}
    for _, s in pairs(S.v.vg) do
        if not s.dead and s.kind ~= "dormant" then out[#out + 1] = s end
    end
    table.sort(out, function(a, b)
        local oa, ob = QD.COXVG.ORDER[a.style or ""] or 9, QD.COXVG.ORDER[b.style or ""] or 9
        if oa ~= ob then return oa < ob end
        if a.x ~= b.x then return a.x < b.x end
        return a.z < b.z
    end)
    return out
end

-- ================================================================ PREDICT

-- A walking shell tramples whoever is in its 3x3 (3-6 a tick, spec V15): its
-- footprint and the ring round it (it moves a tile a tick) are forbidden for
-- the next two tick-ends.
function QD.COXVG.predict(S, F, B)
    local V = QD.COXVG.V
    QD.COXVG.observe(S, F)
    local v = S.v
    for _, s in ipairs(QD.COXVG.living(S)) do
        -- UNDER a combat form: its shell can close on any tick, and the
        -- first stomp reads where I stood the tick the closing shows -- too
        -- late to step out (seed 0, t224). Never end a tick under one.
        -- (not the ranged one: it never moves, and under it has no shot --
        -- Synq 1:41:35; its shell stands 3 ticks before it walks, RV-S3)
        if s.kind ~= "walking" and s.style ~= "ranged" then
            B.hit({ name = "under", x0 = s.x, z0 = s.z, x1 = s.x + V.SIZE - 1, z1 = s.z + V.SIZE - 1,
                    t0 = F.tick + 1, t1 = F.tick + B.h, dmg_exp = 5, dmg_max = 6, forbid = true })
        end
        if s.kind == "walking" then
            -- where it stands now (and the tick after: it may not have moved)
            QD.COXVG.sweep(B, "trample", s.x, s.z, F.tick + 1, F.tick + 2)
            -- ITS WALK (RSProx RV-S1..S3): the shell closes and walks on the
            -- same tick S. On the timer it stands to S+3 and RUNS, 2 tiles a
            -- tick; after a reset it walks at once, a tile a tick. A shell
            -- that has not moved yet may be either; one that stepped on its
            -- first tick is a reset's. Each step's 3x3 and a tile round it,
            -- the tick before, of and after the step, to all three holes (the
            -- hole a chaser last stood on is not always the one it left from
            -- -- seed 11 t273; a route to the hole it stands on is empty).
            local since = F.tick - (s.shelled or F.tick)
            local moved = s.shell_x ~= nil and (s.x ~= s.shell_x or s.z ~= s.shell_z)
            local walks = { }
            if moved and since <= 3 then
                walks[1] = { speed = 1, lag = 0 }
            elseif moved then
                walks[1] = { speed = 2, lag = 0 }
            else
                walks[1] = { speed = 2, lag = math.max(0, 3 - since) }
                if since <= 1 then walks[2] = { speed = 1, lag = 0 } end
            end
            -- (standing till it sets off: its own 3x3 for the wait)
            if walks[1].lag > 0 then
                QD.COXVG.sweep(B, "standing", s.x, s.z, F.tick + 1, F.tick + 1 + walks[1].lag)
            end
            for _, w in ipairs(walks) do
                for hi = 1, 3 do
                    local tx, tz = QD.COXVG.hole_world(S, hi)
                    for k, st in ipairs(QD.COXVG.route(s.x, s.z, tx, tz, B.h, w.speed)) do
                        local t = F.tick + k + w.lag
                        QD.COXVG.sweep(B, "route", st[1], st[2], t - 1, t + 1)
                    end
                end
            end
        end
    end
end

-- ================================================================ STRATEGY

function QD.COXVG.gear(S, style)
    local out = {}
    for _, obj in ipairs(S.ids.sets[style]) do
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = (obj == S.ids.pipe) }
        end
    end
    return out
end

-- THE TARGET: the active (combat-form) vanguard with the most counted hp;
-- ties to the one already chosen, then the nearest.
function QD.COXVG.pick(S, F)
    local best, bd = nil, nil
    for _, s in ipairs(QD.COXVG.living(S)) do
        if s.kind ~= "walking" and s.style then
            local d = math.max(math.abs(s.x + 1 - F.me.x), math.abs(s.z + 1 - F.me.z))
            local better = best == nil or s.hp > best.hp
                or (s.hp == best.hp and (s == S.v.target or (best ~= S.v.target and d < bd)))
            if better then best, bd = s, d end
        end
    end
    return best
end

-- Prayer: against whoever swung at me last (within 6 ticks); else against
-- the target's style.
function QD.COXVG.prayer(S, F, target)
    local pick, pt = nil, -1
    for _, s in ipairs(QD.COXVG.living(S)) do
        if s.style and s.swing_t and F.tick - s.swing_t <= 6 and s.engaged then
            -- (melee in reach outranks: three hits of 18)
            local near = math.max(math.abs(s.x + 1 - F.me.x), math.abs(s.z + 1 - F.me.z)) <= 2
            local score = s.swing_t + ((s.style == "melee" and near) and 100 or 0)
            if score > pt then pick, pt = s.style, score end
        end
    end
    if pick == nil and target then pick = target.style end
    return pick
end

function QD.COXVG.doses(S, list)
    local n = 0
    for i, obj in ipairs(list) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    return n
end

-- The room's own drinks (the supplies drink Xeric's aid and revitalisation):
-- an overload as the fight starts and again as it ends, prayer enhance under
-- 85 prayer. A drink is known by its dose count falling.
function QD.COXVG.drinks(S, F, intents)
    local v, ids = S.v, S.ids
    for _, d in ipairs({ { "overloads", "ovl_until", 501 }, { "enhances", "penh_until", 458 } }) do
        local n = QD.COXVG.doses(S, ids[d[1]])
        local key = d[1] .. "_n"
        if v[key] ~= nil and n < v[key] then v[d[2]] = F.tick + d[3] end
        v[key] = n
    end
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    local pick = nil
    if (v.ovl_until or -1) <= F.tick and F.hp >= 85 then pick = QD.raid._tob_first(S, ids.overloads) end
    if pick == nil and (v.penh_until or -1) < F.tick and F.prayer < 85 then pick = QD.raid._tob_first(S, ids.enhances) end
    if pick then
        intents.drink = pick
        S.last_drink = F.tick
    end
end

function QD.COXVG.strategy(S, F, B)
    local CV, v, ids = QD.COXVG, S.v, S.ids
    local V = CV.V
    -- (observed in predict: the loop runs predict first, and a closing
    -- seen this tick must be in this tick's threat field -- seed 3 t348)
    local intents = { eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    local living = CV.living(S)
    local mode
    if not v.risen then
        mode = "wake"
    elseif #living == 0 then
        mode = "loot"
    elseif v.active == 0 then
        mode = "shuffle"
    else
        mode = "fight"
    end
    intents.mode = mode
    local target = nil
    if mode == "fight" then
        target = CV.pick(S, F)
        v.target = target
    else
        v.target = nil
    end
    -- A TARGET CHANGE ENDS THE OLD ATTACK: the server keeps a player's
    -- attack on its npc through a wield, and a plan that holds ("stay") while
    -- the new target is out of reach left the old one taking hits -- seed 39
    -- t172-181, a reset. The executive holds only an attack it knows of.
    local tkey = target and QD.T2.npc_key(target.row) or nil
    if tkey ~= v.target_key and S.x2 and S.x2.interact and S.x2.interact.state ~= "attacking" then
        S.x2.interact.state = "attacking"
    end
    v.target_key = tkey
    local style = target and V.WEAK[target.style] or nil
    -- the next style while shuffling: the one with the most hp keeps it
    if mode == "shuffle" then
        local hi = nil
        for _, s in ipairs(living) do if s.style and (hi == nil or s.hp > hi.hp) then hi = s end end
        style = hi and V.WEAK[hi.style] or nil
    end
    if style then intents.gear = CV.gear(S, style) end
    if target and QD.raid._tob_worn(S, ids.weapon[style]) then
        B.target({ name = "vg_" .. target.style, npc = target.row, size = V.SIZE, range = V.RANGE[style],
                   speed = V.SPEED[style], value = V.SWING_VALUE })
        if F.prayer >= 30 then intents.pray_boost = ids.boost[style] end
    end
    -- AGGRESSION (spec V4): a tile within 3 of a vanguard that is not on me
    -- yet draws it; priced, not forbidden (the target itself is free)
    -- (a fight with no swing in the plan for 8 ticks drops the price: when
    -- every tile in reach of the target draws another, a strong human takes
    -- the second attacker and prays -- seed 0 t930-1500 stood still)
    local last = S.t2.last
    if mode == "fight" and last and (last.value or 0) <= 0 then
        v.stall = (v.stall or 0) + 1
    else
        v.stall = 0
    end
    local aggro_ok = (v.stall or 0) < 8
    for _, s in ipairs(living) do
        if aggro_ok and s ~= target and s.kind ~= "walking" and not s.engaged then
            B.hit({ name = "aggro_" .. tostring(s.style), x0 = s.x - V.AGGRO, z0 = s.z - V.AGGRO,
                    x1 = s.x + V.SIZE - 1 + V.AGGRO, z1 = s.z + V.SIZE - 1 + V.AGGRO,
                    t0 = F.tick + 1, t1 = F.tick + B.h, dmg_exp = V.AGGRO_COST, dmg_max = V.AGGRO_COST })
        end
    end
    -- UNDER THE RANGED ONE (Synq 1:41:35-1:42:08): fighting the other two,
    -- stand under the ranged vanguard -- it does not move and has no shot at
    -- me there -- and hit them from it (the trident and the blowpipe reach).
    if mode == "fight" and target and target.style ~= "ranged" then
        for _, s in ipairs(living) do
            if s.style == "ranged" and s.kind ~= "walking" then
                B.pull({ name = "under_ranged", x = s.x, z = s.z, size = V.SIZE, weight = 0.4,
                         t0 = F.tick + 1, t1 = F.tick + B.h })
            end
        end
    end
    -- WAKE: to the wake tile (in reach of a hole, out of every aggression
    -- range); SHUFFLE: hold where I stand, off the walkers' paths (predict's
    -- forbid terms move me)
    if mode == "wake" then
        local wx, wz = QD.COX.world(v.room, QD.COXVG.wake_tile(v.room.variant))
        B.pull({ name = "wake", x = wx, z = wz, size = 1, weight = 0.3, t0 = F.tick, t1 = F.tick + B.h })
    elseif mode == "shuffle" then
        B.pull({ name = "hold", x = F.me.x, z = F.me.z, size = 1, weight = 0.1, t0 = F.tick, t1 = F.tick + B.h })
    end
    -- LOOT: the overloads first, then Xeric's aid (one Take a tick)
    if mode == "loot" then
        v.loot_start = v.loot_start or F.tick
        local or_, objs = api_drive.objs(20)
        local want = nil
        if or_ == "ok" and F.tick - v.loot_start < V.LOOT_TICKS and QD.T2.exec.free_slots(S) > 0 then
            for _, list in ipairs({ ids.overloads, ids.brews }) do
                for _, o in ipairs(objs) do
                    for _, id in ipairs(list) do
                        if want == nil and o.obj_id == id and CV.in_room(S, o.x, o.z) then want = o end
                    end
                end
            end
        end
        v.loot_left = want ~= nil
        -- (a second copy on the same tile is the same world op: one tick
        -- without it after every pick-up, so the next is sent afresh)
        local inv_n = 0
        for _, list in ipairs({ ids.overloads, ids.brews }) do
            for _, id in ipairs(list) do inv_n = inv_n + QD.raid._tob_count(S, id) end
        end
        local picked = v.loot_inv ~= nil and inv_n ~= v.loot_inv
        v.loot_inv = inv_n
        if want and not picked then
            intents.world_op = { kind = "obj", id = want.obj_id, op = 3, x = want.x, z = want.z }
        end
    end
    local pray = CV.prayer(S, F, target)
    if pray and mode ~= "loot" then
        local name = QD.T2.PROTECT[pray]
        if not F.lit[name] then intents.pray = name end
    else
        local off = {}
        for _, name in pairs(QD.T2.PROTECT) do if F.lit[name] then off[#off + 1] = name end end
        if #off > 0 then intents.pray_off = off end
    end
    if mode ~= "wake" and mode ~= "loot" then CV.drinks(S, F, intents) end
    if not intents.drink and not intents.eat and QD.raid._tob_count(S, ids.vial_empty) > 0
        and QD.T2.exec.free_slots(S) == 0 then
        intents.drop = ids.vial_empty
    end
    if F.tick % 5 == 0 or mode ~= v.mode_last then
        local parts = {}
        for _, s in ipairs(living) do
            parts[#parts + 1] = string.format("%s:%s hp%d%s", tostring(s.style), s.kind, s.hp, s.engaged and "*" or "")
        end
        QD.raid._tob_trace(S, F.tick, string.format("vanguards %s target %s [%s] resets %d",
            mode, target and target.style or "-", table.concat(parts, " "), v.resets))
        v.mode_last = mode
    end
    return intents
end

-- ================================================================ THE ROOM

QD.T2.register("cox_vanguards", {
    ids = function(opts) return QD.COXVG.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_vanguards_solve"
        S.t2.plan_opts = { h = opts.h or QD.COXVG.V.H, beam = opts.beam or QD.COXVG.V.BEAM }
        -- the room: the one I stand in, else the room ahead (the whole raid
        -- leaves me past the room before's Passage), else any neighbour
        local room = QD.COXVG.find_room()
        assert(room and room.name == "vanguards", "cox_vanguards: not the Vanguards' room ("
            .. tostring(room and room.name) .. ")")
        S.v = { room = room, vg = {}, resets = 0, resyncs = 0, dealt = {}, trample = 0 }
    end,

    -- the style slot 1 (blowpipe Rapid, trident Accurate, tentacle Flick);
    -- then into the room
    enter = function(S)
        local ids = S.ids
        -- Auto Retaliate OFF: a vanguard that swings at me must not take my
        -- attack off the one I chose (seed 24 t135: the whip answered the
        -- magic one for twenty ticks and the gap reset the room)
        for _ = 1, 3 do
            local vr, nd = api_drive.varp(ids.nodef)
            if vr ~= "ok" or nd == 1 then break end
            QD.raid._tob_tab(S, ids.combat_tab)
            api_drive.if_click(ids.retaliate, 1)
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": retaliate" }, 3)
            end
        end
        for press = 1, 4 do
            local mr, m = api_drive.varp(ids.com_mode)
            assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
            if m == 1 then break end
            assert(press <= 3, S.who .. ": three style presses and varp43_com_mode reads " .. tostring(m))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.style_slot_1, 1)
            assert(pr == "ok", S.who .. ": the style press answered " .. tostring(pr) .. " " .. tostring(pd))
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": style" }, 3)
            end
        end
    end,

    predict = function(S, F, B) QD.COXVG.predict(S, F, B) end,

    strategy = function(S, F, B)
        local intents = QD.COXVG.strategy(S, F, B)
        S.t2.want.boost = intents.pray_boost
        intents.pray_boost = nil
        return intents
    end,

    -- done: every vanguard down, the exit crystal gone, the loot taken (or
    -- its time up)
    done = function(S, F)
        local v = S.v
        if v.risen and #QD.COXVG.living(S) == 0 and not v.crystal and not v.loot_left and v.loot_start then
            return "ok"
        end
        return nil
    end,

    summary = function(S)
        local v = S.v
        local parts = {}
        for _, s in pairs(v.vg) do
            parts[#parts + 1] = string.format("%s hp%d swings %d%s", tostring(s.style), s.hp, s.swings,
                s.dead and (" dead@" .. s.dead) or "")
        end
        table.sort(parts)
        local dealt = {}
        for k, n in pairs(v.dealt) do dealt[#dealt + 1] = k .. " " .. n end
        table.sort(dealt)
        return string.format("vanguards: resets %d, trample %d, resyncs %d, risen %s, crystal seen %s gone %s, shells %d, {%s}, dealt {%s}",
            v.resets, v.trample, v.resyncs, tostring(v.risen), tostring(v.crystal_seen), tostring(not v.crystal),
            v.shuffles_seen or 0, table.concat(parts, "; "), table.concat(dealt, ", "))
    end,
})
