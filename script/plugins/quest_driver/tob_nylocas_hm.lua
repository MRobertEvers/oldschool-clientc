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

-- Hard Mode's overrides over a copy of the Normal table: a copy, not a
-- metatable (the plugin sandbox has no setmetatable; QD.NYLO is complete
-- once its file has loaded, and nothing writes its fields at run time).
QD.NYLO_HM = {
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
    BARRAGE_SEATS = { [3] = true, [4] = true },
    -- Vasilias' Hard forms, relative to her south-west tile (her 4x4 body
    -- x0..3): five tiles pairwise 3+ apart and in the blowpipe's 5 of her
    -- (ranged), and one tile in the trident's 7 for everyone (magic). Seats 2
    -- and 4 stood at (-3,-3) and (6,-3): inside the SW and SE supports, so
    -- they could never reach them and held their melee tiles, 2 apart, through
    -- every ranged form. Now north-west and north-east of her.
    SPREAD = { { x = -4, z = 1 }, { x = -2, z = 5 }, { x = 2, z = -4 }, { x = 5, z = 5 }, { x = 7, z = 2 } },
    DOT = { x = 1, z = -3 },
    SPLASH = 2,                                 -- ^tob_vasilias_hard_splash_range
    MUST_COST = 6,
    PREMOVE = 2,                                -- ticks before her change: her form's swings are done (Blert)
    -- THE PILLARS ARE 150 at five (tob_nylocas.rs2 ^tob_nylo_pillar_hp_hard,
    -- Normal's trio has 230) and the cap holds 15 early: a chewer always
    -- counts triple, six times under 80% (n5a: every support fell by room
    -- tick 330 on four seeds of four under Normal's 30/50)
    SUPPORT_SAVE = 80, SUPPORT_HELP = 101,
    SPLIT_SAME = true,
    -- A WALKER IS WORTH THE BITES A KILL SAVES (QD.raid._nylo_target's value
    -- with SAVED_BITES: its life left on arrival), not a flat third of it
    -- ("walkers that are not aggros are not damaging pillars yet", Plank2g,
    -- [HMR], [S]); the third and none at all both collapsed more (ny3-ny5)
    WALKER_WEIGHT = nil,
    SAVED_BITES = true,
    PRINCE_WAVES = { [10] = true, [20] = true, [30] = true },
    PRINCE_SIZE = 3,
    PRINCE_DEFER_FROM = 30,                     -- from this wave a Prinkipas is the focus one (wave 30)
    PRINCE_FOCUS_SEATS = { [2] = true, [4] = true, [5] = true },
    PILLAR_GUARDS = { [1] = true, [3] = true },  -- a ranger and a mage (melee help covers the third style)
    PILLAR_HP = 150, BITE_MEAN = 0.9, BITE_VAR = 0.69, BITE_TICKS = 3,   -- ^tob_nylo_pillar_hp_hard, the bite roll's mean and variance, ^tob_nylo_attackrate
    COLLAPSE_CHANCE = 0.3, SAVE_CHANCE = 0.75,  -- a guard saves a support that falls alone this often and stands guarded this often
    GUARD_RATE = 4, GUARD_WALK = 1,             -- a guard's ticks a hit (the slower of the pair) and to its next
    PRINCE_WAIT_FROM = math.huge,               -- none is held to the cleanup (owner, 2026-10-12)
    PRINCE_HIT = 38, EAT_MARGIN = 10, EAT_CEIL = 99,
    COLLAPSE_HIT = 50, COLLAPSE_NEAR = 25,      -- a collapse's 50 for each pillar under this percent
    -- (a collapse is up to 50 to everyone, with the fallen support's chewers
    -- turned on the team after it: Normal's 25% / 51 left seats at 39-47 for
    -- the next one, all2 a3 and a6)
    SUPPORT_LOW = 40, EAT_COLLAPSE = 65,
    LANE_READY = 3,                             -- a settled lane nylocas is in LANE_SHOT of the box from +3
}
for k, v in pairs(QD.NYLO) do if QD.NYLO_HM[k] == nil then QD.NYLO_HM[k] = v end end

-- Every spread tile is outside the ranged splash of every other, on the box
-- floor, and off the supports' 3x3s.
do
    local V = QD.NYLO_HM
    local land = V.BOSS_LAND
    for i, a in ipairs(V.SPREAD) do
        local ax, az = land.x + a.x, land.z + a.z
        assert(ax >= V.BOX.x0 and ax <= V.BOX.x1 and az >= V.BOX.z0 and az <= V.BOX.z1,
            "NYLO_HM.SPREAD[" .. i .. "] is outside the box")
        for _, sp in ipairs(V.SUPPORTS) do
            assert(not (ax >= sp.x and ax <= sp.x + 2 and az >= sp.z and az <= sp.z + 2),
                "NYLO_HM.SPREAD[" .. i .. "] is on a support")
        end
        for j = i + 1, #V.SPREAD do
            local b = V.SPREAD[j]
            assert(math.max(math.abs(a.x - b.x), math.abs(a.z - b.z)) > V.SPLASH,
                "NYLO_HM.SPREAD[" .. i .. "] and [" .. j .. "] share a splash")
        end
    end
end

function QD.raid._nylo_hm_ids(S)
    local ids = S.ids
    if ids.prince_style then return end
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
--   now      waves 10 and 20: every seat, ahead of every nylocas (owner,
--            2026-10-12: "All team members should be attacking the prinkipas
--            when it lands"; "it should be kill first 2 ASAP")
--   focus    wave 30: PRINCE_FOCUS_SEATS on it the same way, the two
--            PILLAR_GUARDS on the pillars a kill can save (owner: "2 players
--            should use that logic, and the other 3 always focus [it] in the
--            same way as the first"; the savers are the third's, 2026-10-12)
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
function QD.raid._nylo_hm_guard(S, F, phase)
    local V = S.V
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
            if p_alone >= V.COLLAPSE_CHANCE and 1 - p_guarded >= V.SAVE_CHANCE then
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

-- DECIDE, VASILIAS: the Normal decision, then my tile for her form.
--
-- THE PRE-MOVE ([HMR], HMT_STRATEGIES.md: "Pre-move after its second attack
-- of a form: in ranged -> to the dot; in magic -> spread"). Her first swing in
-- a new form comes +2 or +3 after the change and a spread tile is up to 6 from
-- the dot, so a seat that waits to see the colour is still stacked when the
-- ranged splash lands. In ranged I go to the dot (the next is magic or a
-- prayed melee), in magic to my spread tile (ranged or melee), in melee to my
-- spread tile too - a stack caught by the ranged splash takes it whole, a
-- spread team caught by the magic bounce takes it halved on the way in.
--
-- WHEN, from Blert (tools/blert_vasilias_hard.py, 60 Hard fives, 448 forms):
-- her last swing in a form is 4 ticks before the change in 292, 3 in 132, 2
-- in 18 and 1 in 5, and the teams break the stack (or close it) on the tick
-- before the change (pairs within 2 of ten: 9.6 at -2, 7.3 at -1, 3.6 at the
-- change, 0.8 at +2 into ranged). A press decided PREMOVE = 2 before the
-- change moves me on the tick before it, after every swing that reads it but
-- the 1% at -1, and 6 tiles at a run are done before her first swing at +2.
-- The same data has the teams spreading out of melee too (pairs within 2 fall
-- from 4.5 at -2 to 0.8 at +2 into ranged).
function QD.raid._nylo_hm_decide_boss(S, F)
    local V = S.V
    local D = QD.raid._nylo_decide_boss(S, F)
    local b = F.boss
    local colour = S.boss_colour
    if colour == nil or D.style ~= colour then return D end
    local since = F.tick - S.boss_m - V.SWITCH_FIRST
    local left = (since < 0) and -since or (V.SWITCH_EVERY - since % V.SWITCH_EVERY)
    local tile = nil
    if left <= V.PREMOVE then
        tile = (colour == "ranged") and "dot" or "spread"
        D.kind = D.kind .. ":premove"
    elseif colour == "ranged" then
        tile = "spread"
    elseif colour == "magic" then
        tile = "dot"
    end
    if tile == "spread" then
        local s = V.SPREAD[S.role] or V.SPREAD[1]
        D.must = { x = b.x + s.x, z = b.z + s.z }
        D.kind = D.kind .. ":spread"
    elseif tile == "dot" then
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
