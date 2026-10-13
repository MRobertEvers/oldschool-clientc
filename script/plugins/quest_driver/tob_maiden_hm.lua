-- quest-driver / tob_maiden_hm: THE MAIDEN OF SUGADINTI, HARD MODE, five
-- raiders in the Learner/Void kit.
--
--   t.raid.maiden_hm_solve(opts) -> result, detail, record
--
-- The Normal solver's machinery (tob_maiden.lua: the measure, her clock, the
-- pools, the trails, the crab tracker, the freezer's barrage slots, the
-- planner spec, the entry) with Hard Mode's rules on top
-- (wiki Theatre_of_Blood/Hard_Mode :313-317):
--
--   crabs         ALL TEN at every threshold (blert: 10 at 2-5 raiders), 100
--                 hitpoints each at five. Every one is frozen one to five steps
--                 from its leak, and a freeze leaves five ticks of immunity
--                 behind it (wiki_Freeze :7, the engine's NPC_FREEZE), so a
--                 crab that thaws walks into her before a second barrage can
--                 hold it: each has to DIE inside its first freeze.
--   blood spawns  invulnerable; a barrage still freezes one, and a frozen
--                 spawn lays no new trail.
--   trails        never fade (the shared spec forbids a trail for as long as
--                 its loc is seen).
--   focus         from her third storm in a row on one raider they hit harder
--                 (tob_maiden.rs2 ^tob_maiden_hard_focus_hits): the tank turns
--                 over every TANK_STORMS.
--
-- THE ROLES ("Freezers often follow the convention of N1234, S12", :315 -
-- here each freezer takes all of its side):
--   seat 1  the NORTH freezer: barrage slots N1 +1, N2 +6, N3 +11, the 4s +16
--           from x=41, then its side's first thaw (a barrage on a clump, the
--           blowpipe on a lone crab), a thawed crab out of its immunity, and
--           between waves the blood spawns on its half, else her (blowpipe).
--   seat 2  the SOUTH freezer, the same for S1..S4 from x=40.
--   seats 3-5  DPS: the hammer opener, the tentacle on her between waves (one
--           tanks her east face, turned over every TANK_STORMS); in a wave the
--           blowpipe on the crabs the freezers are not on, shared out by thaw.
-- ==========================================================================

