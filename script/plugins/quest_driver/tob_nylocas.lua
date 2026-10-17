-- quest-driver / tob_nylocas: THE NYLOCAS, Normal trio (waves 1-31, then
-- Nylocas Vasilias).
--
--   t.raid.nylocas_solve(opts) -> result, detail, record
--
-- Called by every seat at the room's entry (the corridor side of the
-- barrier, north of it). The leader starts the room at once (nothing in the
-- room moves before the start: the supports are added by it) and a member
-- crosses once it sees the leader inside. Then the fight on the shared loop
-- (tob.lua): MEASURE -> CLOCK -> CONTEXT -> PLAN -> ORDER + EMIT, one decision
-- a server tick. It returns "ok" at Vasilias' death. The plan is
-- ROOM_SOLVERS.md 4.3.1; the spec is solver_specs/nylocas.md.
--
-- Tiles are local to the room's 64-aligned square. Tick mapping (lesson 2):
-- an npc's decision on T reads raiders where they stood at the end of T-1; my
-- press after seeing d resolves in d+1's player phase, after d+1's npc phase.
--
--   waves        a nylo walks its lane to the box (x 26..37, z 19..30), then
--                either to its support (a walker: chews it every 3 ticks) or,
--                swapped to its `fighting` form at the mouth, at the nearest
--                raider (an aggro: swings every 3, its style's prayer zeroes
--                it). Waves >= 16 flicker: their style changes at +5 and +7.
--   detonation   every nylo explodes at birth+51 (small) / +52 (big) on every
--                raider within 2 of its footprint, read at the end of the tick
--                before, 1..18 / 1..21, unprayable -> a lethal zone.
--   style        only a hit of the nylo's own style lands; one wrong-style hit
--                nulls that raider on that nylo for its life.
--   supports     230 hp each at scale 3; a collapse is 1..50 on everyone.
--   Vasilias     lands 16+ ticks after the last nylo leaves, melee form M two
--                ticks later, a colour change at M+9 and every 10 after (to
--                one of the other two, a coin); only her colour's style hurts
--                her, a wrong one is reflected and heals her.
--
-- THE ROLES are by STYLE, by seat (wiki trio: one mager, one melee, one
-- ranger; Blert's trios 76-79% own-style attacks): seat 1 the ranger
-- (blowpipe on Rapid, Rigour), seat 2 the melee (tentacle, Piety), seat 3 the
-- mage (the trident, a powered staff: an attack, not a cast; Augury). Every
-- seat carries the Learner kit's three sets, all three wear Vasilias' colour.
-- ==========================================================================

QD.NYLO = {
    BOX = { x0 = 26, z0 = 19, x1 = 37, z1 = 30 },
    ENTRY = { x = 31, z = 33 },                 -- north of the barrier (z 31)
    -- the supports' SW tiles (size 3); a walker chews within 2 of the centre
    SUPPORTS = { { x = 25, z = 18 }, { x = 25, z = 29 }, { x = 36, z = 18 }, { x = 36, z = 29 } },
    -- Blert's modal stands by role (solver_specs/nylocas.md 5)
    STATION = { [1] = { x = 27, z = 25 }, [2] = { x = 31, z = 21 }, [3] = { x = 36, z = 25 } },
    STYLE_OF_SEAT = { [1] = "ranged", [2] = "melee", [3] = "magic" },
    -- the kit's reach: tentacle beside, blowpipe 5, trident 7
    RANGE = { melee = 1, ranged = 5, magic = 7 },
    -- a lane nylo a ranged or magic seat may shoot: this many tiles out of
    -- the box, on its lane's straight line (the lanes are walled corridors)
    LANE_SHOT = 6,
    LIFE = { small = 51, big = 52 },            -- the detonation's tick after birth
    DET_REACH = 2,
    FLICKER_WAVE = 16, SETTLE_AGE = 7,          -- c shows at the end of +7
    AGGRO_NEAR = 8,
    BOSS_SIZE = 4, BOSS_LAND = { x = 30, z = 23 },
    SWITCH_FIRST = 9, SWITCH_EVERY = 10, BOSS_REACH = 8,
    H = 8, BEAM = 32,
    DET_COST = 21, STATION_PULL = 0.2,
    SUPPORT_LOW = 25,                           -- percent: eat over a collapse's 50
    LET_DETONATE = 15,                          -- ticks left: it pops before it matters
    BARRAGE_SEAT = 3, SPELL_RANGE = 10, CAST_EVERY = 5,
    BARRAGE_MIN = 2,                            -- magic nylos in its 3x3 to cast (the mage seat)
    PREP = 6, PREP_OLD = 25,                    -- ticks before a wave; an "old" target's life left
    RATE = { ranged = 2, melee = 4, magic = 4 },  -- the kit's attack speeds (blowpipe on Rapid)
    CHEW_WEIGHT = 25,                           -- a chewer's value x (1 + chewed% / this)
    AGGRO_BONUS = 150,                          -- an aggro: above every nylocas but a dying pillar's chewer
    SAVE_BONUS = 200,                           -- a chewer on a pillar under SUPPORT_SAVE
    AGGRO_COST = 9,                             -- a melee aggro's swing zone, per tick                           -- ticks of value an aggro is worth on top
    KEEP_RATIO = 1.5,
    WALK_COST = 1.0,                            -- ticks charged a tile out of reach
    SWITCH_COST = 6,                            -- ticks a switch is charged while I have my own to hit
    HELP_MARGIN = 2,                            -- another style this many more: help it
    SUPPORT_HELP = 50,                          -- percent: its chewers first under this
    SUPPORT_SAVE = 30,                          -- percent: its chewers outrank all else
    EAT_COLLAPSE = 51,
}

local function cheb(ax, az, bx, bz) return QD.raid._tob_cheb(ax, az, bx, bz) end
local function gap(x, z, fx, fz, n) return QD.raid._tob_gap(x, z, fx, fz, n) end

function QD.raid._nylo_ids()
    local ids = QD.raid._tob_common_ids("nylocas_solve")
    local room = QD.raid._tob_symbols("nylocas_solve", {
        { "support", "npc", "tob_nylocas_support" },
        { "boss_spawning", "npc", "nylocas_boss_spawning" }, { "boss_melee", "npc", "nylocas_boss_melee" },
        { "boss_magic", "npc", "nylocas_boss_magic" }, { "boss_ranged", "npc", "nylocas_boss_ranged" },
        { "shielded", "spotanim", "tob_nylocas_shielded" },
        { "archer_helm", "obj", "game_pest_archer_helm" }, { "pipe", "obj", "toxic_blowpipe_loaded" },
        { "anguish", "obj", "zenyte_necklace_enchanted" }, { "assembler", "obj", "avas_assembler" },
        { "mage_helm", "obj", "game_pest_mage_helm" }, { "trident", "obj", "toxic_tots_charged" },
        { "occult", "obj", "occult_necklace" }, { "imbued_cape", "obj", "ma2_saradomin_cape" },
        { "defender", "obj", "dragon_parryingdagger" }, { "boots", "obj", "dragon_boots" },
        { "melee_helm", "obj", "game_pest_melee_helm" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "torture", "obj", "zenyte_amulet_enchanted" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "rapid_slot", "component", "combat_interface:style_slot_1" },
        { "barrage", "component", "magic_spellbook:ice_barrage" },
        { "com_mode", "varp", "varp43_com_mode" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "nylocas_solve: no combat tab")
    ids.combat_tab = combat_tab
    -- every nylo form: its style, size and whether it hunts raiders
    ids.nylo = {}
    for _, size in ipairs({ "", "big_" }) do
        for _, form in ipairs({ "incoming", "fighting" }) do
            for _, style in ipairs({ "melee", "ranged", "magic" }) do
                local name = QD.raid.tob_npc_name("tob_nylocas_" .. size .. form .. "_" .. style)
                local r, id = api_drive.symbol("npc", name)
                assert(r == "ok", "nylocas_solve: no npc named " .. name)
                ids.nylo[id] = { style = style, big = (size == "big_"), aggro = (form == "fighting") }
            end
        end
    end
    ids.boss_style = { [ids.boss_melee] = "melee", [ids.boss_magic] = "magic", [ids.boss_ranged] = "ranged" }
    ids.sets = {
        ranged = { ids.archer_helm, ids.pipe, ids.anguish, ids.assembler },
        melee = { ids.melee_helm, ids.tentacle, ids.torture, ids.fire_cape, ids.defender },
        magic = { ids.mage_helm, ids.trident, ids.occult, ids.imbued_cape },
    }
    ids.weapon = { ranged = ids.pipe, melee = ids.tentacle, magic = ids.trident }
    ids.boost = { ranged = "rigour", melee = "piety", magic = "augury" }
    ids.boost_stat = { ranged = "ranged", melee = "attack", magic = "magic" }
    ids.protect = { ranged = "protectfrommissiles", melee = "protectfrommelee", magic = "protectfrommagic" }
    return ids
end

-- ==================================================================== MEASURE

local function in_box(V, lx, lz, n)
    return lx >= V.BOX.x0 and lx + n - 1 <= V.BOX.x1 and lz >= V.BOX.z0 and lz + n - 1 <= V.BOX.z1
end

-- A nylo first seen at local (lx, lz): its lane and its birth, by the steps
-- from its lane's spawn tile (one a tick from S+1; nothing moves on S). nil
-- when it is not in a lane (a split, born on its first sight).
local function lane_birth(lx, lz, big, tick)
    if lx < 26 and (lz == 24 or lz == 25) then return tick - (lx - 17), "W" end
    if lz < 19 and (lx == 31 or lx == 32) then return tick - (lz - 9), "S" end
    if lx >= 38 and (lz == 24 or lz == 25) then return tick - ((big and 45 or 46) - lx), "E" end
    return nil, nil
end

-- The room's facts this tick: the supports, Vasilias, every nylo by LIFE (a
-- slot that vanished and is filled again is a new nylo: a big's despawn and
-- its splits' spawn can share a tick and a slot).
function QD.raid._nylo_measure(S, F)
    local V, ids = S.V, S.ids
    if S.base == nil then
        S.base = { x = F.me.x - F.me.x % 64, z = F.me.z - F.me.z % 64 }
    end
    local bx, bz = S.base.x, S.base.z
    F.supports, F.nylos, F.boss = {}, {}, nil
    local fresh = {}
    for _, row in ipairs(F.npcs) do
        local kind = ids.nylo[row.npc_id]
        if row.npc_id == ids.support then
            local pct = 100
            if row.health_ratio ~= nil and row.health_ratio >= 0 and (row.health_scale or 0) > 0 then
                pct = math.floor(100 * row.health_ratio / row.health_scale)
            end
            F.supports[#F.supports + 1] = { x = row.x, z = row.z, pct = pct, row = row }
        elseif ids.boss_style[row.npc_id] or row.npc_id == ids.boss_spawning then
            F.boss = row
        elseif kind then
            local lx, lz = row.x - bx, row.z - bz
            local L = S.lives[row.slot]
            local new = L == nil or L.last < F.tick - 2 or L.big ~= kind.big
                or cheb(L.x, L.z, row.x, row.z) > 3
            if new then
                local birth, lane = lane_birth(lx, lz, kind.big, F.tick)
                L = { slot = row.slot, big = kind.big, birth = birth or F.tick, lane = lane,
                      key = row.slot .. "@" .. F.tick, first = F.tick, x = row.x, z = row.z }
                S.lives[row.slot] = L
                if lane then
                    -- A WAVE IS SEEN ON ITS SPAWN TILES, on the 4-tick grid: a
                    -- nylocas does not move on its spawn tick, so one seen on a
                    -- spawn tile was born now; one first seen further down its
                    -- lane (held behind another, out of view) only JOINS the
                    -- latest wave at or before its estimate (seeds n15: births
                    -- estimated for held nylos counted 35 waves, and the
                    -- cleanup began during wave 28)
                    local on_spawn = (lx == 17 and (lz == 24 or lz == 25)) or (lz == 9 and (lx == 31 or lx == 32))
                        or (lz == 24 and (lx == 45 or lx == 46)) or (lx == 46 and lz == 25)
                    local grid = S.wave1 == nil or (F.tick - S.wave1) % 4 == 0
                    if on_spawn and grid and S.wave_at[F.tick] == nil then
                        S.wave1 = S.wave1 or F.tick
                        S.waves = S.waves + 1
                        S.wave_at[F.tick] = S.waves
                        S.wave_last = F.tick
                        S.wave_log[#S.wave_log + 1] = "w" .. S.waves .. "@" .. (F.tick - (S.t0 or F.tick))
                    end
                    if on_spawn and grid then L.birth = F.tick end
                    local at = nil
                    for bt in pairs(S.wave_at) do
                        if bt <= L.birth and (at == nil or bt > at) then at = bt end
                    end
                    L.wave = at and S.wave_at[at] or nil
                else
                    S.splits = S.splits + 1
                end
                fresh[#fresh + 1] = L
            end
            L.moved = (L.x ~= row.x or L.z ~= row.z)
            L.dx, L.dz = row.x - L.x, row.z - L.z
            L.x, L.z, L.last = row.x, row.z, F.tick
            L.style, L.aggro, L.row = kind.style, kind.aggro, row
            L.size = kind.big and 2 or 1
            L.det = L.birth + (kind.big and V.LIFE.big or V.LIFE.small)
            L.dying = row.health_ratio == 0 and (row.health_scale or 0) > 0
            F.nylos[#F.nylos + 1] = L
        end
    end
    if S.t0 == nil and #F.supports > 0 then
        S.t0 = F.tick
        QD.raid._tob_trace(S, F.tick, "the supports are up (room tick 0)")
    end
    -- the lives not seen for 3 ticks are gone
    for slot, L in pairs(S.lives) do
        if L.last < F.tick - 2 then S.lives[slot] = nil end
    end
    -- MY NULLS: the shielded graphic on my target's tile on the tick of my
    -- swing (tob_damage.rs2): that nylo is dead to me for its life
    local r, rows = api_drive.spotanims(0)
    if r == "ok" and S.last_target_life then
        local T = S.last_target_life
        for _, row in ipairs(rows) do
            if row.spotanim_id == ids.shielded and row.x == T.x and row.z == T.z and not S.nulled[T.key]
                and (row.cycles_left or 0) > 0 and T.last == F.tick then
                S.nulled[T.key] = true
                S.nulls = S.nulls + 1
                QD.raid._tob_trace(S, F.tick, "NULLED on slot " .. T.slot .. " (" .. T.style .. ")")
            end
        end
    end
    F.pct_low = 100
    for _, sp in ipairs(F.supports) do F.pct_low = math.min(F.pct_low, sp.pct) end
end

-- ===================================================================== TARGET

-- Can I hit this nylo with `style` from a tile of the box? Its style is mine,
-- I am not nulled on it, it is not dying, a flicker has settled, and it is in
-- the box (melee) or in the box or LANE_SHOT out on its lane (ranged, magic).
local function attackable(S, F, L, style)
    local V = S.V
    if L.style ~= style or L.dying or S.nulled[L.key] then return false end
    -- A FLICKER MUST NEVER HAVE MY ATTACK ON IT: a running attack order
    -- swings by itself, and a swing on the tick it changes is the wrong style
    -- (seed n24/sm t372: a trident order on a magic nylocas swung after it
    -- turned ranged). Waves >= 16 flicker until +7; a lane nylocas whose wave
    -- is not known (first seen off its spawn tile) is treated as one.
    if L.lane and (L.wave == nil or L.wave >= V.FLICKER_WAVE) and F.tick < L.birth + V.SETTLE_AGE + 1 then
        return false
    end
    local lx, lz = L.x - S.base.x, L.z - S.base.z
    if in_box(V, lx, lz, L.size) then return true end
    if style == "melee" then return false end
    return lx >= V.BOX.x0 - V.LANE_SHOT and lx + L.size - 1 <= V.BOX.x1 + V.LANE_SHOT
        and lz >= V.BOX.z0 - V.LANE_SHOT and lz + L.size - 1 <= V.BOX.z1
end

QD.raid._nylo_attackable = function(S, F, L, style) return attackable(S, F, L, style) end

-- ======================================================================= SPEC

-- ===================================================================== STEP

-- ==================================================================== PHASES
--
-- THE ROOM AS A STATE MACHINE (the owner, 2026-10-09: as Verzik's), one
-- MEASURE -> DECIDE -> ACT a tick inside each phase:
--   entry     walk to the barrier; the leader starts, a member crosses
--             (QD.raid._nylo_enter, before the loop)
--   waves     wave 31 not yet out: my style's nylos, cross-help, the
--             support emergency
--   cleanup   wave 31 out, nylos left: every nylo is anyone's, the room ends
--             at the last despawn (Vasilias lands 16+ after it)
--   boss_due  the room empty, no Vasilias yet: full hp and prayer, Protect
--             from Melee, the melee set on, beside her landing (30..33,23..26)
--   boss      her colour from her npc id: that colour's set, prayer and attack
--   done      her row gone
function QD.raid._nylo_phase(S, F)
    local phase = S.phase
    if F.boss then
        phase = "boss"
    elseif S.boss_seen then
        phase = "done"
    elseif S.waves >= 31 and #F.nylos == 0 then
        phase = "boss_due"
    elseif S.waves >= 31 then
        phase = "cleanup"
    else
        phase = "waves"
    end
    if phase ~= S.phase then
        QD.raid._tob_trace(S, F.tick, "phase " .. phase .. " (room tick " .. (F.tick - (S.t0 or F.tick)) .. ")")
        S.phase_log[#S.phase_log + 1] = phase .. "@" .. (F.tick - (S.t0 or F.tick))
        S.phase = phase
    end
    return phase
end

-- THE BARRAGE (the mage seat, Ancient Magicks in the kit; wiki: "Magers and
-- rangers should prioritise killing clumps of nylocas with barrage"): the
-- magic nylocas with the most magic ones within 1 of it, cast on only when
-- NOTHING else is in its 3x3 splash - the style check runs per target and a
-- non-magic one in it would null me on it (solver_specs/nylocas.md 3.4) - and
-- nothing in it is one I am nulled on.
function QD.raid._nylo_clump(S, F)
    local V = S.V
    local best, bn = nil, V.BARRAGE_MIN - 1
    -- where a nylocas stands when the cast resolves: a walker one more step
    -- the way it last stepped (lane walks and support walks are straight
    -- lines and diagonals), a standing one where it is
    local function next_tile(L)
        if L.moved then return L.x + (L.dx or 0), L.z + (L.dz or 0) end
        return L.x, L.z
    end
    for _, C in ipairs(F.nylos) do
        if attackable(S, F, C, "magic") and not C.moved then
            local n, bad = 0, false
            local cx, cz = next_tile(C)
            for _, O in ipairs(F.nylos) do
                local ox, oz = next_tile(O)
                local g = gap(cx, cz, ox, oz, O.size)
                -- the splash is the 3x3 round the primary (player_magic.rs2:
                -- `npc_findallany($centre, 1, 0)`), a big by its whole
                -- footprint, AS IT WILL BE when the cast resolves, after the
                -- next npc phase: a walker can step into it, a walking primary
                -- moves it (seed n23/sd t345: two melee smalls stepped into a
                -- radius-1 check's splash and were nulled)
                -- WRONG STYLE IS ZERO, so the check is strict: a non-magic
                -- one (or one I am nulled on, or one dying, whose bar may lie)
                -- within 2 of the splash, now or after its step, forbids the
                -- cast (seed n25/sd t365: a radius-1 projection let a stack of
                -- two melee smalls into the splash). The projected 3x3 is the
                -- count, of standing magic ones.
                local g_now = gap(C.x, C.z, O.x, O.z, O.size)
                if (g <= 2 or g_now <= 2) and (O.style ~= "magic" or S.nulled[O.key] or O.dying) then bad = true end
                if not O.dying then
                    if g <= 1 and O.style == "magic" and not O.moved then n = n + 1 end
                end
            end
            if not bad and (n > bn or (n == bn and best and C.slot < best.slot)) then best, bn = C, n end
        end
    end
    return best, bn
end

-- DECIDE, BOSS: her colour is her npc id; the melee form's first sight is M,
-- her changes at M+9+10k. NEVER a press that resolves on a predicted change
-- tick: it swings after the retype, a wrong style, reflected and healed.
function QD.raid._nylo_decide_boss(S, F)
    local V, ids = S.V, S.ids
    local colour = ids.boss_style[F.boss.npc_id]
    if colour and S.boss_m == nil then
        S.boss_m = F.tick
        QD.raid._tob_trace(S, F.tick, "her melee form (M)")
    end
    if colour and colour ~= S.boss_colour then
        if S.boss_colour then
            local since = F.tick - S.boss_m - V.SWITCH_FIRST
            S.switch_log[#S.switch_log + 1] = colour:sub(1, 3) .. "@M+" .. (F.tick - S.boss_m)
                .. ((since >= 0 and since % V.SWITCH_EVERY == 0) and "" or "!")
        end
        S.boss_colour = colour
    end
    local D = { style = colour or "melee", kind = colour and ("boss:" .. colour) or "boss:landing",
                overhead = ids.protect[colour or "melee"], boss = true }
    if colour then
        D.target, D.range = F.boss, V.RANGE[D.style]
        local next_at = F.tick + 1 - S.boss_m - V.SWITCH_FIRST
        D.hold = next_at >= 0 and next_at % V.SWITCH_EVERY == 0
    else
        D.range = 1
    end
    return D
end

-- ===================================================================== STEP

-- ==================================================================== ENTRY

-- THE ENTRY PHASE: the room is arrived at on (31,49), 18 tiles north of the
-- barrier on z=31 (tob.constant), so the walk to (31,33) puts it in view; then
-- the shared start (the leader answers at once: nothing in the room moves
-- before the start, the supports are added by it).
function QD.raid._nylo_enter(S, opts)
    local tr, me = api_drive.player_tile()
    assert(tr == "ok", "nylocas_solve: no tile")
    local ex, ez = me.x - me.x % 64 + S.V.ENTRY.x, me.z - me.z % 64 + S.V.ENTRY.z
    local deadline = api_drive.tick() + 60
    QD.raid._tob_trace(S, api_drive.tick(), "phase entry")
    while true do
        local F = QD.raid._tob_measure(S)
        if F.me.x == ex and F.me.z == ez then break end
        assert(F.tick <= deadline, "nylocas_solve: never reached the entry tile from " .. F.me.x .. "," .. F.me.z)
        if F.tick - (S.walk_sent or -10) >= 3 then
            api_drive.move_to(ex, ez)
            S.walk_sent = F.tick
        end
        await({ event = "server_tick", match = function() return true end, note = "nylocas_solve: to the entry" }, 3)
    end
    QD.raid._tob_start(S, nil, opts.start_ticks)
    S.fight_start = api_drive.tick()
end

-- ===================================================================== LOOP
