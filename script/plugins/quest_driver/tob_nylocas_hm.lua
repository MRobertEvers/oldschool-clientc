-- quest-driver / tob_nylocas_hm: THE NYLOCAS, HARD MODE, five raiders.
--
--   t.raid.nylocas_hm_solve(opts) -> result, detail, record
--
-- The Normal solver (tob_nylocas.lua: the lives, the waves, the detonations,
-- the style-strict targets, the barrage, Vasilias' colour clock) on Hard
-- Mode's numbers and rules (wiki Theatre_of_Blood/Hard_Mode :322-338):
--
--   reflect      every wave nylo is Vasilias' kind: a wrong-style hit is
--                reflected onto the attacker and nulls it. The shared target
--                rule never swings in the wrong style and the barrage never
--                splashes a wrong one, so nothing changes but the stakes.
--   Prinkipas    a demi-boss on waves 10, 20 and 30 (nylocas_miniboss_*,
--                300/350/400 hp), landing where Vasilias does (30,23) and
--                changing colour on her clock: melee from its third tick (M),
--                a change at M+9 and every 10 after. The first two are killed
--                at once by every seat whose style is its colour (melee helps,
--                the owner's rule); the third waits for the waves to clear
--                ("The third one should be ignored until all of the lesser
--                nylocas are killed", :327).
--   Vasilias     her ranged form "hits everyone within 3 tiles of its primary
--                target" (2 in the content, ^tob_vasilias_hard_splash_range)
--                and her magic form bounces through everyone NOT on the
--                target's tile: "spread at least three tiles away when it is
--                in its ranged form, and death dot when it is in its magic
--                form" (:331). Five spread tiles round her and one dot tile.
--
-- THE ROLES (five, by style): seat 1 ranger (west), seat 2 melee (south),
-- seats 3 and 4 the mages and the barrage (east, north), seat 5 ranger
-- (south-east).
-- ==========================================================================

QD.NYLO_HM = QD.raid._tob_derive(QD.NYLO, {
    -- THE STYLES BY RATE: two blowpipes (2 ticks) for ranged, two tridents
    -- (4) for magic, and melee's one whip has every other seat's help (help
    -- is melee only, QD.raid._nylo_target). With one mage the magic nylocas
    -- did 48% of the bites (ny1: big ones chewed their whole 42-tick life);
    -- the second ranger made a mage (ny2) and ranged took over, so the second
    -- MELEE seat is the second mage. (Maiden's freezers as the mages, ny11-12,
    -- took a third more damage and seat 1's freezer pack ran dry: n7.)
    STATION = { [1] = { x = 27, z = 25 }, [2] = { x = 31, z = 21 }, [3] = { x = 36, z = 25 },
                [4] = { x = 31, z = 28 }, [5] = { x = 35, z = 21 } },
    STYLE_OF_SEAT = { [1] = "ranged", [2] = "melee", [3] = "magic", [4] = "magic", [5] = "ranged" },
    BARRAGE_SEATS = { [3] = true },             -- seat 4 is on Arceuus (thralls) in the Hard relay (tob_relay.lua's books)
    -- Vasilias' Hard forms, relative to her south-west tile (her 4x4 body
    -- x0..3): five tiles pairwise 3+ apart and in the blowpipe's 5 of her
    -- (ranged), and one tile in the trident's 7 for everyone (magic)
    SPREAD = { { x = -4, z = 1 }, { x = -3, z = -3 }, { x = 2, z = -4 }, { x = 6, z = -3 }, { x = 7, z = 2 } },
    DOT = { x = 1, z = -3 },
    MUST_COST = 6,
    -- THE PILLARS ARE 150 at five (tob_nylocas.rs2 ^tob_nylo_pillar_hp_hard,
    -- Normal's trio has 230) and the cap holds 15 early: a chewer always
    -- counts triple, six times under 80% (n5a: every support fell by room
    -- tick 330 on four seeds of four under Normal's 30/50)
    SUPPORT_SAVE = 80, SUPPORT_HELP = 101,
    SPLIT_SAME = true,
    FLEX_HELP = true,                           -- an idle seat helps on melee's best nylocas too (tob_nylocas.lua _nylo_target)
    -- A WALKER IS WORTH THE BITES A KILL SAVES (QD.raid._nylo_target's value
    -- with SAVED_BITES: its life left on arrival), not a flat third of it
    -- ("walkers that are not aggros are not damaging pillars yet", Plank2g,
    -- [HMR], [S]); the third and none at all both collapsed more (ny3-ny5)
    WALKER_WEIGHT = nil,
    SAVED_BITES = true,
    PRINCE_WAVES = { [10] = true, [20] = true, [30] = true },
    PRINCE_SIZE = 3,
    PRINCE_DEFER_FROM = 20,                     -- from this wave a Prinkipas is the focus one (waves 20, 30)
    PRINCE_FOCUS_SEATS = { [2] = true, [4] = true, [5] = true },
    PRINCE_PILLAR_WAVE = 30, PRINCE_PILLAR_PCT = 30,
    SWING_LOG = 60, SWING_FLICK = true, SEQ_TICK_LAG = 1,
    NYLO_RATE = 3, PRINCE_RATE = 4, SWING_LAG = 1,      -- ^tob_nylo_attackrate, the Prinkipas's, and a press's lead (sent now, handled before the next tick's npc turn, where the swing reads it)   -- from this wave's, focus only while every pillar is at this percent
    SPEC_PRINCE = true,                         -- the special weapon on a melee Prinkipas (tob_nylocas.lua spec_set)
    PILLAR_GUARDS = { [1] = true, [3] = true },  -- a ranger and a mage (melee help covers the third style)
    PILLAR_HP = 150, BITE_MEAN = 0.9, BITE_VAR = 0.69, BITE_TICKS = 3,   -- ^tob_nylo_pillar_hp_hard, the bite roll's mean and variance, ^tob_nylo_attackrate
    COLLAPSE_CHANCE = 0.3, SAVE_CHANCE = 0.5, HELP_COLLAPSE = 0.6,   -- (HELP_COLLAPSE: a focus seat's bar to leave its Prinkipas for a pillar)   -- a guard saves a support that falls alone this often and stands guarded this often
    GUARD_RATE = 4, GUARD_WALK = 1,             -- a guard's ticks a hit (the slower of the pair) and to its next
    PRINCE_WAIT_FROM = math.huge,               -- none is held to the cleanup (owner, 2026-10-12)
    PRINCE_HIT = 38, EAT_MARGIN = 10, EAT_CEIL = 99,
    BREW_DOSE_HP = 16, BREW_FLOOR_MIN = 50,     -- a brew dose's heal; the lowest the brew floor goes
    COLLAPSE_HIT = 50, COLLAPSE_NEAR = 25,      -- a collapse's 50 for each pillar under this percent
    -- (a collapse is up to 50 to everyone, with the fallen support's chewers
    -- turned on the team after it: Normal's 25% / 51 left seats at 39-47 for
    -- the next one, all2 a3 and a6)
    SUPPORT_LOW = 40, EAT_COLLAPSE = 65,
    LANE_READY = 3,                             -- a settled lane nylocas is in LANE_SHOT of the box from +3
})

-- The swings due at me in V.SWING_LAG ticks, each nylocas's and Prinkipas's
-- last attack (its seq's start) plus its rate, at its nearest raider (ties
-- counted mine); the heaviest style -> its protection, else nil (the choice
-- stands until a swing names another).
function QD.raid._nylo_hm_swing_overhead(S, F)
    local ids, V = S.ids, S.V
    S.swing_last = S.swing_last or {}
    S.swing_log = S.swing_log or {}
    local weight = {}
    local function consider(row, size, rate, w)
        local st = ids.attack_seq_style[row.seq_id]
        -- (the row's seq_tick is a tick before the server's: the sent cycle
        -- rounds down - swings logged a tick after this read's prediction,
        -- 535 to 244 over smk7's eight seeds)
        if st and row.seq_tick and row.seq_tick >= 0 then
            S.swing_last[row.slot] = { t = row.seq_tick + V.SEQ_TICK_LAG, style = st, id = row.npc_id }
        end
        -- (kept across a retype: a Prinkipas changes colour, an incoming
        -- nylocas turns aggro, and the clock runs on through both)
        local rec = S.swing_last[row.slot]
        if rec == nil or rec.t + rate ~= F.tick + V.SWING_LAG then return end
        -- its target: the nearest by its footprint, a tie to the first found
        -- - the lowest pid (tob_nylocas.rs2 ~tob_nylo_fight_tick; all128f:
        -- 2,955 ties to the lowest, 624 to the highest)
        local mine = QD.raid._tob_gap(F.me.x, F.me.z, row.x, row.z, size)
        for _, rd in ipairs(F.raiders) do
            if not rd.me then
                local g = QD.raid._tob_gap(rd.x, rd.z, row.x, row.z, size)
                if g < mine or (g == mine and F.pid and rd.pid < F.pid) then return end
            end
        end
        weight[rec.style] = (weight[rec.style] or 0) + w
    end
    for _, L in ipairs(F.nylos) do
        if not L.dying and L.row then consider(L.row, L.size or 1, V.NYLO_RATE, (L.size == 2) and 24 or 17) end
    end
    for _, P in ipairs(F.princes) do consider(P.row, V.PRINCE_SIZE, V.PRINCE_RATE, 380) end
    local best = nil
    for _, st in ipairs({ "melee", "ranged", "magic" }) do
        if weight[st] and (best == nil or weight[st] > weight[best]) then best = st end
    end
    -- nothing due at me: a Prinkipas alive keeps its colour prayed (the
    -- owner's rule), the nearest of two; else the last choice stands
    if best == nil then
        local near = nil
        for _, P in ipairs(F.princes) do
            if P.colour then
                local g = QD.raid._tob_gap(F.me.x, F.me.z, P.row.x, P.row.z, V.PRINCE_SIZE)
                if near == nil or g < near then near, best = g, P.colour end
            end
        end
    end
    if best and best ~= S.swing_choice then
        S.swing_choice = best
        S.swing_switches = (S.swing_switches or 0) + 1
    end
    -- (the swing log, for nylo_prayer_policy.py's agreement check: this
    -- tick's dues at me and the choice, the first SWING_LOG ticks with one)
    if next(weight) ~= nil and #S.swing_log < V.SWING_LOG then
        local parts = {}
        for st, w in pairs(weight) do parts[#parts + 1] = st:sub(1, 3) .. w end
        table.sort(parts)
        S.swing_log[#S.swing_log + 1] = "t" .. F.tick .. ":" .. table.concat(parts, "+") .. ">" .. tostring(S.swing_choice):sub(1, 3)
    end
    return S.swing_choice and ids.protect[S.swing_choice] or nil
end

function QD.raid._nylo_hm_ids(S)
    local ids = S.ids
    if ids.prince_style then return end
    ids.attack_seq_style = {}
    for name, st in pairs({ top_spider_melee_attack = "melee", top_spider_ranged_attack = "ranged",
                            top_spider_magic_attack = "magic" }) do
        local r, id = api_drive.symbol("seq", name)
        assert(r == "ok", "nylocas_hm_solve: no seq named " .. name)
        ids.attack_seq_style[id] = st
    end
    local function sym(name)
        local r, id = api_drive.symbol("npc", name)
        assert(r == "ok", "nylocas_hm_solve: no npc named " .. name)
        return id
    end
    ids.prince_spawning = sym("nylocas_miniboss_spawning_hard")
    ids.prince_style = { [sym("nylocas_miniboss_melee_hard")] = "melee", [sym("nylocas_miniboss_magic_hard")] = "magic",
                         [sym("nylocas_miniboss_ranged_hard")] = "ranged" }
end

-- THE PRINKIPAS, EVERY ONE ALIVE (two can be: wave 20's while wave 30's
-- lands): its row, its colour (nil while spawning), the wave it came with,
-- its own colour clock (M: its first melee tick) and its STATE:
--   now      wave 10: every seat, ahead of every nylocas (owner,
--            2026-10-12: "All team members should be attacking the prinkipas
--            when it lands")
--   focus    waves 20 and 30: PRINCE_FOCUS_SEATS on it the same way (killed
--            at once), the two PILLAR_GUARDS on a pillar only when it is
--            likely to fall and they are likely to save it, else on it too
--            (owner: "2 players should use that logic, and the other 3
--            always focus [it] in the same way as the first"; the savers on
--            wave 20's as well - every seat on it collapsed 2.1 pillars a
--            seed to 1.2, ny20)
--   cleanup  none now (PRINCE_WAIT_FROM): once the waves are done and no
--            nylocas is left
-- F.prince is the one a decision reads first; F.princes all of them.
function QD.raid._nylo_hm_prince(S, F)
    local ids = S.ids
    S.prince_rec = S.prince_rec or {}
    F.princes, F.prince = {}, nil
    local seen = {}
    for _, row in ipairs(F.npcs) do
        if row.npc_id == ids.prince_spawning or ids.prince_style[row.npc_id] then
            local rec = S.prince_rec[row.slot]
            if rec == nil then
                rec = { wave = S.waves, first = F.tick }
                S.prince_rec[row.slot] = rec
                S.princes = (S.princes or 0) + 1
                QD.raid._tob_trace(S, F.tick, "Prinkipas lands (wave " .. S.waves .. ")")
            end
            seen[row.slot] = true
            local colour = ids.prince_style[row.npc_id]
            if colour and rec.m == nil then rec.m = F.tick end
            local state = (rec.wave < S.V.PRINCE_DEFER_FROM and "now") or (rec.wave < S.V.PRINCE_WAIT_FROM and "focus") or "cleanup"
            -- WAVE 30'S, ONLY WHILE THE PILLARS CAN AFFORD IT ([HMR], [IND2],
            -- [ROB]: "left until the waves are clear unless the pillars are
            -- above 30%"): a pillar under PRINCE_PILLAR_PCT, or one already
            -- down, sends its focus seats to the nylocas - three on it and two
            -- guards for four pillars collapsed three and four at the end of
            -- the waves, 51 deaths (all128f)
            if state == "focus" and rec.wave >= S.V.PRINCE_PILLAR_WAVE
                and ((F.pct_low or 100) < S.V.PRINCE_PILLAR_PCT or #F.supports < 4) then
                state = "cleanup"
            end
            F.princes[#F.princes + 1] = { row = row, colour = colour, m = rec.m, wave = rec.wave, state = state }
        end
    end
    for slot in pairs(S.prince_rec) do if not seen[slot] then S.prince_rec[slot] = nil end end
    local rank = { now = 1, focus = 2, cleanup = 3 }
    table.sort(F.princes, function(a, b) return rank[a.state] < rank[b.state] or (a.state == b.state and a.row.slot < b.row.slot) end)
    F.prince = F.princes[1]
end

-- A nylocas on a pillar: within 2 of a support's centre (tob_nylocas.lua's
-- `chewing`).
function QD.raid._nylo_hm_chewing(S, F, L)
    if L.aggro then return false end
    for _, sp in ipairs(F.supports) do
        if QD.raid._tob_gap(sp.x + 1, sp.z + 1, L.x, L.z, L.size) <= 2 then return true end
    end
    return false
end

-- THE PILLAR GUARDS' PREDICTION. A chewer bites every ^tob_nylo_attackrate
-- (3) ticks, 0, 1 or 2 weighted 4:3:3 (^tob_nylo_pillar_bite_*: mean 0.9,
-- variance 0.69), until it dies or its life runs out. For each support, the
-- CHANCE it falls: its chewers' bites left alone over their lives, and their
-- bites if the two guards kill them in order of most life left (each a hit,
-- a big two, at the slower guard's rate, shared between two, a tick of
-- walking each) - the bite total as a normal (V.BITE_MEAN, V.BITE_VAR). A
-- guard saves a support only when BOTH hold (owner, 2026-10-12: "seats 1 and
-- 3 should only save pillars if there is 1. A reasonable chance that the
-- pillar will collapse 2. AND they have a good chance of saving it"): left
-- alone it falls with at least V.COLLAPSE_CHANCE, and guarded it stands with
-- at least V.SAVE_CHANCE. Of those the soonest to fall is mine, and on it the
-- chewer I can hit (my style, or melee) with the most life left. Nothing to
-- save: the guard is on the focus Prinkipas, or with none up plays the
-- nylocas.
function QD.raid._nylo_hm_guard(S, F, phase, collapse_chance)
    local V = S.V
    collapse_chance = collapse_chance or V.COLLAPSE_CHANCE
    if phase ~= "waves" and phase ~= "cleanup" then return nil end
    local own = V.STYLE_OF_SEAT[S.role] or "melee"
    local best, best_fall = nil, nil
    for _, sp in ipairs(F.supports) do
        local hp = sp.pct * V.PILLAR_HP / 100
        local chewers = {}
        for _, L in ipairs(F.nylos) do
            if not L.aggro and not L.dying and QD.raid._tob_gap(sp.x + 1, sp.z + 1, L.x, L.z, L.size) <= 2 then
                chewers[#chewers + 1] = L
            end
        end
        if #chewers > 0 then
            table.sort(chewers, function(a, b) return (a.det - a.last) > (b.det - b.last) end)
            -- P(the bites reach hp), the bite count n as a normal with a
            -- continuity half (the logistic form of the normal tail)
            local function falls(n)
                if n <= 0 then return 0 end
                local zs = (hp - 0.5 - n * V.BITE_MEAN) / math.sqrt(n * V.BITE_VAR)
                return 1 / (1 + math.exp(1.702 * zs))
            end
            local alone_n, guarded_n, t = 0, 0, 0
            for _, L in ipairs(chewers) do
                local left = math.max(0, L.det - F.tick)
                alone_n = alone_n + left / V.BITE_TICKS
                t = t + (V.GUARD_WALK + (L.size == 2 and 2 or 1) * V.GUARD_RATE) / 2
                guarded_n = guarded_n + math.min(t, left) / V.BITE_TICKS
            end
            local fall = hp / (#chewers * V.BITE_MEAN / V.BITE_TICKS)
            local p_alone, p_guarded = falls(alone_n), falls(guarded_n)
            if p_alone >= collapse_chance and 1 - p_guarded >= V.SAVE_CHANCE then
                if best_fall == nil or fall < best_fall then best, best_fall = { sp = sp, chewers = chewers }, fall end
            end
        end
    end
    if best == nil then return nil end
    for _, L in ipairs(best.chewers) do
        for _, st in ipairs({ own, "melee" }) do
            if L.style == st and QD.raid._nylo_attackable(S, F, L, st) then
                if S.guard_traced ~= best.sp.row.slot then
                    S.guard_traced = best.sp.row.slot
                    QD.raid._tob_trace(S, F.tick, string.format("guard: support %d at %d%% falls in ~%d ticks alone; saving it",
                        best.sp.row.slot, best.sp.pct, math.floor(best_fall or 0)))
                end
                return { style = st, life = L, target = L.row, range = V.RANGE[st], station = V.STATION[S.role],
                         kind = phase .. ":guard" }
            end
        end
    end
    return nil
end

-- DECIDE, THE PRINKIPAS: mine when its colour is my style (or melee, the
-- help rule), the first two at once and the third once the waves are done.
-- Never a press resolving on its predicted change tick (as Vasilias).
function QD.raid._nylo_hm_decide_prince(S, F, phase, P)
    local V = S.V
    if P == nil or P.colour == nil or P.m == nil then return nil end
    if P.state == "cleanup" and phase ~= "cleanup" then return nil end
    -- (the first, "now", by EVERY seat in its colour - owner, 2026-10-12:
    -- "All team members should be attacking the prinkipas when it lands";
    -- every seat carries the three weapons, as for Vasilias. The others by
    -- the seats of its colour, melee helping.)
    local own = V.STYLE_OF_SEAT[S.role] or "melee"
    -- (a focus one by its seats, and by a guard with no pillar to save -
    -- owner: "if they determine they have a bad chance of saving a pillar,
    -- they should just attack the prinkipas")
    local everyone = P.state == "now" or P.state == "focus"
    if not everyone and P.colour ~= own and P.colour ~= "melee" then return nil end
    local next_at = F.tick + 1 - P.m - V.SWITCH_FIRST
    local row = P.row
    local t = { x = row.x, z = row.z, size = V.PRINCE_SIZE, slot = row.slot, npc_id = row.npc_id,
                element_id = row.element_id }
    for k, v in pairs(row) do if t[k] == nil then t[k] = v end end
    t.size = V.PRINCE_SIZE
    return { style = P.colour, target = t, range = V.RANGE[P.colour], station = V.STATION[S.role],
             kind = phase .. ":prince-" .. P.state .. "-" .. P.colour, hold = next_at >= 0 and next_at % V.SWITCH_EVERY == 0 }
end

-- DECIDE, VASILIAS: the Normal decision, then my tile for her form.
function QD.raid._nylo_hm_decide_boss(S, F)
    local V = S.V
    local D = QD.raid._nylo_decide_boss(S, F)
    local b = F.boss
    if D.style == "ranged" and S.boss_colour == "ranged" then
        local s = V.SPREAD[S.role] or V.SPREAD[1]
        D.must = { x = b.x + s.x, z = b.z + s.z }
        D.kind = D.kind .. ":spread"
    elseif D.style == "magic" and S.boss_colour == "magic" then
        D.must = { x = b.x + V.DOT.x, z = b.z + V.DOT.z }
        D.kind = D.kind .. ":dot"
    end
    return D
end

-- THE INTERCEPT (an idle seat's station). The wave table names every spawn's
-- lane, size and styles (the third is where a flicker settles), so a nylocas
-- still walking its lane - or flickering, unhittable until +8 - is known to
-- be mine before I can hit it. From wave 20 the flickers bit their pillar on
-- +9 and died on +16, 3.5 bites each and two thirds of the room's (ny5),
-- because the seat that could kill one was wherever its last target had been.
-- Every seat sorts the same lane nylocas the same way (the tick it becomes
-- hittable, then its slot) and hands each to the nearest free seat of its
-- settled style, lowest seat first on a tie, one each: mine is waited for
-- one tile into the box at its lane's mouth (where it is on +8).
local function lane_ready(L)
    local V = QD.NYLO_HM
    local W = L.wave and QD.NYLO_WAVES[L.wave]
    if W == nil or L.lane == nil then return nil end
    local sp = nil
    for _, c in ipairs(W.spawns) do
        if c.lane == L.lane and c.big == L.big then sp = c end
    end
    if sp == nil or sp.aggro then return nil end
    local flicker = sp.styles[1] ~= sp.styles[3] or sp.styles[2] ~= sp.styles[3]
    local ready = flicker and (L.birth + V.SETTLE_AGE + 1) or (L.birth + V.LANE_READY)
    return sp.styles[3], ready, sp
end

local function mouth(S, L)
    local lx = math.min(math.max(L.x - S.base.x, 26), 37)
    local lz = math.min(math.max(L.z - S.base.z, 19), 30)
    if L.lane == "W" then return { x = 27, z = lz } end
    if L.lane == "E" then return { x = 36, z = math.min(lz, 29) } end
    return { x = lx, z = 20 }
end

function QD.raid._nylo_hm_intercept(S, F)
    local V = S.V
    local cands = {}
    for _, L in ipairs(F.nylos) do
        if L.lane and not L.dying and not L.aggro then
            local style, ready = lane_ready(L)
            local lx, lz = L.x - S.base.x, L.z - S.base.z
            local inside = lx >= V.BOX.x0 and lx <= V.BOX.x1 and lz >= V.BOX.z0 and lz <= V.BOX.z1
            if style and ready and not inside then cands[#cands + 1] = { L = L, style = style, ready = ready } end
        end
    end
    if #cands == 0 then return nil end
    table.sort(cands, function(a, b) return a.ready < b.ready or (a.ready == b.ready and a.L.slot < b.L.slot) end)
    -- every seat's tile, by role
    local at = {}
    for r = 1, 5 do
        local nm = QD.party.name(r)
        for _, rd in ipairs(F.raiders) do
            if (r == S.role and rd.me) or (r ~= S.role and nm and rd.name and QD.party._same(rd.name, nm)) then
                at[r] = { x = rd.x, z = rd.z }
            end
        end
    end
    local busy = {}
    for _, c in ipairs(cands) do
        local m = mouth(S, c.L)
        local pick, pd = nil, nil
        for r = 1, 5 do
            if not busy[r] and at[r] and V.STYLE_OF_SEAT[r] == c.style then
                local d = QD.raid._tob_cheb(at[r].x - S.base.x, at[r].z - S.base.z, m.x, m.z)
                if pd == nil or d < pd then pick, pd = r, d end
            end
        end
        if pick then
            busy[pick] = true
            if pick == S.role then return m, c end
        end
    end
    return nil
end

function QD.raid._nylo_hm_step(S, F)
    QD.raid._nylo_hm_ids(S)
    QD.raid._nylo_measure(S, F)
    QD.raid._nylo_hm_prince(S, F)
    if F.boss and not S.boss_seen then
        S.boss_seen = F.tick
        QD.raid._tob_trace(S, F.tick, "Vasilias lands")
    end
    local phase = QD.raid._nylo_phase(S, F)
    if phase == "done" then
        QD.raid._tob_trace(S, F.tick, "her death")
        return "ok"
    end
    -- (a Prinkipas still up holds the room in the cleanup: it counts toward
    -- nothing the phase reads, so "no nylos left" is not "the room is clear")
    if phase == "boss_due" and F.prince then phase = "cleanup" end
    local D
    if phase == "boss" then
        D = QD.raid._nylo_hm_decide_boss(S, F)
    elseif phase == "boss_due" then
        D = QD.raid._nylo_decide_due(S, F)
    else
        -- THE PRINKIPAS STATES (QD.raid._nylo_hm_prince): "now" first, then
        -- "focus" for its seats; a guard (while a focus one is up) on the
        -- pillar it can save; then the nylocas; a cleanup one only from
        -- nothing
        local function first_of(state)
            for _, P in ipairs(F.princes) do
                if P.state == state then
                    local d = QD.raid._nylo_hm_decide_prince(S, F, phase, P)
                    if d then return d end
                end
            end
            return nil
        end
        D = first_of("now")
        -- FLEXIBLE ROLES (owner, 2026-10-12: "Perhaps roles in Hard mode for
        -- nylocas should be more flexible?"): a focus seat leaves its
        -- Prinkipas for a pillar too, at the stricter HELP_COLLAPSE - one
        -- that will very likely fall alone and can still be saved
        if D == nil and S.V.PRINCE_FOCUS_SEATS[S.role] and F.prince and F.prince.state == "focus" then
            D = QD.raid._nylo_hm_guard(S, F, phase, S.V.HELP_COLLAPSE)
        end
        if D == nil and S.V.PRINCE_FOCUS_SEATS[S.role] then D = first_of("focus") end
        if D == nil and S.V.PILLAR_GUARDS[S.role] then D = QD.raid._nylo_hm_guard(S, F, phase) or first_of("focus") end
        if D == nil then
            D = QD.raid._nylo_decide_nylos(S, F, phase)
            if D.life == nil and not D.cast then D = first_of("cleanup") or D end
        end
        if phase == "waves" and D.life == nil and not D.cast then
            local m = QD.raid._nylo_hm_intercept(S, F)
            if m then
                D.station = m
                D.kind = phase .. ":intercept"
            end
        end
    end
    -- A HELD PRINKIPAS IS PRAYED AGAINST (owner, 2026-10-12: "For the second
    -- demiboss ... focus on killing the smaller nylos until the waves stop
    -- while praying against the demiboss, then focus the demiboss"): its own
    -- colour is the style it hits in, up to 38; the aggros' weighing is only
    -- the overhead while no Prinkipas is up
    -- (every Prinkipas, whatever its state - owner, 2026-10-12: "make sure
    -- all prinkipas are prayed against correctly")
    -- WHILE ONE IS ALIVE EVERY SEAT PRAYS IT (owner, 2026-10-12: "While the
    -- prinkipas is alive, the prayer should prioritize that one"), the
    -- nearest of two. It swings at the nearest raider (`~tob_vasilias_act`)
    -- and faces only that raider's square, so an entity-face test named
    -- nobody (ny14 n1: seven magic hits on seats praying Missiles).
    -- SWING BY SWING (Hard Mode): the overhead is read at the swing
    -- (tob_nylocas.rs2 ~tob_nylo_swing: the prayer the seat ended the tick
    -- before with), every nylocas swings each ^tob_nylo_attackrate (3) at its
    -- nearest raider and a Prinkipas each 4, so the press made now (in force a
    -- tick on) is the style of the heaviest swing due at me in SWING_LAG
    -- ticks - a Prinkipas's outweighing every other, as the owner's rule
    -- above. Scored offline on all128f's logs (nylo_prayer_policy.py) before
    -- a run: 38.5 of the 61.8 a seat took from them blocked, where praying
    -- the Prinkipas while it lived and the static weighing otherwise left
    -- most of it.
    if not D.boss then
        local o = QD.raid._nylo_hm_swing_overhead(S, F)
        if o then D.overhead = o end
    end
    -- THE FLOOR TO EAT TO, from what can hit in the next few ticks. The
    -- Prinkipas I pray against hits for 17 at most (the Vasilias rule:
    -- melee blocked, the others cut to 17) and one I cannot pray against
    -- (two alive) for 38 (^tob_nylo_prince_max): two held to the cleanup took
    -- a melee seat from 46 to 0 in four ticks (ny9 n1 t513-t517: 30, 30, 18).
    -- At 38 for every Prinkipas and 50 for every low pillar whatever was on
    -- it, the floor sat at 99 and a seat drank nine brew doses to 114 in the
    -- waves while taking 16 (relay hmt_live2 seat 2, t1320-t1428); it reached
    -- Verzik with none. A support down turns its chewers on the team.
    local V = S.V
    local princes = 0
    for _, row in ipairs(F.npcs) do
        if row.npc_id == S.ids.prince_spawning or S.ids.prince_style[row.npc_id] then princes = princes + 1 end
    end
    -- and every pillar near its end can fall on the same tick as another:
    -- 50 a collapse (^tob_nylo_pillar_collapse_max), and two on consecutive
    -- ticks took seats from 56 and 62 to 0 (ny17 n10: 47+14, 44+35)
    -- (every low pillar, bitten now or not: a chewer reaches it in a tick, and
    -- counting only the bitten ones dropped the floor to 27 with a Prinkipas
    -- up - a collapse and a bite took the guard from there, all128b r002,
    -- r006, r012, r023, r025)
    local low = 0
    for _, sp in ipairs(F.supports) do
        if sp.pct < V.COLLAPSE_NEAR then low = low + 1 end
    end
    local floor = 0
    if princes > 0 or low > 0 then
        local prince_hits = V.PRINCE_HIT * princes
        floor = prince_hits + V.COLLAPSE_HIT * low + V.EAT_MARGIN
    end
    if #F.supports < 4 then floor = math.max(floor, V.EAT_COLLAPSE) end
    if floor > 0 then
        floor = math.min(floor, V.EAT_CEIL)
        -- (the brews too: with the fish gone a seat brewed only under 45)
        D.eat_below = math.max(D.eat_below or 0, floor)
        -- (a brew only where a dose does not overheal: brewing to the floor
        -- put a seat at 114 nine doses in a row while it took 16, relay
        -- hmt_live2 seat 2; the fish still eat to the floor)
        D.brew_below = math.max(D.eat_below - V.BREW_DOSE_HP, V.BREW_FLOOR_MIN)
    end
    QD.raid._nylo_act(S, F, D)
    return nil
end

function QD.raid.nylocas_hm_solve(opts)
    assert(QD.raid.tob_mode == "hard", "nylocas_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.raid._nylo_run("nylocas_hm_solve", QD.NYLO_HM, QD.raid._nylo_hm_step, opts)
end