QD.MAIDEN_HM = {
    FREEZE = {
        [1] = { { at = 1, last = 5, aim = { "N1" } }, { at = 6, last = 8, aim = { "N2" } },
                { at = 11, last = 12, aim = { "N3" } }, { at = 16, last = 16, aim = { "N4o", "N4i" } } },
        [2] = { { at = 1, last = 5, aim = { "S1" } }, { at = 6, last = 8, aim = { "S2" } },
                { at = 11, last = 12, aim = { "S3" } }, { at = 16, last = 16, aim = { "S4o", "S4i" } } },
    },
    -- the freezers' wave stations (both reach every spawn point within 10)
    -- and their stations between waves (gap 5: the blowpipe on her)
    FREEZER_STATION = { [1] = { x = 41, z = 30 }, [2] = { x = 40, z = 30 } },
    STATION = { [1] = { x = 36, z = 31 }, [2] = { x = 36, z = 29 } },
    -- the DPS: the tank on her east face (Chebyshev 3 from her centre, the
    -- storm's pick), the other two on the south and west faces (4); where
    -- they wait for a wave (her north-east and south-east corners)
    DPS = { 3, 4, 5 },
    TANK_TILE = { x = 32, z = 31 }, OFF_TILES = { { x = 30, z = 27 }, { x = 25, z = 30 } },
    TANK_STORMS = 2,
    TANK_BREW = 75,
    BLOOD_SEAT = 3, SOUL_RUNE_INDEX = 13,       -- the Ancients DPS seat; enum982_rune_pouch_rune's soulrune
    WAVE_TILE = { [3] = { x = 32, z = 33 }, [4] = { x = 31, z = 34 }, [5] = { x = 32, z = 28 } },
    -- (and a tile further out, Chebyshev 4 from her centre, for the two whose
    -- turn it is not: every wave tile at 3 tied with the tank, her storm took
    -- the higher orb, and seat 3 tanked every wave - 438 and 427 taken and
    -- dead while seat 5 took 26-38, all128 r015, r078; owner, 2026-10-12:
    -- "Maiden non-freezer seats should alternate accepting the tank spot")
    WAVE_FAR = { [3] = { x = 33, z = 34 }, [4] = { x = 31, z = 35 }, [5] = { x = 33, z = 27 } },
    IMMUNE = 5,
    -- THE FREEZERS' PREWAVE, nearer the threshold than Normal's 100 permille:
    -- five raiders' damage is slow on her through her heals, and a freezer
    -- holding its cast set at the station for 124 ticks froze no spawn while
    -- their trails covered the floor (m5a sf: 1669 trail tiles, three seats out
    -- of their planner budget). A spawn is frozen first until SLUG_PREWAVE.
    PRE_WAVE = 40, SLUG_PREWAVE = 20,
    CRAB_URGENT = 4,                            -- a crab this many ticks from her outranks a blood spawn
    -- a crab's hitpoints (^tob_maiden_crab_hp_5) and a blowpipe's hit on one, every PIPE_TICKS (all128k)
    CRAB_HP = 100, PIPE_HIT = 12, PIPE_TICKS = 2,
    -- the rescue freeze: a crab this old (its first freeze landed at +1) and this near her, unfrozen
    RESCUE_AGE = 2, RESCUE_ETA = 5,
}

local function cheb(ax, az, bx, bz) return QD.raid._tob_cheb(ax, az, bx, bz) end

-- ==================================================================== MEASURE

-- Hard Mode's bookkeeping on top of the shared measure: each crab's thaw and
-- its immunity after it; each blood spawn's freeze.
function QD.raid._maiden_hm_marks(S, F)
    local V = QD.MAIDEN
    for _, c in ipairs(F.crabs) do
        local rec = S.crab_rec[c.slot]
        if rec.frozen_until ~= nil and rec.frozen_until > (rec.thaw_seen or -1) then rec.thaw_seen = rec.frozen_until end
        c.thaw = rec.frozen_until
        c.immune = not c.frozen and rec.thaw_seen ~= nil and F.tick < rec.thaw_seen + QD.MAIDEN_HM.IMMUNE
    end
    for _, sl in ipairs(F.slugs) do
        local m = S.slug_freeze[sl.slot] or {}
        if sl.spotanim_sent_id == S.ids.freeze_gfx and sl.spotanim_tick ~= nil and sl.spotanim_tick ~= m.gfx then
            m.gfx = sl.spotanim_tick
            if m.till == nil or m.till < F.tick then m.till = F.tick + V.FREEZE_TICKS end
        end
        if (sl.dx ~= 0 or sl.dz ~= 0) and m.till and m.till > F.tick and (m.gfx == nil or m.gfx < F.tick - 1) then
            m.till = nil
        end
        sl.frozen = m.till ~= nil and m.till > F.tick
        -- (and its immunity once a freeze ends, as a crab's: a barrage then is
        -- a cast wasted)
        if m.till ~= nil and m.till > (m.thaw_seen or -1) then m.thaw_seen = m.till end
        sl.immune = not sl.frozen and m.thaw_seen ~= nil and F.tick < m.thaw_seen + QD.MAIDEN_HM.IMMUNE
        S.slug_freeze[sl.slot] = m
    end
end

-- The wave, on the tick its crabs appear: every point, every wave; my side's
-- barrage slots (a DPS seat has none).
function QD.raid._maiden_hm_wave(S, F)
    local n = 0
    for _, c in ipairs(F.crabs) do
        if c.first == F.tick then
            assert(c.point, "maiden_hm_solve: a crab spawned on no spawn point, at "
                .. (c.x - S.base.x) .. "," .. (c.z - S.base.z))
            n = n + 1
        end
    end
    if n == 0 then return end
    S.waves = S.waves + 1
    S.wave = { t0 = F.tick, key = "hard " .. n, plan = { freeze = QD.MAIDEN_HM.FREEZE[S.role] or {}, dps = {} },
               next = 1, n = S.waves }
    S.wave_log[#S.wave_log + 1] = "w" .. S.waves .. " t" .. F.tick .. " " .. n .. " crabs"
    QD.raid._tob_trace(S, F.tick, "wave " .. S.waves .. " (" .. n .. " crabs)")
end

-- ===================================================================== ROLES

-- Which side of her a tile is on: seat 1 the north (z at or above her centre
-- row), seat 2 the south.
local function side_of(S, z)
    local V = QD.MAIDEN
    return (z >= S.base.z + V.BODY.z + V.SIZE // 2) and 1 or 2
end

-- The crab freezer `role` works on after its slots: its side's first thaw.
function QD.raid._maiden_hm_side_first(S, F, role)
    local first = nil
    for _, c in ipairs(F.crabs) do
        if side_of(S, c.z) == role
            and (first == nil or (c.thaw or 0) < (first.thaw or 0) or ((c.thaw or 0) == (first.thaw or 0) and c.slot < first.slot)) then
            first = c
        end
    end
    return first
end

local function crowd(F, c)
    local n = 0
    for _, o in ipairs(F.crabs) do
        if cheb(o.x, o.z, c.x, c.z) <= 1 then n = n + 1 end
    end
    return n
end

-- THE FREEZER after its slots: a thawed crab out of its immunity (the freeze
-- holds it), then its side's first thaw - a barrage on a clump (the damage
-- lands on all of them), the blowpipe on a lone crab - then the other side's.
-- Nil with no crab up.
function QD.raid._maiden_hm_freezer(S, F)
    local R = QD.raid._maiden_slots(S, F)
    if R then return R end
    local hot = nil
    for _, c in ipairs(F.crabs) do
        if not c.frozen and not c.immune and (hot == nil or c.eta < hot.eta or (c.eta == hot.eta and c.slot < hot.slot)) then hot = c end
    end
    if hot then return { target = hot, kind = "clump" } end
    local first = QD.raid._maiden_hm_side_first(S, F, S.role) or QD.raid._maiden_hm_side_first(S, F, 3 - S.role)
    if first == nil then return nil end
    if crowd(F, first) >= 2 then return { target = first, kind = "clump" } end
    return { target = first, kind = "pipe" }
end

-- THE DPS in a wave: the crabs in kill order - a walking one (nearest its
-- leak), then lone frozen ones by thaw, then clumped ones by thaw (the
-- freezers' barrages are on those), the freezers' own targets last - and DPS
-- seats go to them in that order, as many to a crab as its hitpoints left
-- need before it reaches her.
-- The Ancients DPS seat carries soul runes (its pouch's fourth slot: the
-- relay's Hard kit); a room test's kit does not, and it pipes as before.
function QD.raid._maiden_hm_blood_ok(S)
    if S.blood_ok == nil then
        local r, v = api_drive.varbit(S.ids.pouch_type_4)
        S.blood_ok = (r == "ok" and v == QD.MAIDEN_HM.SOUL_RUNE_INDEX)
    end
    return S.blood_ok
end

-- The frozen crab with the most crabs within 1 of it (Blood Barrage's 3x3,
-- player_magic.rs2 `npc_findallany($centre, 1, 0)`), two at least; the most,
-- then the nearest her, then the slot.
function QD.raid._maiden_hm_blood_clump(S, F)
    local best, bn = nil, 1
    for _, c in ipairs(F.crabs) do
        if c.frozen then
            local n = 0
            for _, o in ipairs(F.crabs) do
                if cheb(o.x, o.z, c.x, c.z) <= 1 then n = n + 1 end
            end
            if n > bn or (n == bn and best and (c.eta < best.eta or (c.eta == best.eta and c.slot < best.slot))) then
                best, bn = c, n
            end
        end
    end
    return best
end

function QD.raid._maiden_hm_dps(S, F)
    if #F.crabs == 0 then return nil end
    local skip = {}
    for _, r in ipairs({ 1, 2 }) do
        local c = QD.raid._maiden_hm_side_first(S, F, r)
        if c then skip[c.slot] = true end
    end
    -- BY THE TICKS TILL IT REACHES HER: walking, its distance; frozen, the
    -- freeze left and then its distance. The side's first freeze and a clump
    -- (the freezers' barrage targets) only break ties. Left to the freezers
    -- after their first freeze, the crabs at the two spawns nearest her
    -- leaked 46% of the time - thawed half-dead at +39..+42 and walked in,
    -- 5.1 leaks a run, each +3.5 to every storm after (all128f, 655 of 3,840;
    -- maiden_leaks.py)
    local function key(c)
        local left = c.eta
        if c.frozen then left = left + math.max((c.thaw or F.tick) - F.tick, 0) end
        if skip[c.slot] then left = left + 3 end
        if c.frozen and crowd(F, c) >= 2 then left = left + 2 end
        return left
    end
    local order = {}
    for _, c in ipairs(F.crabs) do order[#order + 1] = c end
    table.sort(order, function(a, b)
        local ka, kb = key(a), key(b)
        if ka ~= kb then return ka < kb end
        return a.slot < b.slot
    end)
    local k = 1
    for i, r in ipairs(QD.MAIDEN_HM.DPS) do if r == S.role then k = i end end
    -- AS MANY SEATS AS A CRAB NEEDS, in that order: a crab's hitpoints left
    -- (its bar; full before the first) against what one blowpipe lands
    -- before it reaches her. One seat a crab left the most urgent one - the
    -- spawn beside her, 6437,84 - leaking half the time, thawed with 60-90
    -- of its 100 taken (all128k: 187 of 373; maiden_leaks.py TILE=).
    local H = QD.MAIDEN_HM
    local seat, target = 0, nil
    for _, c in ipairs(order) do
        local hp = H.CRAB_HP
        if c.health_ratio ~= nil and c.health_ratio >= 0 and c.health_scale ~= nil and c.health_scale > 0 then
            hp = H.CRAB_HP * c.health_ratio / c.health_scale
        end
        local per_seat = H.PIPE_HIT * math.max(key(c), 1) / H.PIPE_TICKS
        seat = seat + math.max(1, math.ceil(hp / per_seat))
        if k <= seat then target = c break end
    end
    -- (more seats than the crabs need: the k-th again, as before)
    if target == nil then target = order[((k - 1) % #order) + 1] end
    return { target = target, kind = "pipecrab", station = QD.raid._maiden_hm_wave_tile(S) }
end

-- HARD MODE: the unfrozen blood spawn a freezer freezes - its half first,
-- nearest me - or nil.
function QD.raid._maiden_hm_slug(S, F)
    local mine, other = nil, nil
    for _, sl in ipairs(F.slugs) do
        if not sl.frozen and not sl.immune then
            local d = cheb(sl.x, sl.z, F.me.x, F.me.z)
            if side_of(S, sl.z) == S.role then
                if mine == nil or d < mine.d or (d == mine.d and sl.slot < mine.sl.slot) then mine = { sl = sl, d = d } end
            elseif other == nil or d < other.d or (d == other.d and sl.slot < other.sl.slot) then
                other = { sl = sl, d = d }
            end
        end
    end
    return (mine and mine.sl) or (other and other.sl) or nil
end

-- A DPS seat's tile on her between waves: the tank's turn goes round the
-- three every TANK_STORMS storms.
function QD.raid._maiden_hm_station(S)
    local H = QD.MAIDEN_HM
    if S.role == 1 or S.role == 2 then return H.STATION[S.role] end
    local turn = (S.storms // H.TANK_STORMS) % #H.DPS
    local off = 0
    for i, r in ipairs(H.DPS) do
        if i - 1 == turn then
            if r == S.role then return H.TANK_TILE end
        else
            off = off + 1
            if r == S.role then return H.OFF_TILES[off] end
        end
    end
    return H.TANK_TILE
end

local function is_tank(S)
    return QD.raid._maiden_hm_station(S) == QD.MAIDEN_HM.TANK_TILE
end

-- My wave tile: at 3 from her centre on my turn to tank, at 4 otherwise.
function QD.raid._maiden_hm_wave_tile(S)
    local H = QD.MAIDEN_HM
    if QD.raid._maiden_hm_station(S) == H.TANK_TILE then return H.WAVE_TILE[S.role] end
    return H.WAVE_FAR[S.role]
end

-- ===================================================================== STEP

-- The cast set on, the defender and the boots off (the freezer's full roll).
local function cast_gear(S, F, ids, intent)
    local V = QD.MAIDEN
    local off = {}
    if QD.raid._tob_worn(S, ids.defender) then off[#off + 1] = ids.worn_slot[V.WEAR_SHIELD] end
    if QD.raid._tob_worn(S, ids.boots) then off[#off + 1] = ids.worn_slot[V.WEAR_FEET] end
    if F.tick - (S.gear_sent or -10) >= 2 then intent.unequip = off end
    return QD.raid._maiden_missing(S, ids.cast_set), #off
end

function QD.raid._maiden_hm_step(S, F)
    local V, H, ids = QD.MAIDEN, QD.MAIDEN_HM, S.ids
    QD.raid._maiden_measure(S, F)
    if F.dying or (S.seen_boss and F.boss == nil) then
        QD.raid._tob_trace(S, F.tick, "her death")
        return "ok"
    end
    if F.boss == nil or S.base == nil then
        QD.raid._tob_emit(S, F, nil, {})
        return nil
    end
    if not S.seen_boss then
        S.seen_boss = true
        S.fight_start = F.tick
    end
    QD.raid._maiden_clock(S, F)
    QD.raid._maiden_pools(S, F)
    QD.raid._maiden_trails(S, F)
    QD.raid._maiden_crabs(S, F)
    QD.raid._maiden_hm_marks(S, F)
    QD.raid._maiden_hm_wave(S, F)
    local intent = {}
    local freezer = (S.role == 1 or S.role == 2)
    -- the blowpipe on Rapid, once (tob_maiden.lua's note)
    if not S.pipe_rapid and QD.raid._tob_worn(S, ids.pipe) then
        local mr, mode = api_drive.varp(ids.com_mode)
        assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
        if mode == 1 then
            S.pipe_rapid = true
            QD.raid._tob_trace(S, F.tick, "blowpipe on Rapid")
        elseif F.tick - (S.rapid_sent or -10) >= 2 then
            S.rapid_presses = (S.rapid_presses or 0) + 1
            assert(S.rapid_presses <= 3, S.who .. ": three Rapid presses and varp43_com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.rapid_slot, 1)
            assert(pr == "ok", S.who .. ": the Rapid press answered " .. tostring(pr) .. " " .. tostring(pd))
            S.rapid_sent = F.tick
        end
    end
    if not freezer and S.opener ~= "done" then
        if S.opener == nil then S.opener = "hammer" end
        if F.energy < 500 or F.tick - S.fight_start > V.OPENER_TICKS then
            S.opener = "done"
            QD.raid._tob_trace(S, F.tick, "hammer opener done (energy " .. F.energy .. ")")
        end
    end
    local target, kind, range, station = nil, nil, V.PIPE_RANGE, nil
    local want_set, boost, want_off = {}, nil, 0
    local slot = nil
    local prewave = #F.crabs == 0 and F.next_threshold ~= nil and F.permille <= F.next_threshold + H.PRE_WAVE
    if freezer then
        -- THE BLOOD SPAWNS ARE THE FREEZERS' (owner, 2026-10-12: "do the
        -- blood spawn freezing"; "The two freezing seats should do that"):
        -- every step a spawn takes lays a trail that never goes in Hard Mode,
        -- and with spawns frozen only when no crab job was open (and never
        -- near a wave) they walked 40% of their lives - 1096 trail tiles,
        -- the floor covered, the team stood in blood (ma128 r013, r082,
        -- r087). Now: a crab about to reach her first, then any spawn not
        -- frozen and not immune, then the rest of the crab work.
        local R = QD.raid._maiden_hm_freezer(S, F)
        local slug = QD.raid._maiden_hm_slug(S, F)
        local urgent = R ~= nil and R.target ~= nil and R.target.eta ~= nil and R.target.eta <= H.CRAB_URGENT
        if slug and R and not urgent then R = nil end
        if R then slug = nil end
        if slug then prewave = false end
        if R and R.kind == "pipe" then
            target, kind, range = R.target, "pipe", V.PIPE_RANGE
            want_set, boost = QD.raid._maiden_missing(S, ids.range_set), "rigour"
        elseif R or prewave then
            if R then
                target, kind, range, slot = R.target, R.kind, V.SPELL_RANGE, R.slot
            else
                target, kind, range, station = nil, "prewave", V.SPELL_RANGE, H.FREEZER_STATION[S.role]
            end
            want_set, want_off = cast_gear(S, F, ids, intent)
            boost = "augury"
        elseif slug then
            -- her blood spawns cannot be hurt; a barrage still freezes one
            target, kind, range = slug, "slugfreeze", V.SPELL_RANGE
            want_set, want_off = cast_gear(S, F, ids, intent)
            boost = "augury"
        else
            target, kind, station = F.boss, "her", H.STATION[S.role]
            want_set, boost = QD.raid._maiden_missing(S, ids.range_set), "rigour"
        end
    elseif S.opener == "hammer" then
        target, kind, range = F.boss, "hammer", 1
        want_set = QD.raid._tob_worn(S, ids.hammer) and {} or { ids.hammer }
        intent.spec = QD.raid._tob_worn(S, ids.hammer) and F.energy >= 500
        boost = "piety"
    else
        local R = QD.raid._maiden_hm_dps(S, F)
        -- BLOOD BARRAGE ON A FROZEN CLUMP (owner, 2026-10-12: "the other roles
        -- also bring magic and switch to blood barrage after the freezers
        -- have frozen the crabs"; "maiden should not have any leaks"): the
        -- Ancients DPS seat (the relay's seat 3, soul runes in its divine
        -- pouch) casts on the frozen crab with the most crabs in its 3x3 -
        -- the runs Maiden lost had 12.2 leaks a run to the rest's 4.5, in
        -- bursts the wave after a freeze (all128i)
        -- THE RESCUE FREEZE: a crab two ticks old and still unfrozen within
        -- RESCUE_ETA of her had its freeze splash (spotanim 85, ~8% of barrage
        -- landings) or none was aimed at it, and the freezers' next cast lands
        -- on the tick it arrives: 150 of 512 leaks were never frozen, most with
        -- no damage taken (all128l). The Ancients DPS seat's pouch holds Ice
        -- Barrage's runes, and its cast clock is free at a wave's start.
        local rescue = nil
        if S.role == H.BLOOD_SEAT then
            for _, c in ipairs(F.crabs) do
                if not c.frozen and not c.immune and c.born ~= nil and F.tick - c.born >= H.RESCUE_AGE
                    and c.eta <= H.RESCUE_ETA and (rescue == nil or c.eta < rescue.eta or (c.eta == rescue.eta and c.slot < rescue.slot)) then
                    rescue = c
                end
            end
        end
        local C = (rescue == nil and S.role == H.BLOOD_SEAT and QD.raid._maiden_hm_blood_ok(S)) and QD.raid._maiden_hm_blood_clump(S, F) or nil
        if rescue then
            target, kind, range = rescue, "rescue", V.SPELL_RANGE
            want_set, want_off = QD.raid._maiden_missing(S, ids.cast_set), 0
            boost = "augury"
            R = nil
        elseif C then
            target, kind, range = C, "blood", V.SPELL_RANGE
            -- (the cast set on, the defender and boots kept: their take-off is
            -- the freezers' accuracy, and seat 3 has no free slot for them - it
            -- held a blood target for 33 ticks bashing with the trident until
            -- an eat freed one, 53 of 127 wave ticks busy; all128k r001 t178-211)
            want_set, want_off = QD.raid._maiden_missing(S, ids.cast_set), 0
            boost = "augury"
            R = nil
        end
        if C or rescue then
        elseif R then
            target, kind, station, range = R.target, R.kind, R.station, V.PIPE_RANGE
            want_set, boost = QD.raid._maiden_missing(S, ids.range_set), "rigour"
        else
            target, kind, range = F.boss, "her", 1
            station = prewave and QD.raid._maiden_hm_wave_tile(S) or QD.raid._maiden_hm_station(S)
            want_set, boost = QD.raid._maiden_missing(S, ids.melee_set), "piety"
        end
    end
    if #want_set > 0 and F.tick - (S.gear_sent or -10) >= 2 then intent.gear = want_set end
    local ranged = boost == "rigour" or boost == "augury"
    QD.raid._tob_supplies(S, F, { overhead = "protectfrommagic", boost = boost,
        boost_stat = ranged and "ranged" or "attack",
        brew_below = (not freezer and is_tank(S)) and H.TANK_BREW or nil }, intent)
    if kind ~= S.target_kind then
        QD.raid._tob_trace(S, F.tick, "target " .. tostring(kind) .. (target and (" slot " .. tostring(target.slot)) or ""))
        S.target_kind = kind
    end
    local spec, names = QD.raid._maiden_spec(S, F, target, range, station)
    local plan = QD.raid._tob_plan(S, F, spec, names)
    local d = QD.raid._tob_order(S, F, plan, { target = target, range = range })
    -- the cast: as tob_maiden.lua's - a plan that moves walks, a plan that
    -- stands casts when the set is on and the last cast's 5 ticks are gone
    local casting = kind == "slot" or kind == "slot-wait" or kind == "clump" or kind == "slugfreeze" or kind == "blood"
        or kind == "rescue"
    if (freezer or kind == "blood" or kind == "rescue") and casting and d.order and d.order.mode == "attack" then
        if d.x ~= F.me.x or d.z ~= F.me.z then
            d.order = { mode = "walk", x = d.x, z = d.z }
        else
            d.order = nil
        end
        local ready = kind ~= "slot-wait" and d.order == nil and #want_set == 0 and want_off == 0
            and F.tick >= (S.next_cast or 0)
        if ready then
            intent.cast = { npc = target, component = (kind == "blood") and ids.blood_barrage or ids.barrage }
            S.order = nil
            S.next_cast = F.tick + V.CAST_EVERY
            if kind == "blood" then S.bloods = (S.bloods or 0) + 1 end
            if kind == "rescue" then S.rescues = (S.rescues or 0) + 1 end
            local what = kind
            if kind == "slot" then
                what = "w" .. S.wave.n .. " slot+" .. slot.at .. " cast+" .. (F.tick + S.cast_lag - S.wave.t0)
                S.wave.next = S.wave.next + 1
            end
            S.lag_probe = { tick = F.tick, slot = target.slot }
            S.cast_log[#S.cast_log + 1] = "t" .. F.tick .. " " .. what .. " " .. tostring(target.point)
                .. " eta" .. tostring(target.eta)
        end
    end
    QD.raid._tob_emit(S, F, d.order, intent)
    QD.raid._tob_recent(S, F, d)
    return nil
end

-- ===================================================================== LOOP

function QD.raid.maiden_hm_solve(opts)
    opts = opts or {}
    assert(QD.raid.tob_mode == "hard", "maiden_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    local S = QD.raid._tob_state("maiden_hm_solve", QD.raid._maiden_ids(), opts, {
        pools = {}, pool_seen = {}, trails = {}, slug_last = {}, crab_rec = {}, slug_freeze = {},
        pool_probe = {}, crab_probe = {}, cast_log = {},
        throws = 0, storms = 0, hm = true,
        waves = 0, wave_log = {}, freeze_log = {}, cast_lag = QD.MAIDEN.CAST_LAG_GUESS,
    })
    QD.raid._maiden_enter(S, opts)
    return QD.raid._tob_run(S, QD.raid._maiden_hm_step, function(s)
        return QD.raid._tob_summary(s, string.format("storms %d, throws %d; waves %s; casts %s; froze %s",
            s.storms, s.throws, table.concat(s.wave_log, " | "), table.concat(s.cast_log, " | "),
            table.concat(s.freeze_log, " ")))
    end)
end
