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
                local name = "tob_nylocas_" .. size .. form .. "_" .. style
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
    local V, ids = QD.NYLO, S.ids
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
    local V = QD.NYLO
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

-- The nearest support centre to a nylo (Chebyshev, local): a walker's goal.
local function support_dist(S, L)
    local best = nil
    for _, sp in ipairs(QD.NYLO.SUPPORTS) do
        local d = gap(sp.x + S.base.x + 1, sp.z + S.base.z + 1, L.x, L.z, L.size)
        if best == nil or d < best then best = d end
    end
    return best
end

-- The support a CHEWER is on (within 2 of its centre) and that support's bar
-- in percent; nil for a nylo that is not chewing.
local function chewing(S, F, L)
    if L.aggro then return nil end
    for _, sp in ipairs(F.supports) do
        if gap(sp.x + 1, sp.z + 1, L.x, L.z, L.size) <= 2 then return sp end
    end
    return nil
end

-- THE STYLE I AM WEARING, from the weapon in hand (nil: none of the three).
local function worn_style(S)
    local ids = S.ids
    for _, st in ipairs({ "ranged", "melee", "magic" }) do
        if QD.raid._tob_worn(S, ids.weapon[st]) then return st end
    end
    return nil
end

-- WHAT A KILL IS WORTH, in the guides' order:
--   aggros first (wiki: "These aggro's should be prioritised first");
--   then the newest - the life it has left is the chewing and the cap slot a
--   kill saves (wiki: "kill newly spawned nylocas ... prioritising the
--   smaller ones first"; Blert: "Prioritizing newer Nylocas and allowing
--   older ones to expire naturally") - a big counting half before the cap
--   rises (smalls first) and half again more from wave 20 (Blert: "In
--   post-cap, the focus generally shifts to killing bigs quickly to get their
--   splits out early"); in the cleanup a big's life runs on through its
--   splits;
--   and a chewer weighs by its pillar: x6 under 30%, x3 under 50%, a little
--   above (the owner, 2026-10-09: "add some weight to the nylos attacking
--   pillars that are about to die").
local function kill_value(S, F, L)
    local V = QD.NYLO
    local left = L.det - F.tick
    if L.size == 2 then
        if S.phase == "cleanup" then
            left = left + 3 + V.LIFE.small + 1
        elseif S.waves < 20 then
            left = left / 2
        else
            left = left * 1.5
        end
    end
    local v = left
    local sp = chewing(S, F, L)
    if sp then
        if sp.pct < V.SUPPORT_SAVE then
            -- a pillar about to fall outranks everything but an aggro, in the
            -- cleanup too, where a big's splits made it the top value (seeds
            -- e5: every collapse fell in the cleanup, room ticks 333..362)
            v = v * 6 + V.SAVE_BONUS
        elseif sp.pct < V.SUPPORT_HELP then
            v = v * 3
        else
            v = v * (1 + (100 - sp.pct) / V.CHEW_WEIGHT)
        end
    end
    if L.aggro then v = v + V.AGGRO_BONUS end
    return v
end

-- WHAT IT COSTS ME: the set's switch, the running to reach it (2 tiles a
-- tick), and the weapon's speed for the hits it needs (a big two, one once
-- its bar is half gone). A switch is cheap (its one tick) when I have nothing
-- of my own to hit (wiki: "always be on the attack ... switch weapons") or
-- the nylocas is chewing a pillar under SUPPORT_SAVE; otherwise it is
-- SWITCH_COST (the owner: "switching might be overkill ... I would focus
-- that [the dying pillars] over switching").
local function kill_ticks(S, F, L, style, wearing, idle)
    local V = QD.NYLO
    local out = math.max(0, gap(F.me.x, F.me.z, L.x, L.z, L.size) - V.RANGE[style])
    local hits = 1
    if L.size == 2 then
        local r = L.row
        local half = r and r.health_ratio and r.health_scale and r.health_scale > 0
            and r.health_ratio * 2 <= r.health_scale
        hits = half and 1 or 2
    end
    local switch = 0
    if style ~= wearing then
        local sp = chewing(S, F, L)
        switch = (idle or (sp and sp.pct < V.SUPPORT_SAVE)) and 1 or V.SWITCH_COST
    end
    -- a tile of running is charged a tick, not the half a run takes: the
    -- target walks on and the attack starts over (seeds e3: 55% of the
    -- seats' walks were approaches)
    return switch + out * V.WALK_COST + V.RATE[style] * (hits - 1) + 1
end

-- MY TARGET AND THE STYLE I HIT IT WITH, measured: every nylocas any of my
-- three sets can hit, scored as value per tick (kill_value / kill_ticks), the
-- best taken. Switching styles is chosen when it is worth more per tick than
-- staying (the owner, 2026-10-09: "if there is a good measure that it will
-- help more than if you stayed"). Another style's single best nylocas is left
-- to that style's seat; the target I have is kept unless something scores
-- KEEP_RATIO times better.
function QD.raid._nylo_target(S, F, own)
    local V = QD.NYLO
    local wearing = worn_style(S)
    local first_of = {}
    for _, st in ipairs({ "melee", "ranged", "magic" }) do
        if st ~= own then
            local fb, fv = nil, nil
            for _, L in ipairs(F.nylos) do
                if attackable(S, F, L, st) then
                    local v = kill_value(S, F, L)
                    if fv == nil or v > fv or (v == fv and L.slot < fb.slot) then fb, fv = L, v end
                end
            end
            first_of[st] = fb and fb.key or nil
        end
    end
    local best, bstyle, bscore = nil, nil, nil
    local kept, kscore = nil, nil
    -- idle: nothing of my own style worth a hit
    local idle = true
    for _, L in ipairs(F.nylos) do
        if L.style == own and attackable(S, F, L, own)
            and not (L.det - F.tick <= V.LET_DETONATE and not (S.phase == "cleanup" and L.size == 2)) then
            idle = false
        end
    end
    for _, L in ipairs(F.nylos) do
        local st = L.style
        local spent = L.det - F.tick <= V.LET_DETONATE and not (S.phase == "cleanup" and L.size == 2)
        -- HELP IS MELEE ONLY (the owner, 2026-10-09: "the seats only switch to
        -- melee to help"): my own style, or another's nylocas in melee
        local allowed = st == own or st == "melee"
        if allowed and not spent and attackable(S, F, L, st) and (st == own or first_of[st] ~= L.key) then
            local score = kill_value(S, F, L) / kill_ticks(S, F, L, st, wearing, idle)
            if L.key == S.target_key then kept, kscore = L, score end
            if bscore == nil or score > bscore or (score == bscore and L.slot < best.slot) then
                best, bstyle, bscore = L, st, score
            end
        end
    end
    if kept and kscore * V.KEEP_RATIO >= bscore then best, bstyle = kept, kept.style end
    S.target_key = best and best.key or nil
    S.target_style = best and bstyle or nil
    if best and bstyle ~= own then S.helps = (S.helps or 0) + 1 end
    return best, (best and bstyle or own)
end

-- ======================================================================= SPEC

function QD.raid._nylo_spec(S, F, target, range, station)
    local V = QD.NYLO
    local B = V.BOX
    local spec, names, add = QD.raid._tob_spec(S, F, {
        h = V.H, beam = V.BEAM,
        edge = { x0 = S.base.x + B.x0, z0 = S.base.z + B.z0, x1 = S.base.x + B.x1, z1 = S.base.z + B.z1,
                 margin = 0, weight = 1.0 },
    })
    -- THE DETONATIONS: everyone within 2 of its footprint at the end of
    -- det-1. A first sight off its spawn tile can put the birth a tick out,
    -- so det-2 too; a nylo that moves is a tile wider.
    for _, L in ipairs(F.nylos) do
        if not L.dying and L.det - 1 <= F.tick + V.H and L.det >= F.tick then
            local grow = (L.aggro or L.moved) and 1 or 0
            add.zone("detonation", { x = L.x - grow, z = L.z - grow, size = L.size + 2 * grow,
                lo = 0, hi = V.DET_REACH, t0 = math.max(F.tick + 1, L.det - 2), t1 = L.det - 1,
                tier = "lethal", cost = V.DET_COST })
        end
    end
    -- A MELEE AGGRO HUNTING ME: beside its footprint is where it swings
    -- (diagonals count), so that is a damage zone the plan steps out of;
    -- the prayer is kept for the ones at range
    -- (not while Protect from Melee is lit: then its swing is 0, and a zone
    -- kept the melee seat off the aggro it was there to kill - 40% of the
    -- seats' walks were dodges with the target in reach)
    for _, L in ipairs(F.nylos) do
        if L.aggro and L.style == "melee" and not L.dying and not F.lit.protectfrommelee then
            local mine = gap(F.me.x, F.me.z, L.x, L.z, L.size)
            local nearest = true
            for _, rd in ipairs(F.raiders) do
                if not rd.me and gap(rd.x, rd.z, L.x, L.z, L.size) < mine then nearest = false end
            end
            if nearest and mine <= V.AGGRO_NEAR then
                local grow = L.moved and 1 or 0
                add.zone("aggro-melee", { x = L.x - grow, z = L.z - grow, size = L.size + 2 * grow, lo = 0, hi = 1,
                    t0 = F.tick + 1, t1 = F.tick + 3, tier = "damage", cost = V.AGGRO_COST })
            end
        end
    end
    if target then add.reach("reach", { x = target.x, z = target.z, size = target.size or 1 }, range) end
    -- the station only when idle: a pull toward it while a target is in
    -- reach steps me off the attack (seeds n20: 25-33% of each seat's wave
    -- ticks were walks)
    if station and not target then
        add.pull({ x = S.base.x + station.x, z = S.base.z + station.z, size = 1, weight = V.STATION_PULL,
            t0 = F.tick, t1 = F.tick + V.H })
    end
    return spec, names
end

-- The objs of `set` not worn now.
local function missing(S, set)
    local out = {}
    for _, obj in ipairs(set) do
        if not QD.raid._tob_worn(S, obj) then out[#out + 1] = obj end
    end
    return out
end

-- THE PIPE ON RAPID (as the Maiden's freezer): the combat tab's style_slot_1
-- until varp43_com_mode reads 1, once the blowpipe is in hand. By slot: the
-- names are a clientscript's text and scriptrun runs none.
local function pipe_rapid(S, F)
    local ids = S.ids
    if S.pipe_rapid or not QD.raid._tob_worn(S, ids.pipe) then return end
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

-- THE OVERHEAD IN THE WAVES: an aggro swings at the raider NEAREST it
-- (tob_nylocas.rs2 `~tob_nylo_fight_tick`; a tie to the first found), so only
-- the aggros whose nearest raider is me and that can reach me (melee beside,
-- the others 8) are mine to pray against, each weighed by its max hit (17 a
-- small, 24 a big); the heaviest style is prayed, and with none the prayer is
-- left as it is. Counting every aggro in reach prayed against the ones hunting
-- someone else (seeds n31: the ranger took 120..290 from aggro swings).
local function wave_overhead(S, F)
    local V = QD.NYLO
    local weight = { melee = 0, ranged = 0, magic = 0 }
    for _, L in ipairs(F.nylos) do
        if L.aggro and not L.dying then
            local mine = gap(F.me.x, F.me.z, L.x, L.z, L.size)
            local nearest = true
            for _, rd in ipairs(F.raiders) do
                if not rd.me and gap(rd.x, rd.z, L.x, L.z, L.size) < mine then nearest = false end
            end
            -- WHAT IS ATTACKING ME: an aggro facing me (its row's `facing` is
            -- my pid + 32768) or, a tick before it turns, the one whose
            -- nearest raider I am and that can reach me
            local reach = (L.style == "melee") and 2 or V.AGGRO_NEAR
            local facing_me = F.pid and L.row and L.row.facing == F.pid + 32768
            if facing_me or (nearest and mine <= reach) then
                -- a melee aggro reaches 1 and is stepped away from (its zone
                -- in the spec); one at range 8 cannot be, so it is the
                -- prayer's: melee counts half (seed n33/sc t359: two big melee
                -- aggros beside the ranger outweighed the big magic one that
                -- hit it through Protect from Melee)
                local w = (L.size == 2) and 24 or 17
                if L.style == "melee" then w = w / 2 end
                weight[L.style] = weight[L.style] + w
            end
        end
    end
    local best = nil
    for _, st in ipairs({ "melee", "ranged", "magic" }) do
        if weight[st] > 0 and (best == nil or weight[st] > weight[best]) then best = st end
    end
    if best and best ~= S.overhead then
        QD.raid._tob_trace(S, F.tick, string.format("overhead %s (mel %g rng %g mag %g)", best,
            weight.melee, weight.ranged, weight.magic))
        S.overhead = best
    end
    return S.ids.protect[S.overhead or "melee"]
end

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
    local V = QD.NYLO
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

-- THE CLOCK (as Verzik's: the room as a schedule). The wave table
-- (QD.NYLO_WAVES, tob_nylocas_waves.lua, generated beside the content's own
-- table) names every spawn - lane, tile, size, aggro, styles, support - and the
-- next wave comes on the last one's tick plus its natural stall, on the 4-tick
-- grid, held 4 more while the room is at its cap (12 before wave 20, 24 from
-- it; corpses count). Returns the next wave's number and predicted tick.
function QD.raid._nylo_clock(S, F)
    local n = S.waves + 1
    if n > 31 or S.wave_last == nil then return nil, nil end
    local W = QD.NYLO_WAVES[S.waves]
    local at = S.wave_last + W.stall
    local cap = (S.waves < 20) and 12 or 24
    if #F.nylos >= cap then at = math.max(at, F.tick + 4 - ((F.tick - S.wave1) % 4)) end
    return n, at
end

-- The box tile a seat waits on for a spawn: one in from the lane's mouth,
-- on the spawn's row (west, east) or column (south).
local function lane_wait(sp)
    if sp.lane == "W" then return { x = 27, z = sp.lz } end
    if sp.lane == "E" then return { x = 36, z = math.min(sp.lz, 29) } end
    return { x = sp.lx, z = 20 }
end

-- DECIDE, WAVES and CLEANUP: my target and the style I hit it with. In the
-- cleanup there is nothing to come, so my own style has no claim: whatever
-- the target rule ranks first, my worn style winning a tie.
function QD.raid._nylo_decide_nylos(S, F, phase)
    local V = QD.NYLO
    local own = V.STYLE_OF_SEAT[S.role] or "melee"
    local life, style = QD.raid._nylo_target(S, F, own)
    local D = { style = style, life = life, target = life and life.row, range = V.RANGE[style],
                station = V.STATION[S.role], overhead = nil }
    D.kind = life and ((life.aggro and "aggro") or "nylo") or "station"
    if life and style ~= own then D.kind = "help-" .. style end
    if S.role == V.BARRAGE_SEAT and F.tick >= (S.next_cast or 0) then
        local C, n = QD.raid._nylo_clump(S, F)
        if C and n >= V.BARRAGE_MIN then
            D.style, D.life, D.target, D.range = "magic", C, C.row, V.SPELL_RANGE
            D.cast, D.kind, D.clump = true, "barrage", n
        end
    end
    D.kind = phase .. ":" .. D.kind
    return D
end

-- DECIDE, BOSS_DUE: she lands melee (her spawning form, then Ischyros), so the
-- melee set, Protect from Melee, hp and prayer to full, and a tile beside her
-- landing on my side (melee south, ranger west, mage east).
function QD.raid._nylo_decide_due(S, F)
    local V = QD.NYLO
    local B = V.BOSS_LAND
    local side = { [1] = { x = B.x - 1, z = B.z + 1 }, [2] = { x = B.x + 1, z = B.z - 1 },
                   [3] = { x = B.x + V.BOSS_SIZE, z = B.z + 1 } }
    return { style = "melee", range = 1, station = side[S.role] or side[2], kind = "boss_due",
             overhead = S.ids.protect.melee, eat_below = F.hp_base - 20, full = true }
end

-- DECIDE, BOSS: her colour is her npc id; the melee form's first sight is M,
-- her changes at M+9+10k. NEVER a press that resolves on a predicted change
-- tick: it swings after the retype, a wrong style, reflected and healed.
function QD.raid._nylo_decide_boss(S, F)
    local V, ids = QD.NYLO, S.ids
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

-- ACT: the decision's set, prayers and supplies, the plan, one order, emit.
function QD.raid._nylo_act(S, F, D)
    local V, ids = QD.NYLO, S.ids
    local intent = { gear = missing(S, ids.sets[D.style]) }
    -- a held op re-sent every tick while the set goes on would repeat it: two
    -- ticks apart, unless the set wanted CHANGED (seed n3/sa t127: the swap
    -- back to melee held a tick and the attack went out with the trident)
    if #intent.gear == 0 or (F.tick - (S.gear_sent or -10) < 2 and S.gear_style == D.style) then intent.gear = nil end
    if intent.gear then S.gear_style = D.style end
    -- ARMED IS WORN, SEEN: a wield sent in the tick of the press is not
    -- enough (seed n29/sp t430: the mage set's wields went out before the
    -- attack and the swing still left with the tentacle; the trident showed
    -- at t431). The tick of a switch holds.
    local weapon = ids.weapon[D.style]
    local armed = QD.raid._tob_worn(S, weapon)
    pipe_rapid(S, F)
    local want = { overhead = D.overhead or wave_overhead(S, F), boost = ids.boost[D.style],
                   boost_stat = ids.boost_stat[D.style], eat_below = D.eat_below }
    if F.pct_low < V.SUPPORT_LOW then want.eat_below = math.max(want.eat_below or 0, V.EAT_COLLAPSE) end
    QD.raid._tob_supplies(S, F, want, intent)
    if D.kind ~= S.kind then
        QD.raid._tob_trace(S, F.tick, "context " .. D.kind)
        S.kind = D.kind
    end
    local spec, names = QD.raid._nylo_spec(S, F, D.target, D.range or 1, D.station)
    if D.boss then
        -- within 8 of her or she walks at whoever is nearest
        spec.zones[#spec.zones + 1] = { x = F.boss.x, z = F.boss.z, size = V.BOSS_SIZE, lo = 1, hi = V.BOSS_REACH,
            require = true, tier = "soft", cost = 2.0 }
    end
    local plan = QD.raid._tob_plan(S, F, spec, names)
    local d = QD.raid._tob_order(S, F, plan, { target = D.target, range = D.range or 1 })
    -- NEVER A SWING IN THE WRONG STYLE: an attack press only with the target's
    -- weapon in hand or wielded in this same tick, before the press (emit's
    -- order); one wrong-style hit nulls me on a nylo for its life
    -- (a WALK to the plan's step, not nil: a nil order after a held op is
    -- emit's cue to press the last target again)
    if (D.hold or not armed) and d.order and d.order.mode == "attack" then
        d.order = { mode = "walk", x = d.x, z = d.z }
    end
    -- THE CAST (as the Maiden's freezer): never an attack press; a plan that
    -- moves walks, one that stands casts, the mage set on
    if D.cast and d.order and d.order.mode == "attack" then
        if d.x ~= F.me.x or d.z ~= F.me.z then
            d.order = { mode = "walk", x = d.x, z = d.z }
        elseif not (armed and intent.gear == nil) then
            -- not ready to cast: HOLD, never the attack the plan's order was
            d.order = { mode = "walk", x = F.me.x, z = F.me.z }
        else
            intent.cast = { npc = D.target, component = ids.barrage }
            d.order = nil
            S.order = nil
            S.next_cast = F.tick + V.CAST_EVERY
            S.barrages = (S.barrages or 0) + 1
            S.barrage_hits = (S.barrage_hits or 0) + (D.clump or 0)
        end
    end
    if d.order and d.order.mode == "attack" and D.life then
        S.last_target_life = D.life
        S.attacks = S.attacks + 1
    end
    -- WHY I WALK (the sweep's accounting): to reach the target, or away from
    -- a hazard with the target already in reach, or with no target at all
    if d.order and d.order.mode == "walk" and (d.x ~= F.me.x or d.z ~= F.me.z) then
        local why = "idle"
        if D.target then
            why = QD.raid._tob_in_reach(S, F, F.me.x, F.me.z, D.target, D.range or 1) and "dodge" or "approach"
        end
        S.walks[why] = (S.walks[why] or 0) + 1
    end
    QD.raid._tob_emit(S, F, d.order, intent)
    QD.raid._tob_recent(S, F, d)
end

-- ===================================================================== STEP

-- One tick: MEASURE, the PHASE, DECIDE in it, ACT.
function QD.raid._nylo_step(S, F)
    QD.raid._nylo_measure(S, F)
    if F.boss and not S.boss_seen then
        S.boss_seen = F.tick
        QD.raid._tob_trace(S, F.tick, "Vasilias lands")
    end
    local phase = QD.raid._nylo_phase(S, F)
    if phase == "done" then
        QD.raid._tob_trace(S, F.tick, "her death")
        return "ok"
    end
    local D
    if phase == "boss" then
        D = QD.raid._nylo_decide_boss(S, F)
    elseif phase == "boss_due" then
        D = QD.raid._nylo_decide_due(S, F)
    else
        D = QD.raid._nylo_decide_nylos(S, F, phase)
    end
    QD.raid._nylo_act(S, F, D)
    return nil
end

-- ==================================================================== ENTRY

-- THE ENTRY PHASE: the room is arrived at on (31,49), 18 tiles north of the
-- barrier on z=31 (tob.constant), so the walk to (31,33) puts it in view; then
-- the shared start (the leader answers at once: nothing in the room moves
-- before the start, the supports are added by it).
function QD.raid._nylo_enter(S, opts)
    local tr, me = api_drive.player_tile()
    assert(tr == "ok", "nylocas_solve: no tile")
    local ex, ez = me.x - me.x % 64 + QD.NYLO.ENTRY.x, me.z - me.z % 64 + QD.NYLO.ENTRY.z
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

function QD.raid.nylocas_solve(opts)
    opts = opts or {}
    local S = QD.raid._tob_state("nylocas_solve", QD.raid._nylo_ids(), opts, {
        lives = {}, wave_at = {}, waves = 0, splits = 0, wave_log = {}, nulled = {}, nulls = 0,
        attacks = 0, switch_log = {}, phase_log = {}, walks = {},
    })
    QD.raid._nylo_enter(S, opts)
    return QD.raid._tob_run(S, QD.raid._nylo_step, function(s)
        return QD.raid._tob_summary(s, string.format(
            "room t0 %s; phases %s; waves %d (%s); splits %d; attacks %d; walks approach %d dodge %d idle %d; barrages %s (%s in reach); nulls %d; boss landed %s, M %s; switches %s",
            tostring(s.t0), table.concat(s.phase_log, " "), s.waves, table.concat(s.wave_log, " "), s.splits, s.attacks,
            s.walks.approach or 0, s.walks.dodge or 0, s.walks.idle or 0,
            tostring(s.barrages or 0), tostring(s.barrage_hits or 0), s.nulls,
            tostring(s.boss_seen and (s.boss_seen - (s.t0 or 0))), tostring(s.boss_m and (s.boss_m - (s.t0 or 0))),
            table.concat(s.switch_log, " ")))
    end)
end

-- The ids the test's tick-log measures read (a test file has no api_drive).
function QD.raid.nylocas_symbols()
    local out = QD.raid._tob_symbols("nylocas_symbols", {
        { "support", "npc", "tob_nylocas_support" },
        { "boss_spawning", "npc", "nylocas_boss_spawning" }, { "boss_melee", "npc", "nylocas_boss_melee" },
        { "boss_magic", "npc", "nylocas_boss_magic" }, { "boss_ranged", "npc", "nylocas_boss_ranged" },
        { "shielded", "spotanim", "tob_nylocas_shielded" },
    })
    out.nylo = {}
    for _, size in ipairs({ "", "big_" }) do
        for _, form in ipairs({ "incoming", "fighting" }) do
            for _, style in ipairs({ "melee", "ranged", "magic" }) do
                local r, id = api_drive.symbol("npc", "tob_nylocas_" .. size .. form .. "_" .. style)
                assert(r == "ok", "nylocas_symbols: no npc tob_nylocas_" .. size .. form .. "_" .. style)
                out.nylo[id] = true
            end
        end
    end
    return out
end
