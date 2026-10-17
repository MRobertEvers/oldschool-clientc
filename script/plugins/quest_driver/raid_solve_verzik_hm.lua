-- quest-driver / raid_solve_verzik_hm: VERZIK VITUR, HARD MODE, five raiders.
--
--   t.raid.verzik_p1_hm_solve(opts), verzik_p2_hm_solve(opts),
--   verzik_p3_hm_solve(opts) -> result, detail, S
--
-- The Normal phase solvers (raid_solve_verzik_p1/p2/p3.lua) with Hard Mode's
-- hazards passed in through their `opts.hm` hooks (wiki Theatre_of_Blood/
-- Hard_Mode :359-375, docs/minigames/theater_of_blood/HMT_STRATEGIES.md):
--
--   P1 rubble    each pillar her bolt hits drops three 1x1 rubble within 3 of
--                its edge, up to 35 and a stun; the shadow (the falling-debris
--                shadow gargboss_debris_shadow_120, tob_verzik.rs2
--                ~tob_verzik_pillar_debris) is drawn on the hit and lands
--                ^tob_verzik_hard_debris_land_ticks (4) later: its tile is out until then.
--   P2 acid      an urnbomb's tile is a poison pool for 16 ticks after it lands
--                ("rotate between five tiles", :363): priced, not forbidden, so
--                the formation can still give way to a worse hazard.
--   P3 yellows   three pools a raider, three strikes 3 ticks apart (the last
--                on the charge's 20th tick: blert 20-tick pools; [S] "3 ticks
--                apart"); a pool that saved its raider is spent, so each raider
--                walks its three in order. Shared out in pid order, nearest
--                first, three rounds.
-- ==========================================================================

QD.VZHM = {
    RUBBLE_LAND = 4,
    P3_EAT = 75, P3_BREW = 70,                       -- P3 floors over her biggest Hard auto (61)
    POP_PULL = 4,
    RED_BURST = 2,                                   -- within this of a red's footprint at her summon (its burst reads 1 from its SW tile)                                    -- a tile short of popping my crab: more than a tick off my target (reach: 3)
    CRAB_LOOK = 1,                                   -- a walking crab's stop is read this far ahead (Normal: 6)
    BACK_CLEAR = 6,                                  -- a back tile is taken only with no pool on it over this many ticks
    ACID_TICKS = 16, ACID_COST = 1e6,                -- ~INF (1e9): a pool is stepped on only when nothing else is open (xv5c: 856 of 1337 P2 damage was the pools and their poison)
    YELLOW_STRIKES = { 14, 17, 20 },         -- after her charge's seq tick
    YELLOW_LAST = 20,
    YELLOW_HOP = 5,                          -- a chain step: three ticks of running, less a tile for her body
    YELLOW_NODES = 3000,                     -- the split's search cap, in chains looked at (the step budget)
    POOL_PULL = 1.5,
}

-- ====================================================================== P1

-- ====================================================================== P2

-- ====================================================================== P3

-- THE YELLOWS, HARD MODE: the strikes' ticks from her charge, and my three
-- pools (every raider computes every raider's, in pid order).
-- THE YELLOWS' CHAINS. Three strikes three ticks apart, so a raider's three
-- pools are a chain whose two steps each fit in three ticks of running - the
-- greedy share (nearest first, round by round) handed the last raiders pools
-- eight tiles apart and they died between them (xv14 sa t1277-t1280: 49 then
-- 45 off a 78). The fifteen pools are split into `n` chains of three, every
-- step within YELLOW_HOP (relaxed a tile at a time only when no split exists),
-- least total walking first; a node cap keeps it inside the step budget. Each
-- seat splits the same sorted pools the same way, so the seats agree.
function QD.raid._vzhm_yellow_chains(pools, n)
    -- (in C, api_drive.yellow_chains: the same split, ties and node cap
    -- included - this Lua's chain search made Verzik P3's heaviest ticks,
    -- 100-111k instructions of the 400000 budget, v3_1e3_256 / c22m)
    local V = QD.VZHM
    local r, idx, hop = api_drive.yellow_chains(pools, n, V.YELLOW_HOP, V.YELLOW_HOP + 3, V.YELLOW_NODES)
    assert(r == "ok", "yellow_chains answered " .. tostring(r))
    if idx == nil then return nil end
    local out = {}
    for k, c in ipairs(idx) do out[k] = { pools[c[1]], pools[c[2]], pools[c[3]] } end
    return out, hop
end

-- Chains to raiders (in F.raiders' order, the same on every seat): the
-- matching with the least walk to the first pool, each chain run from
-- whichever end is nearer. n <= 5: every permutation.
function QD.raid._vzhm_yellow_match(raiders, chains)
    -- (in C, api_drive.yellow_match: the same search, the same order)
    local firsts, thirds = {}, {}
    for c, ch in ipairs(chains) do firsts[c], thirds[c] = ch[1], ch[3] end
    local r, picks = api_drive.yellow_match(raiders, firsts, thirds)
    assert(r == "ok", "yellow_match answered " .. tostring(r))
    if picks == nil then return nil end
    local best = {}
    for jj, p in ipairs(picks) do
        local c = chains[p[1]]
        best[jj] = p[2] and { c[3], c[2], c[1] } or c
    end
    return best
end
