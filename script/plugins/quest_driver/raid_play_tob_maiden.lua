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
        entry = { storm = 28, storm_raw = 30, pool = 12, prove_protect = true, presteps = 12, flicks = 12, solo_melee = true },
        -- Normal (raid seam32 play_tob_maiden_normal), from maiden.tsv's
        -- Normal rows: the blackstorm is 36.5 + 3.5c (maiden.auto_damage_base,
        -- auto_damage_per_leak), halved by the prayer (auto_protect_ratio):
        -- 18 at c = 0, 25 at c = 4, so `storm` 25 covers four leaks and
        -- `storm_raw` 50 the unprotected hit at c = 4; a pool or trail hit is
        -- 10 + 2c (pool_damage_base, pool_damage_per_leak), 20 at c = 5.
        -- `overhit`: the hit is settled on her launch tick (the threat below).
        normal = { storm = 25, storm_raw = 50, pool = 20, prove_protect = false, presteps = 0, flicks = 0, overhit = true },
        hard = { storm = 25, storm_raw = 50, pool = 20, prove_protect = false, presteps = 0, flicks = 0 },
    },
    walk_prayers = { "protectfrommagic" },
    -- raid seam33: the offensive prayers a party raider prays beside Protect
    -- from Magic ("77 prayer for rigour, augury, and piety will massively
    -- speed things up", 10Boot 0:02:45): Piety for the hammer, Rigour for the
    -- bow and the pipe.  Listed here so the library manages them; the Entry
    -- solo plan never wants either, so it sends nothing new.
    down_prayers = { "piety", "rigour" },
    -- THE PARTY'S ROLES (raid seam32 play_tob_maiden_normal; a party of one
    -- is the Entry solo plan above, unchanged).  Sources:
    --   W:603 "in solo to trio there is one freezer";
    --   10Boot (transcripts/yt_4i4lv-srJkw.md 0:06:33-0:08:48): "The person
    --   closest to the boss becomes the tank ... You don't want your mage to
    --   be closest at any time"; "Everyone will be ranging in this room, but
    --   the freezer has a special role"; "freeze the crabs that spawn closest
    --   to Maiden first.  Prioritizing the south side"; "Everyone else should
    --   machine gun down the crabs that aren't in the clump"; "if a crab
    --   spawns at the closest north side tile, this crab should not get
    --   frozen.  Everyone else in the raid should just try and kill it";
    --   "camping on the north side of the arena gives the mage space to hit
    --   the south freezes".
    --   W:589 "closest player -> players on her north/east side -> orb
    --   order"; W:633 "The freezer(s) should range Maiden from a distance".
    -- Seat 1 (the leader, the tick log's raider) is the ranger TANK, nearest
    -- her; seat 2 the FREEZER, south and furthest back; seat 3 the second
    -- RANGER, north (10Boot's "camping on the north side").  Homes are her
    -- footprint-relative tiles (absolute for the instance K measured; the
    -- plan re-bases them on her own tile when it first sees her).
    -- raid seam33: `pipe` -- the rangers carry a loaded toxic blowpipe for the
    -- Matomenos ("Everyone else should machine gun down the crabs that
    -- aren't in the clump with their blowpipe", 10Boot 0:08:14); the freezer
    -- barrages them instead.  Every seat opens with the hammer (P.opener).
    roles = {
        -- raid seam33: the tank camps NORTH of her middle, still the nearest
        -- raider ("If you're the range or melee role, camping on the north
        -- side of the arena gives the mage space to hit the south freezes and
        -- allows you to get the best access to the crab that doesn't get
        -- frozen", 10Boot 0:08:48): s33m-survey w2, the tank on 6435,90 could
        -- not reach the north walkers and three walked in
        -- raid seam40 play_tob_maiden_follows_blert: THE REAL TRIO.  The
        -- reference (sources/blert_api/reference/maiden_normal_3.json, 24
        -- death-free Normal scale-3 rooms) has both dps on her with the
        -- scythe (melee_pct 88.9 / 89.3; SCYTHE in every phase), standing on
        -- her north-east corner: dps1 at (5,6) (4,6) (3,6) from her SW tile,
        -- dps2 at (6,5) (6,4) (6,2) (dist_boss 1 in every phase).  `melee`
        -- puts a seat on her edge (its `side`: the north row or the east
        -- column), swinging `weapon`, with no tank swap: both are targeted
        -- (boss_targeted_pct 38 and 41.45).  The old ranger homes stay as
        -- `reserve` for nothing; seat 1 keeps the leader's tick log.
        [1] = { name = "dps1", home = { 6431, 98 }, side = "north", melee = true, weapon = "scythe_of_vitur", freezer = false, slugs = false, pipe = true },
        -- the freezer stands east of her on her middle row, within Ice
        -- Barrage's ten tiles of both spawn rows (s32mzn2: from 6440,89 the
        -- north spawns at z 101-103, x 6444-6448, were out of reach and three
        -- walked in unfrozen); still the furthest raider from her
        [2] = { name = "freezer", home = { 6441, 94 }, freezer = true, slugs = false, pipe = false },
        [3] = { name = "dps2", home = { 6432, 97 }, side = "east", melee = true, weapon = "scythe_of_vitur", freezer = false, slugs = false, pipe = true },
    },
    -- raid seam33 THE OPENER: "When you run in, everyone should drop a dragon
    -- warhammer spec, then switch to range gear" (10Boot 0:06:33); "Melee &
    -- Ranger: Instantly hammer Maiden" (W:624, the trio row; this cache
    -- has no Bandos godsword drain, so the freezer hammers too, as 10Boot's
    -- "everyone").  Smash lowers her CURRENT Defence by 30% on a successful
    -- hit, and a 0 lowers nothing (wiki_Dragon_warhammer.wikitext:59), so a 0
    -- is swung again while the energy lasts ("use Elder maul special attacks
    -- until two hit", W:630, the duo row's rule for a missed drain).  Cost 500
    -- of the orb's 1000 (DRIVER_NOTES seam10 "falls by 500 (DWH)").
    -- raid seam40: the hammer is the scythe seats' alone, swung once (the
    -- reference: HAMMER in dps1|100 in 4 of 24 rooms, ELDER_MAUL in dps2|100
    -- in 8, one attack each; "no real room's freezer used it there", its
    -- first attacks are TWISTED_BOW 23/24 and TONALZTICS 23/24; m40i: the
    -- freezer's and dps2's second swings after a 0 put phase 100 at 57 ticks
    -- against the real 42 [32-52])
    opener = { weapon = "dragon_warhammer", cost = 500, tries = 1, give_up = 14 },
    -- the toxic blowpipe: PvM speed 3, rapid 2, reach 5 (wiki_Toxic_blowpipe
    -- .wikitext:41, :78); its swing seq 5061 (raid_play_tob_nylocas.lua)
    pipe = { item = "toxic_blowpipe_loaded", reach = 5 },
    -- The crab weapon of a real trio is the SCYTHE (Blert, 26 Regular scale-3
    -- Maiden rooms: the scythe on crabs in 25 / 25 / 19 of 26 rooms per
    -- threshold, the blowpipe in 6 / 7 / 4; sources/blert_api/maiden_trio_crabs/
    -- README.md). Darts land 0-15 on a 75 hp Matomenos (seam35m); the scythe
    -- kills it. The pipe stays as the fallback when the kit has no scythe.
    crab_weapon = { item = "scythe_of_vitur", reach = 1 },
    bow = "twisted_bow",
    -- her spawn tile (south-west of her 6x6 footprint): the origin the homes,
    -- the floor and the reach are written against
    body = { 6426, 92 },
    -- a Matomenos that stands two ticks on one tile outside her reach gap is
    -- frozen (they walk one tile every tick: maiden.crab_walk, grade B), and
    -- a cast's freeze is waited out this many ticks before it is cast again
    -- (Ice Barrage hits two ticks after the cast)
    -- raid seam33: one still tick is a freeze (s33m2 t241-245: at 2 the
    -- rangers kept shooting a crab the barrage had just stopped while two
    -- walkers went in)
    frozen_after = 1, recast_after = 4,
    -- her thresholds (maiden.threshold_percent 70,50,30, grade B) and how
    -- close to the next one the freezer puts the magic set on (the seam's
    -- choice: about four bow hits of her Normal trio pool)
    thresholds = { 0.70, 0.50, 0.30 }, prime = 0.05,
    -- a walking nylocas this close to her is the rangers' first target (the
    -- seam's choice: five ticks of walking, one bow swing and a bit)
    imminent = 5, freeze_min_gap = 4,
    -- raid seam40: how far a scythe seat steps off her for a WALKING
    -- nylocas the freezer's plan leaves (the reference's dps attack 1-3 adds
    -- a phase, first 4 ticks after the spawn: react.phase.70.dps1.attack_add
    -- 4 [3-16]); a frozen one is the freezer's
    melee_add_reach = 12,
    -- raid seam40: `freezer_melee30 = { casts = 4, item = "scythe_of_vitur" }`
    -- puts the freezer on her with the scythe after four barrages in her last
    -- form (the reference's freezer|30: SCEPTRE median 4, then SCYTHE 3).  OFF:
    -- measured on the three survey names (survey5) the clump it stopped
    -- barraging thawed and walked in at full health, 30 percent leaks 21 / 9 /
    -- 7 against 3 / 3 / 2 without it and the room 338 / 305 / 349 against 271 /
    -- 285 / 327; the real freezer leaves a clump that is already dead.
    freezer_melee30 = nil,
    -- raid seam40: the solo's scythe after its technique proofs (THE SOLO ON
    -- HER below; the mode's `solo_melee` switches it on)
    solo_melee = { item = "scythe_of_vitur" },
    -- raid seam35m: the freezer's plan (QD.raid._play_maiden_ice_plan) looks
    -- this many barrages ahead (four casts cover a 4's walk: 17 ticks from the
    -- far spawn, m35base), and the first lands this many ticks after the tick
    -- the client saw (calibrated from m35base's casts against the tick log)
    ice_depth = 4, ice_lead = 0,
    -- raid seam35m: Ice Barrage holds a nylocas 32 ticks here (m35b: S1 still
    -- t121-t153, "20 seconds", wiki Ice Barrage); the freezer re-casts on one
    -- frozen this long before it walks
    ice_refresh = 24,
    -- the tank steps out with this many bites left, anglerfish plus brew
    -- doses (the seam's choice; seam32's 4 anglerfish, and since seam33 a
    -- dose heals 16 to a bite's 22, so eight)
    tank_out = 8,
    -- raid seam54: super combat doses a scythe seat keeps past this room (the
    -- re-boost below never drinks them): one potion, the next room's door dose
    reboost_keep = 4,
    decide = "_play_maiden_decide",
    -- raid seam55: the events the play loop raises for this room
    -- (raid_play.lua "TRIGGERS AND WATCHES") and the reactions it registers
    events = { forms = "forms", add = "crab", add_event = "crab", label = "_play_maiden_label",
        projectiles = { [1578] = "blood_thrown" }, pools = { [1579] = "pool_landed" },
        boss_seqs = { [8092] = "storm_sent", [8091] = "blood_sent" },
        -- Ice Barrage's impact graphic (wiki Ice Barrage: graphic 369)
        freeze_spotanims = { [369] = true } },
    on_start = "_play_maiden_on_start",
})

-- ==========================================================================
-- raid seam55 play_tob_maiden_triggers_like_blert: THE TRIO ON TRIGGERS, as
-- the recorded raiders play it.  Source: the 26 Regular-mode scale-3 Blert
-- rooms (docs/minigames/theater_of_blood/sources/blert_api/maiden_trio_crabs,
-- streams build/blert_maiden/*.json; read per wave and per raider by
-- seam55's ref.py: the spawn tiles by maidenCrab.position, every
-- PLAYER_ATTACK's tick from the spawn, its target's position and the
-- raider's tile).  What they do, wave after wave:
--   * spawn positions from her SW tile: 0 (11,-8) 1 (11,12) 2 (15,-8)
--     3 (15,12) 4 (19,-8) 5 (19,12) 6 (23,-6) 7 (23,-8) 8 (23,10) 9 (23,12)
--     (46-48 of 52 waves each; the rest one tile off);
--   * THE FREEZER casts a barrage on the spawn's tick (its attack shows at
--     spawn + 1 in 17 / 18 / 19 of 26 waves at 70 / 50 / 30), first at
--     position 0 (14 / 18 / 13), then every 5 ticks (72 / 86 / 67 of the
--     gaps): +6 at 2 or 3, +11 at 4, +16 at 6, 7 or 9, from her east side
--     (15,-1..3: the freezer's tile at the first cast);
--   * THE SCYTHE SEATS stand on her NE corner (5,6) / (6,5) and leave her
--     for the north crabs only: the first crab swing is at +4 on position 1
--     (20 / 19 / 23 of 52 seat-waves), the second seat's at +9 on position 3
--     (5 / 6 / 1); one or two crab swings a wave (median), back on her
--     at +9 .. +14 (median); the 30 percent wave is spent on her (0 or 1
--     crab swings, 12 of 52 seat-waves none).
-- The plan below registers those reactions (QD.raid._play_maiden_on_start);
-- the default plan (decide) still carries the walks, the prayers, the
-- supplies and the gear, and a solo (Entry) registers nothing.
-- ==========================================================================
QD.RAID_MAIDEN_REF = {
    -- the freezer's order: one group per 5-tick cast, then the rest
    cast_groups = { { 0 }, { 2, 3 }, { 4 }, { 6, 7, 9 }, { 5, 8, 1 } },
    cast_casts = 8,           -- casts a wave at most (2-8, median 4 / 5 / 3.5)
    -- the scythe seats' crab: role -> { position, from (ticks after the
    -- spawn the press goes out: the swing lands about two ticks later),
    -- leave (ticks after the spawn it gives up and goes back on her),
    -- waves (the waves it does this in) }
    seat_crab = {
        -- `nth`: the seat takes the nth north crab of the burst in the order
        -- 1, 3, 5, 8, 9 (the reference's first crab is position 1 in 62 of
        -- 156 seat-waves, position 3 when 1 did not spawn)
        [1] = { nth = 1, from = 1, leave = 10, waves = { true, true, true } },
        [3] = { nth = 2, from = 5, leave = 14, waves = { true, true, false } },
    },
    -- the second trip: the reference's east seat leaves her again for the
    -- FROZEN south crabs at +18 .. +29 ("19:c2 24:c0 29:c4", "18:c2 23:c0
    -- 28:c0" from (6,5)), the kills that keep the leak at 1-2 a wave (the
    -- killed crabs die 20-50 ticks after the spawn: kill.py, 216 of 233
    -- under the freezer's barrage or a scythe); the north seat only for a
    -- frozen north crab close to her.  side, window (ticks after the spawn),
    -- the furthest gap to her it walks to, the waves.
    seat_clean = {
        [1] = { side = "north", from = 15, till = 35, gap = 4, waves = { true, true, true } },
        [3] = { side = "south", from = 15, till = 35, gap = 8, waves = { true, true, true } },
    },
    -- the 30 percent wave: the reference spends it on her (0-1 crab swings a
    -- seat) because she dies 48 [34-75] ticks after it; a seat leaves her
    -- only for a frozen crab within this gap of her (the one that walks in
    -- first: "9:c0@7,-2" at 30 percent)
    clean_gap_30 = 3,
    blood_pri = 8, cast_pri = 5, storm_pri = 3,
}

-- Position label (0-9) of a crab tile from her SW tile, by the spawn grid
-- above; nil off the grid.
function QD.raid._play_maiden_label(dx, dz)
    local col = math.floor((dx - 11 + 2) / 4)
    if col < 0 or col > 3 then return nil end
    if dz < 0 then
        if col == 3 then return (dz >= -7) and 6 or 7 end
        return col * 2
    elseif dz > 5 then
        if col == 3 then return (dz <= 11) and 8 or 9 end
        return col * 2 + 1
    end
    return nil
end

function QD.raid._play_maiden_on_start(st)
    if st.party <= 1 then return end
    st.on("crab_spawn", "_play_maiden_on_crab_spawn")
    st.on("blood_thrown", "_play_maiden_on_blood")
    st.on("pool_landed", "_play_maiden_on_pool")
    st.on("storm_sent", "_play_maiden_on_storm")
    st.watch("freezer_cast_due", "_play_maiden_cast_due", "_play_maiden_cast_change")
end

-- crab_spawn: the freezer opens the wave's cast clock; a scythe seat arms
-- its reference crab (the plan's add path presses it: party_wave).
function QD.raid._play_maiden_on_crab_spawn(st, v, ev)
    local m = st.m
    if m == nil or m.R == nil then return nil end
    m.ref = m.ref or { waves = {}, seat = {} }
    local W = m.ref.waves[ev.wave]
    if W == nil then
        W = { tick = v.tick, slots = {}, casts = 0 }
        m.ref.waves[ev.wave] = W
        m.ref.wave = ev.wave
    end
    W.slots[ev.slot] = ev.label
    local S = QD.RAID_MAIDEN_REF.seat_crab[st.role]
    if m.R.melee and S ~= nil and S.waves[math.min(ev.wave, 3)] and ev.label ~= nil then
        -- the burst lands on one tick: re-rank its north crabs on each spawn
        -- event and keep the seat's nth
        local order = { [1] = 1, [3] = 2, [5] = 3, [8] = 4, [9] = 5 }
        local north = {}
        for slot, label in pairs(W.slots) do
            if order[label] ~= nil then north[#north + 1] = { slot = slot, k = order[label] } end
        end
        table.sort(north, function(x, y) return x.k < y.k end)
        for slot, _ in pairs(m.ref.seat) do
            if m.ref.seat[slot].wave == ev.wave then m.ref.seat[slot] = nil end
        end
        local pick = north[S.nth]
        if pick ~= nil then
            m.ref.seat[pick.slot] = { from = W.tick + S.from, leave = W.tick + S.leave, wave = ev.wave }
            m.ref.armed = (m.ref.armed or 0) + 1
        end
    end
    return nil
end

-- The freezer's cast clock: the index of the 5-tick cast due now in the
-- newest wave (-1 when no wave or past its casts).
function QD.raid._play_maiden_cast_due(st, v)
    local m = st.m
    if m == nil or m.R == nil or not m.R.freezer or m.ref == nil or m.ref.wave == nil then return -1 end
    local W = m.ref.waves[m.ref.wave]
    local k = (v.tick - W.tick) // st.plan.cast_every
    if k >= QD.RAID_MAIDEN_REF.cast_casts then return -1 end
    return k
end

-- The cast for the clock's new index: the first group in the reference's
-- order holding a live crab that walks (not frozen, not cast at inside
-- recast_after, not at her yet) in barrage range; the 3x3 with the most
-- walkers wins inside a group.  None walking: the oldest freeze past
-- ice_refresh.  Nothing: no cast (the reference's waves end at 2-8 casts).
function QD.raid._play_maiden_cast_change(st, v, k, old)
    if k < 0 then return nil end
    local P, m = st.plan, st.m
    local adds = st.ev.adds
    local b = v.ev_boss
    if b == nil then return nil end
    local W = m.ref.waves[m.ref.wave]
    local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
    local function walking(slot, a)
        local cast_at = m.crab_cast and m.crab_cast[slot]
        return not a.gone and not a.frozen and QD.raid._play_gap(b, a.x, a.z) > 1
            and not (cast_at ~= nil and v.tick - cast_at < P.recast_after)
    end
    local target, tn, why = nil, -1, nil
    for g, group in ipairs(QD.RAID_MAIDEN_REF.cast_groups) do
        for _, pos in ipairs(group) do
            for slot, a in pairs(adds) do
                if a.label == pos and walking(slot, a) and cheb(a.x, a.z, v.me.x, v.me.z) <= 10 then
                    local n = 0
                    for s2, o in pairs(adds) do
                        if walking(s2, o) and math.abs(o.x - a.x) <= 1 and math.abs(o.z - a.z) <= 1 then n = n + 1 end
                    end
                    if n > tn then target, tn, why = slot, n, "ref g" .. g .. " pos" .. pos .. " x" .. n end
                end
            end
        end
        if target ~= nil then break end
    end
    if target == nil then
        -- every walker is frozen: the reference keeps casting every 5 ticks
        -- (4-8 casts a wave; "11:c4 21:c4 26:c4") at the frozen crabs, and
        -- its barrage is what kills them (kill.py: 64 + 11 + 7 of 233 kills
        -- the freezer's own, 50 more with no direct hit = the 3x3).  The
        -- clump with the most live crabs in its 3x3; a tie, the crab
        -- frozen longest (the first to walk again).
        local best, bn, bage = nil, -1, -1
        for slot, a in pairs(adds) do
            if not a.gone and QD.raid._play_gap(b, a.x, a.z) > 1 and cheb(a.x, a.z, v.me.x, v.me.z) <= 10 then
                local n = 0
                for _, o in pairs(adds) do
                    if not o.gone and QD.raid._play_gap(b, o.x, o.z) > 1 and math.abs(o.x - a.x) <= 1 and math.abs(o.z - a.z) <= 1 then n = n + 1 end
                end
                local since = m.crab_frozen and m.crab_frozen[slot]
                local age = since ~= nil and (v.tick - since) or 0
                if n > bn or (n == bn and age > bage) then best, bn, bage = slot, n, age end
            end
        end
        if best ~= nil then target, why = best, "ref clump x" .. bn .. " age" .. bage end
    end
    if target == nil then return nil end
    W.casts = W.casts + 1
    m.last_cast = v.tick
    m.crab_cast = m.crab_cast or {}
    m.crab_cast[target] = v.tick
    local _, mg = QD.skill.read("magic")
    m.casts[#m.casts + 1] = { tick = v.tick, slot = target, result = "trigger", wave = m.waves, form = #m.forms,
        magic = mg and mg.level, why = why, k = k, since = v.tick - W.tick }
    return { pri = QD.RAID_MAIDEN_REF.cast_pri, by = "freezer_cast_due",
        cast = { spell = P.freeze_spell, symbol = P.crab[st.mode], slot = target, why = why } }
end

-- The shortest safe tile off a tile something lands on: one tile first, then
-- two; never a pool, never another throw's tile; a scythe seat keeps to a
-- tile beside her when one is safe.
function QD.raid._play_maiden_off_tile(st, v, lx, lz)
    local P, m = st.plan, st.m
    local b = v.ev_boss
    local marked = {}
    for k, on in pairs(v.shadows or {}) do if on then marked[k] = true end end
    for _, e in ipairs(v.events or {}) do
        if e.name == "blood_thrown" or e.name == "pool_landed" then marked[e.x * 100000 + e.z] = true end
    end
    for _, k in pairs(st.ev.projs or {}) do
        if k.x ~= nil then marked[k.x * 100000 + k.z] = true end
    end
    -- the blood spawns' trails (loc 32984) hurt as a pool does: the handler
    -- runs before decide, whose v.marks carry them, so it reads them itself
    -- (survey5b: two raiders walked a trail at 36 a tick and died)
    if api_drive.loc_copies ~= nil then
        local lr, locs = api_drive.loc_copies(P.trail_loc, 0)
        if lr == "ok" and type(locs) == "table" then
            for _, l in ipairs(locs) do marked[l.x * 100000 + l.z] = true end
        end
    end
    local ox, oz = m.ox or 0, m.oz or 0
    local best, bx, bz = nil, nil, nil
    for r = 1, 2 do
        for dx = -r, r do
            for dz = -r, r do
                if math.max(math.abs(dx), math.abs(dz)) == r then
                    local x, z = v.me.x + dx, v.me.z + dz
                    local on_floor = x >= P.floor[1] + ox and x <= P.floor[3] + ox and z >= P.floor[2] + oz and z <= P.floor[4] + oz
                    local under = b ~= nil and x >= b.x and x <= b.x + (b.size or 1) - 1 and z >= b.z and z <= b.z + (b.size or 1) - 1
                    if on_floor and not under and not marked[x * 100000 + z] and (x ~= lx or z ~= lz) then
                        local score = r * 10
                        if m.R ~= nil and m.R.melee and b ~= nil and QD.raid._play_gap(b, x, z) ~= 1 then score = score + 5 end
                        if best == nil or score < best then best, bx, bz = score, x, z end
                    end
                end
            end
        end
        if best ~= nil then break end
    end
    return bx, bz
end

-- blood_thrown on this raider's tile: step by the shortest safe tile before
-- it lands (the T-1 rule: it is judged against the tile of the tick before
-- impact); the freezer's cast is HELD a tick and goes out from the new tile.
function QD.raid._play_maiden_on_blood(st, v, ev)
    local m = st.m
    if m == nil or m.R == nil or not ev.mine then return nil end
    local x, z = QD.raid._play_maiden_off_tile(st, v, ev.x, ev.z)
    if x == nil then return nil end
    m.trig_until = v.tick + math.max(ev.ticks, 1)
    m.trig_steps = (m.trig_steps or 0) + 1
    return { pri = QD.RAID_MAIDEN_REF.blood_pri, walk = { x = x, z = z }, why = "blood on my tile" }
end

-- pool_landed under this raider (a throw it did not see, or an extra): off.
function QD.raid._play_maiden_on_pool(st, v, ev)
    local m = st.m
    if m == nil or m.R == nil or not ev.mine then return nil end
    local x, z = QD.raid._play_maiden_off_tile(st, v, ev.x, ev.z)
    if x == nil then return nil end
    m.trig_until = v.tick + 1
    m.trig_steps = (m.trig_steps or 0) + 1
    return { pri = QD.RAID_MAIDEN_REF.blood_pri, walk = { x = x, z = z }, why = "pool under me" }
end

-- storm_sent: the prayer is Protect from Magic (wiki: the blackstorm is
-- magic); the eat stays the plan's (_play_supplies, by the largest hit due).
function QD.raid._play_maiden_on_storm(st, v, ev)
    local m = st.m
    if m == nil or m.R == nil then return nil end
    m.storms_seen = (m.storms_seen or 0) + 1
    return { pri = QD.RAID_MAIDEN_REF.storm_pri, want = { protectfrommagic = true }, why = "storm" }
end

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
    v.crabs, v.slugs, v.crab_dead = {}, {}, {}
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
            elseif m.ids.crab[row.npc_id] or m.ids.crab[row.base_npc_id] then
                -- raid seam33: an empty bar is a kill, never a leak
                v.crab_dead[row.slot] = true
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

-- raid seam35m: a Matomenos has REACHED her when its 2x2 footprint is within
-- one tile of her 6x6 ("Reached her" is blert's distanceTo2D <= 1, tob.constant
-- :480-494): over the crab's south-west anchor that is the rectangle two tiles
-- out on her west and south faces and one on her north and east, tested at the
-- start of each tick on the tile it stood on at the end of the last
-- (tob_maiden.rs2 [proc,tob_maiden_crab_tick]: arrived before the walk), so
-- a crab frozen inside it is absorbed all the same.  m35a w1: S1 frozen on
-- 5,-2 (her gap 2 as a 1x1) was absorbed the next tick.
function QD.raid._play_maiden_crab_in(b, x, z)
    local dx, dz = x - b.x, z - b.z
    return dx >= -2 and dx <= 6 and dz >= -2 and dz <= 6
end

-- raid seam35m THE FREEZER'S PLAN (PLAY_NOTES.md "Maiden, Normal trio").
-- The sources' freeze order is a plan over where the nylocas WILL be, not
-- where they are: "There are 5 potential nylocas spawns on the north and
-- south side ... N1, N2, and S1 cannot be clumped together with other
-- nylocas, but the other spawns can all be frozen on top of each other in
-- front of Maiden" (W:637); "A solo freezer should hover their mouse over
-- the S1's spawn position ... and reactively freeze another if no S1 spawns.
-- Clumping 3s and 4s should be prioritised over freezing a single S2 or N2
-- spawn; if it is necessary to leave a single nylocas, the DPS roles should
-- attack it as it moves to the boss" (W:643).  A person who knows the room
-- sees each crab's walk: "which will walk straight toward her" (ET 2.3) --
-- diagonally until level with her footprint, then straight along her top or
-- bottom row (seam35m m35base: N3 from 19,12 reached 6,5 in 13 ticks, S3
-- from 19,-8 reached 6,0 in 13; every unfrozen crab of the five names'
-- surveys matched this walk), one tile a tick (maiden.crab_walk, grade B).
-- Ice Barrage freezes the target and every nylocas in its 3x3 on the cast's
-- own tick (m35base: npc_spotanim 369 on the cast tick, the crab's last
-- npc_tile that tick), and the spell is cast every `cast_every` ticks.  So
-- the plan tries every order of the next `ice_depth` barrages (each on any
-- nylocas the freezer reaches, a frozen anchor included: the walkers
-- behind come through its 3x3, W:637 "frozen on top of each other") and
-- keeps the order that freezes the most before they reach her; among equals
-- the one whose LEFT nylocas reaches her last (the DPS need the time, W:643),
-- then the one that freezes more with the first cast.  Every raider runs
-- it on what its own client sees, so the rangers know which crab the
-- freezer will leave ("if it is necessary to leave a single nylocas, the
-- DPS roles should attack it", W:643).
-- `crabs`: { slot, x, z, frozen }; `d0`: ticks from now to the first cast;
-- `me`: the freezer's tile (nil: no range test); `delay`: the ticks a cast
-- sent at an offset resolves after it (nil: 0 or 1, both checked).  Returns
-- { target = slot or nil, frozen = n, total = walkers, left = {slot=true},
--   first = n frozen by the first cast, left_arrive = the left crabs' walks,
--   each short of 20 ticks, negative }.
function QD.raid._play_maiden_ice_plan(st, v, crabs, d0, me, delay)
    local P = st.plan
    local b = v.boss
    if b == nil or #crabs == 0 then return nil end
    local bx, bz = b.x, b.z
    local horizon = d0 + P.cast_every * (P.ice_depth - 1) + 2
    -- one step toward the nearest tile of her footprint (tob_maiden.rs2
    -- [proc,tob_maiden_crab_goal]: the crab's own tile clamped into her 6x6)
    local function step(x, z)
        if x > bx + 5 then x = x - 1 elseif x < bx then x = x + 1 end
        if z > bz + 5 then z = z - 1 elseif z < bz then z = z + 1 end
        return x, z
    end
    -- each crab's tile at every offset 0..horizon (after that tick's step, the
    -- tile a cast on that tick freezes), and the first offset it stands in
    -- the arrival rectangle (absorbed the tick after: it cannot be saved
    -- from then on).  `lead`: the client draws the last tick's tiles, so a
    -- cast sent now resolves after one more step (m35a t132: seen 12,0,
    -- frozen on 11,0); a crab seen on its first tick is drawn where the cast
    -- finds it (m35a t121: seen and frozen on 18,-7).
    local paths, arrive = {}, {}
    for i, c in ipairs(crabs) do
        local x, z = c.x, c.z
        paths[i] = {}
        arrive[i] = 999
        if not c.frozen then
            for _ = 1, (c.lead or 0) do x, z = step(x, z) end
        end
        for d = 0, horizon do
            if d > 0 and not c.frozen and arrive[i] == 999 then x, z = step(x, z) end
            paths[i][d] = { x, z }
            if arrive[i] == 999 and not c.frozen and QD.raid._play_maiden_crab_in(b, x, z) then arrive[i] = d end
        end
    end
    local n = #crabs
    local best = nil
    local frozen_at = {}
    local walkers = 0
    for i, c in ipairs(crabs) do
        if c.frozen then frozen_at[i] = -1 else walkers = walkers + 1 end
    end
    local function where(i, d)
        -- a crab frozen at offset f stays on its tile from then on
        local f = frozen_at[i]
        if f ~= nil and f >= 0 and f < d then return paths[i][f] end
        return paths[i][d]
    end
    local first_target, first_n = nil, 0
    local function score()
        -- `left_arrive`: the DPS's time on what is left, each left crab's walk
        -- counted to 20 ticks (m35c w2: leaving N1 and S1, six ticks each,
        -- instead of N1 and S3, thirteen, put both into her)
        local fz, left_arrive = 0, 0
        local left = {}
        for i, c in ipairs(crabs) do
            if not c.frozen then
                if frozen_at[i] ~= nil then fz = fz + 1
                else
                    left[c.slot] = true
                    left_arrive = left_arrive + math.min(arrive[i], 20) - 20
                end
            end
        end
        local better = best == nil or fz > best.frozen
            or (fz == best.frozen and left_arrive > best.left_arrive)
            or (fz == best.frozen and left_arrive == best.left_arrive and first_n > best.first)
        if better then
            best = { target = first_target, frozen = fz, total = walkers, left = left, first = first_n, left_arrive = left_arrive }
        end
    end
    local function cast(k)
        if k >= P.ice_depth then score() return end
        local d = d0 + P.cast_every * k
        local any = false
        for t = 1, n do
            -- the cast resolves on the tick planned or the one after
            -- (svdplaymaide w1: casts sent t107/t112 resolved t108/t113, m35a's
            -- t132 on t132): a crab counts only if it is in the target's 3x3
            -- on both ticks and has not reached her by the second
            -- (`delay` measured: the one tick it resolves on)
            local d1, d2 = d + (delay or 0), d + (delay or 1)
            local tp, tq = where(t, d1), where(t, d2)
            local alive = crabs[t].frozen or frozen_at[t] ~= nil or arrive[t] > d2
            if alive and (me == nil or math.max(math.abs(tp[1] - me.x), math.abs(tp[2] - me.z)) <= 10) then
                local got = {}
                for i = 1, n do
                    if frozen_at[i] == nil and arrive[i] > d2 then
                        local p, q = paths[i][d1], paths[i][d2]
                        if math.abs(p[1] - tp[1]) <= 1 and math.abs(p[2] - tp[2]) <= 1
                            and math.abs(q[1] - tq[1]) <= 1 and math.abs(q[2] - tq[2]) <= 1 then got[#got + 1] = i end
                    end
                end
                if #got > 0 then
                    any = true
                    for _, i in ipairs(got) do frozen_at[i] = d1 end
                    if k == 0 then first_target, first_n = crabs[t].slot, #got end
                    cast(k + 1)
                    for _, i in ipairs(got) do frozen_at[i] = nil end
                    if k == 0 then first_target, first_n = nil, 0 end
                end
            end
        end
        if not any then score() end
    end
    cast(0)
    return best
end

-- THE PARTY'S MATOMENOS (raid seam32 play_tob_maiden_normal; PLAY_NOTES.md
-- "Maiden, Normal trio").  Every raider tracks each nylocas it can see by
-- the tick it last stepped: they walk one tile every tick (maiden.crab_walk,
-- grade B), so one that has stood `frozen_after` ticks outside her reach
-- gap is frozen -- what a person reads off the screen as the ice on it.
--   The FREEZER puts the magic set on in one block when a wave is seen (as
--   the Entry plan), and casts Ice Barrage every `cast_every` ticks: first
--   on a walking nylocas, nearest her first ("freeze the crabs that spawn
--   closest to Maiden first", 10Boot 0:07:40; W:639 "freeze N1 on the first
--   tick possible"), leaving one already at her gap for the rangers ("this
--   crab should not get frozen.  Everyone else ... kill it before it
--   reaches the boss", 10Boot 0:08:48); then, all frozen, on the frozen
--   nylocas with the most others in its 3x3 ("If you freeze correctly ...
--   most of the crabs get stuck in a big clump.  The freezer can barrage
--   these down until they're all dead", 10Boot 0:08:14; W:639 "Freezers
--   should then barrage the clump until it is dead").  The ranged set goes
--   back on when no nylocas is left.
--   The RANGERS shoot the walking nylocas nearest her first ("Everyone else
--   should machine gun down the crabs that aren't in the clump", 10Boot
--   0:08:14; W:639 "DPS roles should kill the stray nylocas before getting
--   back on Maiden"), then the frozen ones nearest her (the freeze wears
--   off), then her.  The north ranger (seat 3) shoots the blood spawns when
--   no nylocas is up (W:598-600 "Kill or avoid"); the tank stays on her.
-- Returns the add to shoot ({row, symbol}) or nil.
-- raid seam54 play_tob_maiden_whole THE FREEZER'S BLOOD RULE: never a second
-- tick on a blood tile while holding a cast.  The cast is a click on a crab,
-- and a click replaces the walk, so a barrage sent while the freezer is on a
-- pool, a trail or a landing splat (v.marks: the pools, the trails and the
-- splats in flight) or while it is stepping off one keeps it standing there
-- (camera seam53 survey2: the freezer stood 3-4 ticks in her blood at its
-- home tile, 6440-6441,157-161, and died from 90+ on 3 of 3 names).  So the
-- step goes first (the decide's "pool" / "dodge" walk, the shortest safe
-- tile) and the cast waits for the tick it arrives; the target and its
-- order are kept.
function QD.raid._play_maiden_in_blood(v, m)
    local me = v.me
    if me ~= nil and v.marks ~= nil and v.marks[me.x * 100000 + me.z] then return true end
    return m.move ~= nil and (m.move.why == "pool" or m.move.why == "dodge")
end

function QD.raid._play_maiden_party_wave(st, v, m, R, mg, moving, gap_to_her, cheb)
    local P = st.plan
    local me = v.me
    local wave = #m.forms
    for _, c in ipairs(v.crabs) do
        local tr = m.crab_track[c.slot]
        if tr == nil then
            tr = { x = c.x, z = c.z, sx = c.x, sz = c.z, moved = v.tick, first = v.tick, wave = wave, still = 0 }
            m.crab_track[c.slot] = tr
        elseif tr.x ~= c.x or tr.z ~= c.z then
            tr.x, tr.z, tr.moved, tr.still = c.x, c.z, v.tick, 0
        elseif tr.seen == v.tick - 1 and v.tick - tr.first >= 2 then
            -- (the view after the first repeats it: the first view of a new
            -- crab already shows its first step, m35a t121; svc w2's plan saw
            -- six "frozen" spawns and nothing to freeze)
            -- raid seam35m: still only across two views of consecutive ticks
            -- (m35b w2: the magic-set block skipped the views, the stale
            -- `moved` read every walker as frozen and the plan saw 0 walkers).
            -- The first still view is two ticks after the cast that froze it
            -- (the client draws the last tick): the freezer's cadence, for
            -- the rangers' copy of its plan.
            tr.still = (tr.still or 0) + 1
            if tr.still == 1 then
                m.ice_seen = v.tick - 2
                -- the freezer's own: the cast on this crab, and the ticks it
                -- took to resolve (svcplaymaide w1: every cast one tick late,
                -- w2-w3 none; constant within a wave)
                if m.crab_cast ~= nil and m.crab_cast[c.slot] ~= nil then
                    local dl = v.tick - 2 - m.crab_cast[c.slot]
                    if dl >= 0 and dl <= 1 then m.ice_delay, m.ice_delay_wave = dl, wave end
                end
            end
        end
        tr.seen, tr.hr = v.tick, c.health_ratio
    end
    -- raid seam33: THE LEAKS a person counts.  A nylocas that was beside her
    -- last tick with health on its bar and is gone this tick walked into her
    -- ("If they manage to reach the Maiden, she will be healed ... in
    -- addition to increasing the damage dealt by her blackstorm", W:594); one
    -- killed shows an empty bar first (the see's `alive` drops it then).
    m.leaks = m.leaks or 0
    for slot, tr in pairs(m.crab_track) do
        if tr.seen == v.tick - 1 and not tr.gone and not v.crab_dead[slot] and QD.raid._play_maiden_crab_in(v.boss, tr.x, tr.z) and (tr.hr == nil or tr.hr ~= 0) then
            local here = false
            for _, c in ipairs(v.crabs) do if c.slot == slot then here = true end end
            if not here then
                tr.gone = true
                m.leaks = m.leaks + 1
                m.leak_ticks = m.leak_ticks or {}
                m.leak_ticks[#m.leak_ticks + 1] = v.tick
            end
        elseif tr.seen ~= nil and tr.seen < v.tick - 1 then
            tr.gone = true
        end
    end
    -- raid seam33: the lane, from the spawn row (south rows z 85/87, north
    -- 101/103: PLAY_NOTES "Maiden, Normal trio"): the freezer takes the south
    -- first ("freeze the crabs that spawn closest to Maiden first.
    -- Prioritizing the south side of the arena, since those crabs reach the
    -- boss first", 10Boot 0:07:40-0:08:14) and the rangers the north ("camping
    -- on the north side of the arena gives the mage space to hit the south
    -- freezes and allows you to get the best access to the crab that doesn't
    -- get frozen", 10Boot 0:08:48)
    local function south(c)
        local tr = m.crab_track[c.slot]
        return tr ~= nil and v.boss ~= nil and tr.sz < v.boss.z
    end
    -- N1, the closest north spawn (6436,103 for her at 6426,92: the
    -- north row, ten tiles east of her spawn tile): "if a crab spawns at the
    -- closest north side tile, this crab should not get frozen.  Everyone
    -- else in the raid should just try and kill it before it reaches the
    -- boss" (10Boot 0:08:48); "N1, N2, and S1 cannot be clumped" (W:637)
    local function n1(c)
        local tr = m.crab_track[c.slot]
        return tr ~= nil and v.boss ~= nil and tr.sz > v.boss.z + 5 and tr.sx - v.boss.x <= 10
    end
    local function frozen(c)
        local tr = m.crab_track[c.slot]
        return tr ~= nil and (tr.still or 0) >= P.frozen_after and not QD.raid._play_maiden_crab_in(v.boss, c.x, c.z)
    end
    -- raid seam35m: a barrage on its cadence beats finishing a dodge while a
    -- nylocas still walks (svbplaymaide w2: a dodge slipped the third cast
    -- from t211 to t212, and the N4 pair and the S4 pair, both in reach of
    -- casts at t211 and t216, needed one each; the 212 cast froze one pair
    -- and the other walked in).  The click on the crab ends the walk.
    local function walkers_up()
        for _, c in ipairs(v.crabs) do
            if not frozen(c) and not QD.raid._play_maiden_crab_in(v.boss, c.x, c.z) then return true end
        end
        return false
    end
    for _, c in ipairs(v.crabs) do
        if frozen(c) and m.crab_frozen[c.slot] == nil then m.crab_frozen[c.slot] = v.tick end
        if not frozen(c) then m.crab_frozen[c.slot] = nil end
    end
    if R.freezer then
        -- raid seam40 THE FREEZER'S LAST WAVE: the reference's freezer
        -- barrages the 30 percent wave (role.freezer.phase.30.attacks_add 4
        -- [2-7]; SCEPTRE 20/24 rooms, 4 attacks) and then walks onto her with
        -- the scythe (SCYTHE in freezer|30 19 of 24 rooms, 3 attacks;
        -- dist_boss 1 [1-3]; reference/maiden_normal_3.json).  After its
        -- `casts` barrages in her last form the scythe goes on in one block and
        -- the attack press walks it to her side.
        if m.fz == "melee" then
            return nil
        end
        local F30 = P.freezer_melee30
        if F30 ~= nil and m.fz == "magic" and st.boss_symbol ~= nil and st.boss_symbol:find("_30", 1, true) ~= nil then
            local n30 = 0
            for _, c in ipairs(m.casts) do
                if c.form == wave then n30 = n30 + 1 end
            end
            if n30 >= F30.casts then
                local hr, has = QD.inv.has(F30.item)
                if hr == "ok" and has then
                    QD.raid._play_maiden_block(st, v, "scythe (30)", { F30.item })
                    m.fz, m.melee30 = "melee", v.tick
                    st.engaged = false
                    return nil
                end
            end
        end
        -- PRIMED: "A solo freezer should hover their mouse over the S1's spawn
        -- position when Maiden is close to spawning a new set of nylocas"
        -- (W:643), and N1 is frozen "on the first tick possible" (W:639): the
        -- magic set goes on while her health bar (what the screen shows) is
        -- within `prime` of the next threshold, so the first barrage leaves on
        -- the tick after the spawn (s32mzn4: the swap on the spawn tick put
        -- the first cast six ticks after it, when N1 and S1 had reached her)
        local b = v.boss
        local frac = nil
        if b ~= nil and b.health_ratio ~= nil and b.health_scale ~= nil and b.health_ratio >= 0 and b.health_scale > 0 then
            frac = b.health_ratio / b.health_scale
        end
        local next_thr = P.thresholds[wave + 1]
        if m.fz == nil and #v.crabs == 0 and next_thr ~= nil and frac ~= nil and frac <= next_thr + P.prime then
            local restore = nil
            if mg.level < P.freeze_level and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY then restore = QD.raid._play_maiden_restore() end
            QD.raid._play_maiden_block(st, v, "magic set (primed)", P.magic_set, restore)
            m.fz, m.fz_tick, m.fz_cast, m.primed = "magic", v.tick, {}, wave
            m.primes = (m.primes or 0) + 1
            m.halt = v.tick
            st.engaged = false
            return nil
        end
        if m.fz == "magic" and m.primed ~= nil and #v.crabs == 0 then
            -- held until the wave is seen (the form changes on the spawn
            -- tick, maiden.crab_spawn_tick 0), at most 60 ticks
            if v.tick - m.fz_tick <= 60 then return nil end
            m.primed = nil
        end
        if m.fz == "magic" and m.primed ~= nil and #v.crabs > 0 then
            m.primed = nil
            m.waves = m.waves + 1
        end
        if m.fz == nil and #v.crabs > 0 then
            local restore = nil
            if mg.level < P.freeze_level and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY then restore = QD.raid._play_maiden_restore() end
            QD.raid._play_maiden_block(st, v, "magic set", P.magic_set, restore)
            m.fz, m.fz_tick, m.fz_cast = "magic", v.tick, {}
            m.waves = m.waves + 1
            m.halt = v.tick
            st.engaged = false
        elseif m.fz == "magic" and #v.crabs == 0 then
            QD.raid._play_maiden_block(st, v, "ranged set", P.ranged_set)
            m.fz = nil
            st.engaged = false
        elseif m.fz == "magic" and m.ref == nil and v.tick - m.last_cast >= P.cast_every and (not moving or walkers_up())
            and not QD.raid._play_maiden_in_blood(v, m) then
            -- a walking nylocas the barrage can still stop: the one with the
            -- most walking nylocas in its 3x3 (Ice Barrage's area: "the other
            -- spawns can all be frozen on top of each other", W:637), nearest
            -- her first among equals (s32mzn2: one freeze every 5 ticks, nearest
            -- first, stopped 3 of 6 a threshold)
            local function walking(c)
                local cast_at = m.crab_cast[c.slot]
                local waiting = cast_at ~= nil and v.tick - cast_at < P.recast_after
                -- Ice Barrage lands two ticks after the cast (s32mzn8:
                -- npc_spotanim 369 two ticks after each cast), and a nylocas
                -- walks a tile a tick, so one nearer than `freeze_min_gap` reaches
                -- her before the ice does: that one is the rangers'
                return not frozen(c) and not waiting and not n1(c) and gap_to_her(c.x, c.z) >= P.freeze_min_gap
            end
            -- The target may itself be frozen: the nylocas behind walk into
            -- a frozen one's tile ("frozen on top of each other in front of
            -- Maiden", W:637), so a barrage on the frozen one stops them too
            -- (s32mzn3: north lane 1080 frozen at 6438,97 while 1081/1082
            -- walked through its 3x3 two ticks later, unstopped).
            local target, why, best_n = nil, nil, 0
            -- raid seam35m: the plan over where they WILL be (_play_maiden_ice_plan)
            local list = {}
            -- raid seam40: with scythe seats on her north-east corner the
            -- north walkers are theirs (the rank in the seats' own pick), so
            -- the plan is made over the south lanes while one still walks
            -- ("Prioritizing the south side of the arena, since those crabs
            -- reach the boss first", 10Boot 0:07:40; m40f w1: the plan froze
            -- a north walker at t126 while the south pair walked past the
            -- freezer's tile into her at 75 each)
            local south_only = false
            local last_wave = st.boss_symbol ~= nil and st.boss_symbol:find("_30", 1, true) ~= nil
            if P.south_first and P.roles[1] ~= nil and P.roles[1].melee and not last_wave then
                for _, c in ipairs(v.crabs) do
                    if south(c) and not frozen(c) and not QD.raid._play_maiden_crab_in(v.boss, c.x, c.z) then south_only = true end
                end
            end
            for _, c in ipairs(v.crabs) do
                if not south_only or south(c) or frozen(c) then
                    list[#list + 1] = { slot = c.slot, x = c.x, z = c.z, frozen = frozen(c), lead = (m.crab_track[c.slot] ~= nil and m.crab_track[c.slot].first == v.tick) and 0 or 1 }
                end
            end
            local plan = QD.raid._play_maiden_ice_plan(st, v, list, P.ice_lead, me, m.ice_delay_wave == wave and m.ice_delay or nil)
            if plan ~= nil and plan.target ~= nil then
                for _, c in ipairs(v.crabs) do
                    if c.slot == plan.target then
                        target, best_n = c, 99
                        why = "plan" .. plan.first .. "/" .. plan.frozen .. "of" .. plan.total .. "@" .. (c.x - v.boss.x) .. "," .. (c.z - v.boss.z)
                    end
                end
            end
            m.ice_plans = m.ice_plans or {}
            if plan ~= nil and m.ice_plans[m.waves] == nil then
                m.ice_plans[m.waves] = { tick = v.tick, frozen = plan.frozen, total = plan.total }
            end
            for _, c in ipairs(v.crabs) do
                if best_n < 99 and cheb(c.x, c.z, me.x, me.z) <= 10 then
                    local n = 0
                    for _, o in ipairs(v.crabs) do
                        if walking(o) and math.abs(o.x - c.x) <= 1 and math.abs(o.z - c.z) <= 1 then n = n + 1 end
                    end
                    local better = n > best_n
                    if n > 0 and n == best_n then
                        local cw, tw = walking(c), walking(target)
                        -- the one that reaches her first (its gap), then the
                        -- south one ("freeze the crabs that spawn closest to
                        -- Maiden first.  Prioritizing the south side ...
                        -- since those crabs reach the boss first", 10Boot
                        -- 0:07:40: the south is a tie-break for the arrival;
                        -- svdplaymaide w2, south-first left N2 at 6440,103,
                        -- nine ticks out, to walk in)
                        local cs, ts = south(c), south(target)
                        local cg, tg = gap_to_her(c.x, c.z), gap_to_her(target.x, target.z)
                        better = (cw and not tw) or (cw == tw and cg < tg) or (cw == tw and cg == tg and cs and not ts)
                    end
                    if better then target, best_n, why = c, n, (walking(c) and "walking" or "anchor") .. n end
                end
            end
            -- raid seam35m: a freeze about to wear off is refreshed first (Ice
            -- Barrage holds `ice_hold` ticks: m35b S1 frozen t121, walking
            -- t153, absorbed t158 with no ranger in reach of it), then the
            -- clump ("Freezers should then barrage the clump until it is dead",
            -- W:639), the one frozen longest among equals
            local function ice_age(c)
                return v.tick - math.max(m.crab_frozen[c.slot] or v.tick, m.ice_on[c.slot] or 0)
            end
            if target == nil then
                local oldest = nil
                for _, c in ipairs(v.crabs) do
                    if frozen(c) and ice_age(c) >= P.ice_refresh and (oldest == nil or ice_age(c) > ice_age(oldest)) then oldest = c end
                end
                if oldest ~= nil and cheb(oldest.x, oldest.z, me.x, me.z) <= 10 then target, why = oldest, "refresh" .. ice_age(oldest) end
            end
            if target == nil then
                local best, best_age = -1, -1
                for _, c in ipairs(v.crabs) do
                    local near = 0
                    for _, o in ipairs(v.crabs) do
                        if math.abs(o.x - c.x) <= 1 and math.abs(o.z - c.z) <= 1 then near = near + 1 end
                    end
                    local age = frozen(c) and ice_age(c) or -1
                    if (near > best or (near == best and age > best_age)) and (frozen(c) or not n1(c)) and cheb(c.x, c.z, me.x, me.z) <= 10 then
                        target, best, best_age, why = c, near, age, "clump" .. near
                    end
                end
            end
            if target ~= nil then
                local cr, cd = QD.player.cast(P.freeze_spell, P.crab[st.mode], 1, 2, { slot = target.slot, quick = true })
                st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
                m.last_cast = v.tick
                m.crab_cast[target.slot] = v.tick
                for _, o in ipairs(v.crabs) do
                    if math.abs(o.x - target.x) <= 1 and math.abs(o.z - target.z) <= 1 then m.ice_on[o.slot] = v.tick end
                end
                -- raid seam40: `form`, her threshold the cast was made in (m.waves
                -- does not count a wave that spawns while the last one's clump
                -- still holds the magic set: survey3, "waves 2" with casts in
                -- all three thresholds)
                m.casts[#m.casts + 1] = { tick = v.tick, slot = target.slot, result = tostring(cr), wave = m.waves, form = wave, magic = mg.level, why = why }
                if cr ~= "ok" and #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " cast " .. tostring(cr) .. ": " .. string.sub(tostring(cd), 1, 120) end
            end
        end
        return nil
    end
    -- a ranger: a walking nylocas about to reach her (gap <= `imminent`)
    -- first, nearest her; then the frozen one frozen longest (its ice wears
    -- off first: s32mzn7, three of ten leaks walked in after a 32-42 tick
    -- freeze nobody finished); then any walking one, nearest her
    -- raid seam33: every WALKING nylocas before any frozen one, nearest her
    -- first: the frozen clump is the freezer's ("The freezer can barrage
    -- these down until they're all dead.  Everyone else should machine gun
    -- down the crabs that aren't in the clump", 10Boot 0:08:14).  s33m1 w2:
    -- the rangers shot the four frozen south crabs while the two north ones
    -- (gap 7 at the spawn, rank 2 under seam32's order) walked in with 2-3
    -- hits on them.  A frozen one is shot only when nothing walks.
    -- raid seam35m: the rangers run the freezer's plan on what they see and
    -- shoot the nylocas it LEAVES ("if it is necessary to leave a single
    -- nylocas, the DPS roles should attack it as it moves to the boss",
    -- W:643), never one it is about to freeze (a crab killed on its walk is
    -- one the clump did not get; m35base w1: the darts killed the far N at
    -- gap 5 while the barrage was two casts from it).  The freezer casts on
    -- the wave's first tick and every `cast_every` after (its m.casts), so
    -- the next cast is the next multiple of that from the wave's first tick.
    -- raid seam55: on triggers a scythe seat takes the crab its crab_spawn
    -- armed (the reference's position and window, QD.RAID_MAIDEN_REF
    -- seat_crab) and nothing else: back on her outside it
    if R.melee and m.ref ~= nil then
        for _, c in ipairs(v.crabs) do
            local s = m.ref.seat[c.slot]
            if s ~= nil and v.tick >= s.from and v.tick <= s.leave then
                s.pressed = (s.pressed or 0) + 1
                return { row = c, symbol = P.crab[st.mode], ref = true }
            end
        end
        local C = QD.RAID_MAIDEN_REF.seat_clean[st.role]
        local W = m.ref.wave ~= nil and m.ref.waves[m.ref.wave] or nil
        if C ~= nil and W ~= nil and C.waves[math.min(m.ref.wave, 3)] and v.tick >= W.tick + C.from and v.tick <= W.tick + C.till then
            local best, bg = nil, nil
            for _, c in ipairs(v.crabs) do
                local a = st.ev.adds[c.slot]
                local side = (C.side == "north" and c.z > v.boss.z + 5) or (C.side == "south" and c.z < v.boss.z)
                local g = gap_to_her(c.x, c.z)
                local limit = C.gap
                if m.ref.wave >= 3 then limit = math.min(limit, QD.RAID_MAIDEN_REF.clean_gap_30) end
                if a ~= nil and a.frozen and side and g > 1 and g <= limit and (best == nil or g < bg) then best, bg = c, g end
            end
            if best ~= nil then
                m.ref.cleans = (m.ref.cleans or 0) + 1
                return { row = best, symbol = P.crab[st.mode], ref = true }
            end
        end
        return nil
    end
    m.wave_first = m.wave_first or {}
    if #v.crabs > 0 and m.wave_first[wave] == nil then m.wave_first[wave] = v.tick end
    local left = nil
    if #v.crabs > 0 then
        local list = {}
        for _, c in ipairs(v.crabs) do
            list[#list + 1] = { slot = c.slot, x = c.x, z = c.z, frozen = frozen(c), lead = (m.crab_track[c.slot] ~= nil and m.crab_track[c.slot].first == v.tick) and 0 or 1 }
        end
        -- the cadence from the last freeze SEEN this wave (a crab's first
        -- still view, two ticks after its cast), else from the wave's first
        -- tick; the freezer's own reach from its home tile
        local base, rdelay = m.wave_first[wave], nil
        if m.ice_seen ~= nil and m.ice_seen >= base then base, rdelay = m.ice_seen, 0 end
        local since = v.tick - base
        local d0 = (P.cast_every - since % P.cast_every) % P.cast_every + P.ice_lead
        local fh = P.roles[2] and P.roles[2].home
        local fme = fh and { x = fh[1] + (m.ox or 0), z = fh[2] + (m.oz or 0) } or nil
        local plan = QD.raid._play_maiden_ice_plan(st, v, list, d0, fme, rdelay)
        if plan ~= nil then left = plan.left end
    end
    local function rank(c)
        local tr = m.crab_track[c.slot]
        local gp = gap_to_her(c.x, c.z)
        -- raid seam40: a scythe seat takes ANY walker that comes into its
        -- reach, nearest her first (m40d w2: the freezer's plan "meant" to
        -- freeze the three north walkers, so the seats beside her skipped
        -- them and all three reached her corner at full health, 75 each)
        -- and only the NORTH walkers (the seats camp her north-east corner,
        -- where the north lanes arrive; the freezer takes the south first,
        -- 10Boot 0:07:40), or one already beside the seat (m40e w2: both
        -- seats chased the south pair and the north pair walked in at 75)
        if R.melee then
            -- N1 first, both seats ("if a crab spawns at the closest north
            -- side tile, this crab should not get frozen.  Everyone else in
            -- the raid should just try and kill it", 10Boot 0:08:48): it
            -- walks onto the seats' corner in six ticks (svaplaymaide,
            -- svbplaymaide: N1 absorbed at 75 in every 70 percent wave, heal
            -- 150 against the reference's phase.70.boss_heal 1 [0-226])
            if not frozen(c) and n1(c) then return -1, gp end
            -- (every north walker, not only those the freezer's plan leaves:
            -- svaplaymaide w1, the seats waited for N3's gap to reach 4 and
            -- it walked in unhit two ticks after their press)
            if not frozen(c) and not south(c) then return 0, gp end
            return nil
        end
        if not frozen(c) and (left == nil or left[c.slot]) then return 0, gp end
        if not frozen(c) and gp <= 2 then return 1, gp end
        if not frozen(c) then return nil end
        return 2, (tr and tr.moved or v.tick)
    end
    -- raid seam33: a pipe ranger shoots what its pipe reaches from where it
    -- stands, plus two tiles of step (the press paths into reach 5,
    -- wiki_Toxic_blowpipe.wikitext:78): s33m2 t237-246, the tank chased a
    -- south crab onto 6437,90 and died there to a pool and a storm in one tick
    local pick_reach = (R.pipe and P.pipe ~= nil) and (P.pipe.reach + 2) or 10
    -- raid seam40: a scythe seat swings at a WALKING nylocas only as it
    -- reaches her beside it (the reference's dps: attacks_add 1-3 a phase,
    -- 0-1 in the last), never a frozen one across the room
    if R.melee then pick_reach = P.melee_add_reach end
    local pick, pk, pv = nil, nil, nil
    for _, c in ipairs(v.crabs) do
        -- a FROZEN one anywhere in the bow's ten (it cannot walk to her
        -- while the ice holds; svdplaymaide w2, S2 frozen on its spawn tile
        -- at gap 9 outside the pipe's reach thawed after 68 ticks and walked
        -- in): the press walks the ranger into reach once nothing walks
        -- (a scythe seat measures from her edge, not from its own tile: m40g
        -- w1, dps1 dodged to her north-west end and never saw the north pair)
        local within = cheb(c.x, c.z, me.x, me.z) <= ((frozen(c) and 10) or pick_reach)
        if R.melee then within = not frozen(c) and gap_to_her(c.x, c.z) <= pick_reach end
        if within then
            local k, val = rank(c)
            if k ~= nil and (pick == nil or k < pk or (k == pk and val < pv)) then pick, pk, pv = c, k, val end
        end
    end
    -- raid seam33: the walker already being shot stays the target while it
    -- walks in reach (both rangers on one crab until it drops: svdplaymaide
    -- w2, the darts split over N1, N3 and N4 as their gaps crossed, and N3
    -- reached her with 21 of its 75 left)
    -- (a scythe seat too, on the north walker it is on: svaplaymaide w2, the
    -- pick flipped between N1 and her and the swing at t165 went on her)
    if m.add_slot ~= nil and (pick == nil or m.add_slot ~= pick.slot) and (pick ~= nil or R.melee) then
        for _, c in ipairs(v.crabs) do
            if c.slot == m.add_slot and not frozen(c) and (rank(c) ~= nil or (R.melee and not south(c))) and (R.melee or cheb(c.x, c.z, me.x, me.z) <= pick_reach) then
                pick = c
                m.sticky = (m.sticky or 0) + 1
            end
        end
    end
    if pick ~= nil then
        return { row = pick, symbol = P.crab[st.mode] }
    end
    if #v.crabs == 0 and R.slugs and #v.slugs > 0 then
        local s = v.slugs[1]
        for _, c in ipairs(v.slugs) do
            if cheb(c.x, c.z, me.x, me.z) < cheb(s.x, s.z, me.x, me.z) then s = c end
        end
        if cheb(s.x, s.z, me.x, me.z) <= 10 then return { row = s, symbol = P.slug[st.mode] } end
    end
    return nil
end

-- raid seam33 THE OPENER (P.opener; PLAY_NOTES.md "Maiden, Normal trio").
-- Every seat, on her first ticks: the hammer goes on in one block, the
-- special is armed from the orb with the attack press the next tick (the
-- press paths the raider to her side), the swing is SEEN as the energy it
-- spends (varp300, the orb's own number), and its splat is read off her
-- health bar's newest hitsplat one tick on (what a person sees).  A 0 drains
-- nothing (wiki_Dragon_warhammer.wikitext:59 "on successful hit") and is
-- swung again while the energy lasts and `tries` allows; any other splat, or
-- none in three ticks, ends it: the bow goes back on in one block ("then
-- switch to range gear", 10Boot 0:06:33).  The library's run-by (Bloat) is
-- the same shape.  Returns true while the opener owns the tick.
function QD.raid._play_maiden_opener(st, v, m, intent)
    local O = st.plan.opener
    local ob = m.opener
    if ob == nil then
        ob = { stage = "wait", splats = {}, swings = 0 }
        m.opener = ob
    end
    if ob.stage == "done" or ob.stage == "gave_up" then
        return false
    end
    local _, energy = QD.var.varp("varp300_sa_energy")
    energy = tonumber(energy) or 0
    -- raid seam40 THE FIRST SHOT: every real raider's first attack is the
    -- twisted bow on the run in (sources/blert_api/reference/maiden_normal_3
    -- .json: TWISTED_BOW in dps1|100 19 of 24 rooms, dps2|100 22, one attack;
    -- the cached streams put it at tick 5-6 from eight tiles east of her, the
    -- second attack (the special) five ticks later, the scythe after).  The
    -- press paths the raider into the bow's reach and it shoots there: the
    -- shot is the first tick it stands still in reach, and the hammer goes on
    -- the next tick (its press walks in; the swing waits out the bow's 5).
    local R = m.R
    if ob.stage == "wait" and R ~= nil and R.melee and ob.bow == nil then
        ob.bow = { press = v.tick }
        intent.attack = true
        st.engaged = false
        return true
    end
    if ob.stage == "wait" and ob.bow ~= nil and ob.bow.shot == nil then
        local b = v.boss
        local gx = b and math.max(b.x - v.me.x, 0, v.me.x - (b.x + 5)) or 99
        local gz = b and math.max(b.z - v.me.z, 0, v.me.z - (b.z + 5)) or 99
        local still = st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
        if (still and math.max(gx, gz) <= 10 and v.tick - ob.bow.press >= 2) or v.tick - ob.bow.press >= 12 then
            ob.bow.shot = v.tick
        else
            intent.attack = not st.engaged
            return true
        end
    end
    if ob.stage == "wait" then
        if energy < O.cost then
            ob.stage = "gave_up"
            ob.why = "energy " .. energy
            return false
        end
        ob.stage = "equip"
        ob.equip_tick = v.tick
        ob.energy0 = energy
        intent.gear = { O.weapon }
        intent.want.piety = true
        intent.want.rigour = nil
        return true
    end
    intent.want.piety = true
    intent.want.rigour = nil
    if ob.stage == "equip" then
        ob.stage = "swing"
        ob.arm_tick = v.tick
        ob.energy0 = energy
        intent.spec = true
        intent.attack = true
        st.engaged = false
        return true
    end
    if ob.stage == "fired" then
        local b = v.boss
        local seen = b ~= nil and b.hit_cycle ~= nil and ob.cycle_at_fire ~= nil and b.hit_cycle > ob.cycle_at_fire
        if seen then
            ob.splat = b.hit_damage
            ob.splats[#ob.splats + 1] = tostring(b.hit_damage) .. "@t" .. v.tick
        end
        if seen and ob.splat == 0 and energy >= O.cost and ob.swings < O.tries then
            ob.stage = "swing"
            ob.arm_tick = v.tick
            ob.energy0 = energy
            intent.spec = true
            intent.attack = true
            st.engaged = false
            return true
        end
        -- raid seam40: with no second swing allowed the splat is not waited
        -- for: the scythe goes on the tick after the energy is spent (survey4:
        -- the hammer at room tick 11, the scythe's first swing at 19 where its
        -- six-tick delay allows 17; the reference's scythes start at 16)
        if seen or v.tick - ob.fired >= 3 or ob.swings >= O.tries then
            ob.stage = "done"
            ob.done_tick = v.tick
            intent.gear = { (m.R ~= nil and m.R.weapon) or st.plan.bow }
            intent.want.piety = nil
            st.engaged = false
            return false
        end
        return true
    end
    -- "swing": spent when the orb's energy falls by the cost
    if energy <= ob.energy0 - O.cost then
        ob.stage = "fired"
        ob.fired = v.tick
        ob.swings = ob.swings + 1
        ob.cycle_at_fire = v.boss ~= nil and v.boss.hit_cycle or nil
        return true
    end
    if v.tick - ob.arm_tick > O.give_up then
        ob.stage = "gave_up"
        ob.why = "no energy spent in " .. (v.tick - ob.arm_tick) .. " ticks"
        intent.gear = { (m.R ~= nil and m.R.weapon) or st.plan.bow }
        intent.want.piety = nil
        st.engaged = false
        return false
    end
    local _, armed = QD.var.varp("varp301_sa_attack")
    if tonumber(armed) == 0 and v.tick - ob.arm_tick >= 2 and (ob.rearm or 0) < 3 then
        ob.rearm = (ob.rearm or 0) + 1
        intent.spec = true
    end
    intent.attack = true
    return true
end

-- raid seam33: the blackstorm a person expects, from the Matomenos seen to
-- reach her: "36.5 + (3.5 * c) ... halved by Protect from Magic" (W:590).
-- c is counted off the screen (a nylocas that left the pool at her side
-- with health on its bar: _play_maiden_party_wave's leak count), so a tank
-- stepping in late knows the storm the old tank took without reading it
-- off its own hitpoints (the seam32 storm_seen was the raider's own loss).
function QD.raid._play_maiden_storm(c, prayed)
    local raw = math.floor(36.5 + 3.5 * c)
    if prayed then return math.floor(raw / 2) end
    return raw
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
    -- raid seam33: Rigour while the bow or the pipe is in hand (down_prayers)
    if st.party > 1 and (m.opener == nil or m.opener.stage == "done" or m.opener.stage == "gave_up") and m.fz ~= "magic" then
        -- raid seam40: Piety for a scythe seat (the reference's dps are
        -- melee; Blert prayerSet bit 26 piety, sources/blert_api/README.md)
        if (m.R ~= nil and m.R.melee) or m.fz == "melee" then intent.want.piety = true else intent.want.rigour = true end
    end
    local b = v.boss
    if b == nil then
        return intent
    end
    -- raid seam32: the party's role and its geometry.  A party of one keeps
    -- the Entry plan's absolute tiles and its own freeze (role "solo"); a
    -- party re-bases every tile on her own tile, read once.
    if m.role == nil then
        local R = st.party > 1 and P.roles[st.role] or nil
        m.role = R and R.name or "solo"
        m.R = R
        -- raid seam35e play_tob_entry_relay: the solo plan re-bases too.  Its
        -- tiles were written in the room test's instance (::tobmode lands
        -- Maiden at 6426,92); the Theatre entered by its door builds her in
        -- the next free instance (6426,156 on the relay's first run, where
        -- the absolute floor put every dodge tile 64 rows away and the
        -- raider stood in her blood until it killed him at t158).  In the
        -- room test her tile IS P.body, so the offset there is 0 and the
        -- kept rooms play exactly as before.
        m.ox = b.x - P.body[1]
        m.oz = b.z - P.body[2]
        m.body_seen = { x = b.x, z = b.z, tick = v.tick }
        m.crab_track, m.crab_cast, m.crab_frozen, m.ice_on = {}, {}, {}, {}
    end
    local R = m.R
    local ox, oz = m.ox, m.oz
    -- raid seam32: the leader's OWN pid in its tick log.  api_drive.players'
    -- `me` row (the library's st.my_pid) counts from 1 and the log's pid from
    -- 0 (s32mzn1: players() named the leader pid 1 and the third raider pid
    -- 3, the log's player_tile rows put them on the same tiles as pid 0 and
    -- 2), so in a party the library read the freezer's swings as the
    -- tank's.  The plan re-reads it once: the one log pid standing on this
    -- raider's own tile, alone, at the newest tile tick.
    if R ~= nil and st.log and not m.pid_fixed then
        local tr, rows = QD.ticklog.rows({ kind = "player_tile", since = m.tile_serial or 0 })
        if tr == "ok" and type(rows) == "table" and #rows > 0 then
            local newest = rows[#rows].tick
            local mine, others = nil, 0
            for _, row in ipairs(rows) do
                m.tile_serial = math.max(m.tile_serial or 0, row.serial or 0)
                if row.tick == newest then
                    if row.x == me.x and row.z == me.z then
                        if mine == nil then mine = row.pid else others = others + 1 end
                    end
                end
            end
            if mine ~= nil and others == 0 then
                m.pid_was = st.my_pid
                st.my_pid = mine
                m.pid_fixed = true
            end
        end
    end
    local home = R and { R.home[1] + ox, R.home[2] + oz } or { P.home[1] + ox, P.home[2] + oz }
    local reach_x = P.reach_x + ox
    local freezer = R == nil or R.freezer
    local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
    local function floor_ok(x, z)
        -- raid seam40: a scythe seat also stands on the two rows north of
        -- her (the reference's dps1 at (3..5,6) from her SW tile)
        if R ~= nil and R.melee and z >= b.z + 6 and z <= b.z + 7 and x >= b.x and x <= b.x + 6 then return true end
        return x >= P.floor[1] + ox and x <= P.floor[3] + ox and z >= P.floor[2] + oz and z <= P.floor[4] + oz
    end
    -- raid seam40: a tile sharing an edge with her 6x6 (the scythe's reach,
    -- raid_play.lua _play_reach), and the seat's own side of her corner
    local function her_edge(x, z)
        local along_x = x >= b.x and x <= b.x + 5
        local along_z = z >= b.z and z <= b.z + 5
        return (along_x and (z == b.z - 1 or z == b.z + 6)) or (along_z and (x == b.x - 1 or x == b.x + 6))
    end
    local function own_side(x, z)
        if R == nil or not R.melee then return false end
        if R.side == "north" then return z == b.z + 6 and x >= b.x and x <= b.x + 5 end
        return x == b.x + 6 and z >= b.z and z <= b.z + 5
    end
    -- the gap between a 1x1 npc's tile and her 6x6 footprint (maiden.crab_arrive_gap)
    local function gap_to_her(x, z)
        local gx = math.max(b.x - x, 0, x - (b.x + 5))
        local gz = math.max(b.z - z, 0, z - (b.z + 5))
        return math.max(gx, gz)
    end
    -- THE TANK SWAP (raid seam32): "The person closest to the boss becomes
    -- the tank and will take the most damage.  You can swap out when someone
    -- gets low ... swap this between your range and melee roles" (10Boot,
    -- transcripts/yt_4i4lv-srJkw.md 0:06:33); "having another teammate step
    -- in when they are low on health" (W:589).  Seat 1 tanks; when its food
    -- is down to `tank_out` it steps back to the north ranger's corner, and
    -- the north ranger (seat 3), seeing seat 1 off the tank tile for three
    -- ticks (its own client's player rows), steps onto the tank tile.  The
    -- freezer never does (s32mzn3-5: the tank's 20 anglerfish lasted about
    -- 300 ticks of a 600-tick room, and every death came after its last).
    if R ~= nil and not R.freezer and not R.melee then
        if m.tank == nil then m.tank = (st.role == 1) end
        local _, food_left = QD.inv.count("anglerfish")
        food_left = tonumber(food_left) or 0
        -- raid seam33: the kit is brews over anglerfish now, so a brew dose
        -- counts as a bite; and the old tank steps out on the tick after her
        -- launch is seen (nine ticks to the next scan: s33m1 t406, a step-out
        -- mid-cycle left the freezer the closest at her t411 launch)
        for k, name in ipairs(QD.RAID_PLAY_BREWS) do
            local cr, n = QD.inv.count(name)
            if cr == "ok" then food_left = food_left + k * (tonumber(n) or 0) end
        end
        local just_launched = m.last_attack ~= nil and v.tick - m.last_attack <= 1
        if m.tank and food_left <= P.tank_out and m.tank_since ~= nil and m.out_tick == nil and just_launched then
            m.tank, m.out_tick = false, v.tick
            m.swap_walk = true
        elseif not m.tank and m.out_tick == nil and st.role == 3 then
            local pr, prow = api_drive.players()
            -- seat 1 standing in the reserve corner (it walked there to step
            -- out; a dodge never goes there: s32mzn6, a "gap > 3" reading
            -- took the tank's three-tile dodge for a step-out) or gone
            local rx, rz = P.roles[1].reserve[1] + ox, P.roles[1].reserve[2] + oz
            local out_seen, present = false, false
            if pr == "ok" then
                for _, row in ipairs(prow) do
                    if not row.me and QD.party._same(row.name, QD.party.name(1)) then
                        present = true
                        if cheb(row.x, row.z, rx, rz) <= 1 then out_seen = true end
                    end
                end
            end
            if (out_seen or not present) and v.tick - st.start_tick >= 30 then m.tank_gone = (m.tank_gone or 0) + 1 else m.tank_gone = 0 end
            if m.tank_gone >= 1 and food_left > P.tank_out then
                m.tank = true
                m.tank_in = v.tick
                m.swap_walk = true
            end
        end
        if m.tank and m.tank_since == nil then m.tank_since = v.tick end
        local spot = nil
        if m.tank then
            spot = P.roles[1].home
        elseif m.out_tick ~= nil then
            spot = P.roles[1].reserve
        end
        if spot ~= nil then home = { spot[1] + ox, spot[2] + oz } end
        if m.swap_walk and cheb(me.x, me.z, home[1], home[2]) <= 1 then m.swap_walk = false end
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
                if cheb(x, z, fx, fz) == d and floor_ok(x, z) and x <= reach_x and not v.marks[x * 100000 + z]
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
                        local score = cheb(x, z, home[1], home[2]) * 10 + crowd * 4
                        -- raid seam40: a scythe seat dodges along her edge
                        -- (the reference's dps keep dist_boss 1: react step 1)
                        if R ~= nil and R.melee then
                            if not her_edge(x, z) then score = score + 200 end
                            if not own_side(x, z) then score = score + 20 end
                        end
                        if best == nil or score < best then best, bx, bz = score, x, z end
                    end
                end
            end
        end
        return bx, bz
    end

    -- raid seam33 THE OPENER (P.opener): the hammer specials own the first
    -- ticks; the prayer and the supplies still run (the library's SEND).
    if R ~= nil and R.melee and P.opener ~= nil and QD.raid._play_maiden_opener(st, v, m, intent) then
        local base = m.last_attack or (st.start_tick - 1)
        intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, function(h)
            local total = 0
            for k = 0, 2 do
                local launch = base + k * P.attack_every
                if launch - 1 > v.tick and launch - 1 <= v.tick + h then total = total + N.storm + 1 end
            end
            return total
        end)
        return intent
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
    -- raid seam55: a trigger's step (blood_thrown / pool_landed on this
    -- tile) is the move; the plan's own dodge and pool walks stand down
    -- until it has landed (m.trig_until)
    if v.trigger ~= nil and v.trigger.walk ~= nil then
        m.move = { x = v.trigger.walk.x, z = v.trigger.walk.z, why = "trigger" }
        m.still = 0
    end
    if m.move == nil and not (m.trig_until ~= nil and v.tick <= m.trig_until) then
        local next_attack = m.last_attack ~= nil and (m.last_attack + P.attack_every) or nil
        -- raid seam32: the freezer holds its tile for the barrage while a
        -- nylocas still walks (s32mzn7: a dodge put nine ticks between two
        -- casts and 1085 walked in unfrozen); it still steps off a pool that
        -- has landed under it (the branch after this one)
        local holding = R ~= nil and R.freezer and m.fz == "magic" and #v.crabs > 0
        if near_blood(me.x, me.z, P.scatter) and not holding then
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
            local sx, sz = QD.raid._play_hazard(st, v, me.x, me.z, function(x, z) return floor_ok(x, z) and x <= reach_x end)
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
        elseif R ~= nil and R.melee and st.role == 1 and next_attack ~= nil and v.tick == next_attack - 1 and m.share_seen ~= next_attack
            and own_side(me.x, me.z) and floor_ok(me.x, me.z + 1) and not v.marks[me.x * 100000 + me.z + 1] and not near_blood(me.x, me.z + 1, P.scatter) then
            -- raid seam40 THE SHARED TANK.  Her blackstorm takes "the closest
            -- player to her centre by Chebyshev distance ... a tie, higher orb
            -- order" (tob_maiden.rs2 ~tob_maiden_blackstorm, [mc]), and every
            -- tile on her north and east faces is 3 from that centre, so with
            -- both scythe seats on her the leader took every storm (m40a-k:
            -- 19-26 of 19-26, two deaths).  The reference's dps split them
            -- (role.dps1.boss_targeted_pct 38 [5.6-58.8], dps2 41.45 [18.8-
            -- 64.3]): on every other attack the leader steps one tile north
            -- (4 from her centre) on the tick before her scan (the T-1 rule,
            -- ENCOUNTER_TIMING 1.1) and the second seat takes it; the walk home
            -- brings it back after the launch.
            m.share_seen = next_attack
            m.share_flip = not m.share_flip
            if m.share_flip then
                new_walk = { x = me.x, z = me.z + 1, why = "share" }
                m.shares = (m.shares or 0) + 1
            end
        elseif R ~= nil and not R.melee and m.fz ~= "melee" and ((R.freezer and m.fz ~= "magic") or #v.crabs == 0 or m.swap_walk) and cheb(me.x, me.z, home[1], home[2]) > 1
            and not v.marks[home[1] * 100000 + home[2]] and not near_blood(home[1], home[2], P.scatter) then
            -- raid seam32: back to the role's tile when nothing is landing
            -- (10Boot 0:06:33 "The person closest to the boss becomes the
            -- tank ... You don't want your mage to be closest at any time";
            -- the primed freezer too, before the wave: svaplaymaide's freezer
            -- dodged away in its magic set, never walked back, and cast
            -- nothing at the 50% wave: all six reached her);
            -- 0:08:48 the others camp north), every tick-end of the walk off
            -- the blood (library _play_safe_step)
            local hx, hz = QD.raid._play_safe_step(st, v, home[1], home[2], function(x, z) return floor_ok(x, z) and x <= reach_x end)
            if hx ~= me.x or hz ~= me.z then new_walk = { x = hx, z = hz, why = "home" } end
        elseif R ~= nil and R.melee and (not own_side(me.x, me.z) or cheb(me.x, me.z, home[1], home[2]) > 2) and m.add_slot == nil
            and not (m.share_seen ~= nil and v.tick < m.share_seen)
            and not v.marks[home[1] * 100000 + home[2]] and not near_blood(home[1], home[2], P.scatter) then
            -- raid seam40: a scythe seat off its side of her corner (the
            -- opener's press paths it to the nearest edge tile, a crab took
            -- it away) walks back to its tile while no wave is up
            local hx, hz = QD.raid._play_safe_step(st, v, home[1], home[2], floor_ok)
            if hx ~= me.x or hz ~= me.z then new_walk = { x = hx, z = hz, why = "home" } end
        end
        if new_walk ~= nil then
            m.move = new_walk
            m.still = 0
            if new_walk.why ~= "home" then
                m.dodges = m.dodges + 1
                st.dodges = st.dodges + 1
            else
                m.home_walks = (m.home_walks or 0) + 1
            end
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
    if R ~= nil then
        add = QD.raid._play_maiden_party_wave(st, v, m, R, mg, moving, gap_to_her, cheb)
        -- The rangers stay on HER through the 30 percent wave (Blert, 26 trio
        -- rooms: 0 crabs killed at 30 percent, she dies about 12 ticks after
        -- that spawn); the freezer still barrages.
        -- raid seam54 play_tob_maiden_whole: one swing at the walker that is
        -- arriving BESIDE the seat.  A Matomenos that reaches her heals her
        -- by twice its current hitpoints (W:593; tob_maiden.rs2 ~1411), and
        -- ours arrived whole: seam54 survey1, 2-5 leaks at 30 percent healed
        -- her 300-478 (reference outcome.phase.30.boss_heal 49 [0-762]) and
        -- the phase ran 76-114 ticks against 48 [34-75].  The reference's dps
        -- spend about one attack a seat on the crabs in that phase
        -- (role.dps1.phase.30.attacks_add 1 [0-8], dps2 1 [0-3]) and stay on
        -- her edge (dist_boss 1): so the seat keeps an add only when it is a
        -- walker two tiles or less from her and from the seat, never a walk
        -- across the room.
        if add ~= nil and not add.ref and not R.freezer and st.boss_symbol ~= nil and st.boss_symbol:find("_30", 1, true) ~= nil then
            local c = add.row
            if c == nil or c.x == nil or gap_to_her(c.x, c.z) > 2 or cheb(c.x, c.z, me.x, me.z) > 2 then
                add = nil
            end
        end
        -- raid seam33 THE HALT: a weapon swap does not end an attack, so the
        -- freezer that put the wand on while its bow was on her walked to her
        -- side and swung the wand there (svdplaymaide t168-202: 6441,94 to
        -- 6432,96, the closest raider, out of Ice Barrage's ten tiles of the
        -- north spawns, and her storms on it).  A person clicks a tile: one
        -- step east, off her, in the swap's tick.
        if R.freezer and m.halt == v.tick and intent.walk == nil and not moving then
            local hx, hz = me.x + 1, me.z
            if not floor_ok(hx, hz) or v.marks[hx * 100000 + hz] then hx, hz = me.x, me.z + 1 end
            if not floor_ok(hx, hz) or v.marks[hx * 100000 + hz] then hx, hz = me.x, me.z - 1 end
            intent.walk = { x = hx, z = hz }
            m.move = { x = hx, z = hz, why = "halt" }
            m.halts = (m.halts or 0) + 1
            moving = true
        end
    elseif m.fz == nil and #v.crabs > 0 and m.flick == nil then
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
            -- raid seam40: the scythe instead of the bow once the solo is on her
            local back = P.ranged_set
            if m.solo_melee ~= nil then
                back = { P.solo_melee.item }
                for _, item in ipairs(P.ranged_set) do
                    if item ~= P.bow then back[#back + 1] = item end
                end
            end
            QD.raid._play_maiden_block(st, v, "ranged set", back)
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
    if R ~= nil then
        -- (the party's add was chosen above)
    elseif m.fz == "ranged" and #v.crabs > 0 then
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

    -- raid seam40 THE SOLO ON HER: the reference's one Entry solo room
    -- (reference/maiden_entry_1.json, Blert 6ae3d9f8, 82 ticks) is played
    -- with the scythe from her side (role.solo.melee_pct 100, dist_boss 1;
    -- SCYTHE in every phase).  The kept room's technique rows are made with
    -- the bow first (the unprotected storm, the presteps, the flicks: this
    -- plan's `prove_protect`, `presteps`, `flicks`); once all three are
    -- measured and no wave is up the scythe goes on in one block and the
    -- library's cadence row follows it.
    if R == nil and P.solo_melee ~= nil and N.solo_melee and m.solo_melee == nil and m.fz == nil and m.flick == nil and not moving then
        local proofs = (not N.prove_protect or (m.first_storm ~= nil and v.tick >= m.first_storm + P.storm_impact + 1))
            and (m.presteps >= N.presteps or m.prestep_seen) and (#m.flicks >= N.flicks or m.drain_seen)
        if proofs then
            local hr, has = QD.inv.has(P.solo_melee.item)
            if hr == "ok" and has then
                QD.raid._play_maiden_block(st, v, "scythe (proofs done)", { P.solo_melee.item })
                m.solo_melee = v.tick
                st.weapon = QD.RAID_PLAY_WEAPONS[P.solo_melee.item]
                st.engaged = false
            end
        end
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

    -- raid seam33 THE PIPE: a ranger's blowpipe goes on in one block while
    -- Matomenos are up ("Everyone else should machine gun down the crabs that
    -- aren't in the clump with their blowpipe", 10Boot 0:08:14) and the bow
    -- comes back when none is left ("DPS roles should kill the stray nylocas
    -- before getting back on Maiden", W:639).
    if R ~= nil and R.pipe and P.pipe ~= nil and not R.melee then
        -- The scythe when the kit holds one (the real trios' crab weapon),
        -- else the pipe.
        if m.crab_item == nil then
            local has_result, has = false, false
            if P.crab_weapon ~= nil then has_result, has = QD.inv.has(P.crab_weapon.item) end
            m.crab_item = (has_result == "ok" and has) and P.crab_weapon.item or P.pipe.item
        end
        local crab_item = m.crab_item
        -- At the 30 percent wave the real trios IGNORE the crabs and kill her:
        -- Blert's 26 rooms show a median of 0 crabs killed at 30 percent and
        -- her death about 12 ticks after that spawn (sources/blert_api/
        -- maiden_trio_crabs/README.md). So the crab weapon goes on only for
        -- the first two waves; the last wave is spent on her.
        local last_wave = (st.boss_symbol ~= nil and st.boss_symbol:find("_30", 1, true) ~= nil)
        if #v.crabs > 0 and m.gear ~= "pipe" and not last_wave then
            QD.raid._play_maiden_block(st, v, "pipe", { crab_item })
            m.gear = "pipe"
            m.pipe_swaps = (m.pipe_swaps or 0) + 1
            st.engaged = false
            m.add_slot = nil
        elseif (#v.crabs == 0 or last_wave) and m.gear == "pipe" then
            QD.raid._play_maiden_block(st, v, "bow", { P.bow })
            m.gear = nil
            st.engaged = false
        end
    end

    -- ATTACK on cooldown (library skill): her, or the add the plan named;
    -- never mid-walk, with the whip on, or in the magic set.
    local may_attack = not moving and m.flick == nil and m.fz ~= "magic"
    if may_attack and add ~= nil then
        if m.add_slot ~= add.row.slot or v.tick - m.add_press >= st.weapon.speed + 2 then
            local ar
            if R ~= nil then
                -- raid seam33 (e): the library's press and its TRUE answer.
                -- QD.player.attack's `timeout` is "pressed, no hit inside the
                -- one-tick settle" (combat.lua; raid_play.lua _play_press): a
                -- dart or an arrow cannot land inside one tick, so seam32's
                -- 'timeout' presses were presses that landed.
                ar = QD.raid._play_press(st, v, { symbol = add.symbol, slot = add.row.slot, op = 2 })
                m.add_answers = m.add_answers or {}
                m.add_answers[tostring(ar)] = (m.add_answers[tostring(ar)] or 0) + 1
            else
                ar = QD.player.attack(add.symbol, 2, 1, { quick = true, slot = add.row.slot })
            end
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
    -- raid seam32 (Normal, `overhit`): "The attack always lands as a
    -- successful hit ... and cannot be tick-eaten" (W:590): the hit is settled
    -- against the health the target has when she LAUNCHES it (her attack tick;
    -- the room's own reading, tob_maiden.rs2 ~tob_maiden_blackstorm "the
    -- verdict is settled HERE, on the launch tick"), so a launch inside the
    -- horizon needs one more than the whole hit, and food eaten in the flight
    -- cannot undo it (s32mzn1: the tank at 12 hp on her attack tick t227 ate
    -- at t229 and died to the hit at t232).  The hit is the larger of the
    -- mode's figure and the largest one-tick loss this raider has read off its
    -- own hitpoints, plus two (a leak adds 3.5 before the prayer's halving).
    local function threat(h)
        local total = 0
        local storm = prayed and N.storm or N.storm_raw
        if N.overhit then storm = math.max(storm, (m.storm_seen or 0) + 2) end
        -- raid seam33 (c): the storm the leaks a person counted make (W:590)
        if R ~= nil then storm = math.max(storm, QD.raid._play_maiden_storm(m.leaks or 0, prayed) + 2) end
        local base = m.last_attack or (st.start_tick - 1)
        for k = 0, 8 do
            local launch = base + k * P.attack_every
            local impact = launch + P.storm_impact
            -- the launch is judged one tick early: the client reads her
            -- animation a tick after the server's npc phase started it, and a
            -- bite sent on T-1 is eaten in T's player phase, after her scan
            -- (s32mzn4: the tank at 30 hp sent nothing before her t347 launch
            -- and died to the 30 at t352)
            -- raid seam33: a party raider looks six ticks ahead for a launch
            -- (two potion doses, QD.RAID_PLAY_DRINK_DELAY 3 apart): the
            -- library's two-tick horizon between swings let svdplaymaide's
            -- tank, out of anglerfish at 8 hitpoints, see her t365 launch only
            -- at t363, one brew short of the 28 she settled at the launch
            local reach_h = (R ~= nil) and math.max(h, 6) or h
            if N.overhit and launch - 1 > v.tick and launch - 1 <= v.tick + reach_h then
                total = total + storm + 1
            elseif impact > v.tick and impact <= v.tick + h then
                total = total + storm
            end
        end
        -- raid seam35m: the pool a person counts too, 10 + 2c (W:597; svaplaymaide's
        -- tank, eleven leaks in, ate nothing at 41 and took 32 + 32 on her face)
        local pool = N.pool
        if R ~= nil then pool = math.max(pool, 10 + 2 * (m.leaks or 0) + 2) end
        if threatened then total = total + 2 * pool end
        return total
    end
    if N.overhit and st.hp_at[v.tick - 1] ~= nil and st.hp_at[v.tick - 1] - v.hp > (m.storm_seen or 0) then
        m.storm_seen = st.hp_at[v.tick - 1] - v.hp
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    -- raid seam33 (d): THE BREW FIRST for a ranger.  The kit is brews over
    -- anglerfish ("eight brews, four restores, and three anglers", 10Boot
    -- 0:04:23), and a potion adds no attack delay where a bite does ("Potions
    -- do not incur the standard 3 tick attack or eat delay",
    -- consume_shared.rs2:49): a bite the library chose that one brew dose
    -- covers is a dose instead.  Never the freezer: a brew lowers Magic, and
    -- Ice Barrage needs 94 (10Boot 0:07:06).
    if R ~= nil and not R.freezer and not R.melee and intent.eat ~= nil and intent.drink == nil
        and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY and v.hp + QD.RAID_PLAY_BREW_HEAL > (intent.need or 0) then
        for _, name in ipairs(QD.RAID_PLAY_BREWS) do
            local cr, n = QD.inv.count(name)
            if intent.drink == nil and cr == "ok" and n > 0 then intent.drink = name end
        end
        if intent.drink ~= nil then
            intent.eat = nil
            m.brew_first = (m.brew_first or 0) + 1
        end
    end
    -- a drained Ranged (or Magic before a wave) is restored ("You should
    -- always repot ... if you ever get drained at maiden", advanced guide :111);
    -- never inside a flick, whose reading is the drain itself
    if intent.drink == nil and m.flick == nil and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY then
        local _, rg = QD.skill.read("ranged")
        -- raid seam33: a ranger on brews restores at 76 (about three doses'
        -- drain; one restore dose gives back 32, wiki Super restore), not
        -- after every dose
        local floor_lv = (R ~= nil and not R.freezer) and 76 or 88
        if R ~= nil and R.melee then
            -- raid seam40: a scythe seat's brews drain Attack and Strength
            -- (wiki Saradomin brew: -10% -2 each dose); m40b's dps1 drank 32
            -- brews and its swings fell from 45 to 27 a swing
            local _, at = QD.skill.read("attack")
            local _, sg = QD.skill.read("strength")
            -- the super combat again under 108 (a boost is set from the base
            -- level: wiki Super combat potion "+5 +15%", 118 at 99), so a
            -- brew's drain costs one sip, not the boost (the scythe's three
            -- hits on her: 36.7 a swing in phase 100, 29.3 in phase 50 after
            -- the brews, five survey names; the reference's per attack 44.5)
            local combat = nil
            for _, dose in ipairs({ "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat" }) do
                local cr, n = QD.inv.count(dose)
                if combat == nil and cr == "ok" and (tonumber(n) or 0) > 0 then combat = dose end
            end
            -- raid seam54: a reserve of `reboost_keep` doses is never drunk
            -- here (the reference's dps drink nothing in the room:
            -- role.dps1.phase.*.drinks 0 [0-0], dps2 0 [0-0]; camera seam53
            -- survey1: the re-boost drank all four of the relay's doses at
            -- Maiden and bloat.potion FAILed on seats 1 and 3)
            local doses = 0
            for k, dose in ipairs({ "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat" }) do
                local cr, n = QD.inv.count(dose)
                if cr == "ok" then doses = doses + k * (tonumber(n) or 0) end
            end
            if doses <= (P.reboost_keep or 0) then combat = nil end
            if (at.level < 108 or sg.level < 108) and combat ~= nil then
                intent.drink = combat
                m.reboosts = (m.reboosts or 0) + 1
            elseif at.level < 90 or sg.level < 90 then intent.drink = QD.raid._play_maiden_restore() end
        elseif rg.level < floor_lv then intent.drink = QD.raid._play_maiden_restore() end
    end
    -- raid seam40: a scythe seat's press paths it to her edge, and that path
    -- goes through no skill; while blood is down it walks to an unmarked edge
    -- tile first (library _play_reach, Bloat's shape)
    if ((R ~= nil and R.melee) or m.solo_melee ~= nil) and intent.attack and intent.walk == nil and not moving and add == nil then
        local rx, rz, hold = QD.raid._play_reach(st, v, floor_ok)
        if rx ~= nil then
            local sx, sz = QD.raid._play_safe_step(st, v, rx, rz, floor_ok)
            if sx ~= me.x or sz ~= me.z then
                intent.walk = { x = sx, z = sz }
                m.move = { x = sx, z = sz, why = "reach" }
                m.reach_walks = (m.reach_walks or 0) + 1
            end
            intent.attack = false
        elseif hold then
            intent.attack = false
        end
    end
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    return intent
end
