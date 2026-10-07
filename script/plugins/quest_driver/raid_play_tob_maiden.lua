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
    -- owner_tob_normal sm14: the scythe seats' opener set and their melee set.
    -- The streams' gear on every TONALZTICS_SPEC (gear2.py, 58 specs): void /
    -- masori ranged armour, necklace of rupture 51, Dizana's quiver, void or
    -- zaryte gloves -- the special is thrown in RANGED gear (an isolated one
    -- dropped her 96; ours in the melee set 11 and 20); their first three
    -- attacks: TWISTED_BOW, TONALZTICS_SPEC, SCYTHE (27 of 72 seat-rooms).
    opener_set = { "twisted_bow", "masori_mask", "masori_body", "masori_chaps", "necklace_of_anguish", "zaryte_vambraces" },
    melee_set = { "scythe_of_vitur", "torva_helm", "amulet_of_rancour", "radiant_oathplate_chest", "radiant_oathplate_legs", "ferocious_gloves" },
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
    -- owner_tob_normal M6: THE DEFENCE DRAIN, as the recorded trios open.
    -- TONALZTICS in phase 100 for the freezer in 23 of 24 rooms, dps1 16,
    -- dps2 15, one attack each (reference/maiden_normal_3.json weapons); the
    -- hammer in 4 (dps1), the maul in 8 (dps2).  W:249 "Tonalztics of ralos can
    -- be used to reduce Maiden to 0 defence in only 2 specs after a single
    -- Dragon warhammer"; its special lowers Defence by 1/8 of the target's
    -- Magic on each of its two hits (pvm_tonalztics_of_ralos_charged.rs2: her
    -- Magic 350, 43 a hit, 87 a special; her Defence 200, cache
    -- tob_maiden_100 stat2), 50 percent energy (special_attack.obj sa_energy
    -- 500).  Three specials put her at 0: the scythe's hit chance against
    -- Defence 200 is about 0.79, against 0 about 0.99 (owner phase 100 15.5
    -- hp a tick against the reference's 17.9 with one hammer).  A special
    -- that splats 0 drains nothing and is thrown again (tries 2).
    opener = { weapon = "tonalztics_of_ralos_charged", cost = 500, tries = 1, give_up = 14 },
    spec30 = { weapon = "dragon_claws", cost = 500, tries = 1, give_up = 10 },
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
    -- her footprint (all.npc [tob_maiden_100] .. [tob_maiden_30] size=6): the
    -- npc pool rows carry none, and every crab gap was read from her
    -- south-west tile alone (sm28: a crab frozen against her east edge on
    -- (6,0) read gap 6)
    boss_size = 6,
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
    thresholds = { 0.70, 0.50, 0.30 }, prime = 0.05, prime_hold = 0.035,
    -- the fewest live crabs in a 3x3 the freezer barrages for damage
    clump_min = 2,
    -- barrages a wave before the clump casts stop (walkers and thaws still go)
    wave_casts = 5,
    -- a scythe seat eats at or under this (owner M28; ref eat_at_hp_pct 36 [14-75])
    melee_eat_at = 45,
    -- the scythe seats' trips to lone frozen crabs before they thaw (M27)
    lone_trips = true,
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
    -- owner_tob_normal M2: ON again for the trigger plan, as the reference's
    -- freezer|30 (SCYTHE 19 of 24 rooms): the freezer's idle gear in her last
    -- form (the survey5 finding above was the old plan's: it stopped
    -- barraging the clump for good; the trigger freezer goes back to the
    -- magic set for any walker or any freeze P.ice_rearm old)
    -- owner_tob_normal M38: OFF -- the freezer's scythe walk to her edge in
    -- her last form wandered the trails for 47 ticks without a swing (owner
    -- M37 svaplaymaide t235-t282; her 30 form took 87 ticks at 9 a tick); its
    -- idle gear there is the bow, as in her other forms
    freezer_melee30_ref = nil,
    -- a frozen crab's ice is renewed by a barrage this many ticks after it
    -- landed (Ice Barrage holds 32 ticks, player_magic.rs2 freeze_time; the
    -- cast lands two ticks after it is sent)
    ice_rearm = 26,
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
    -- owner_tob_normal: the trio's standing intents are reconciled, not re-clicked
    -- (raid_play.lua _play_reconcile); the Entry solo's decide never sets it
    reconcile = true,
})

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
    -- a blood spawn's tile and the tiles round it: its next step lays a trail
    -- there (sm16 _play_maiden: the leader stepped off one trail tile onto the
    -- next for six ticks, 18 a tick, and died at (5,-1))
    for _, sl in ipairs(v.slugs or (m and m.slugs_last) or {}) do
        for ax = -1, 1 do
            for az = -1, 1 do marked[(sl.x + ax) * 100000 + sl.z + az] = true end
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
                        -- a scythe seat in the trio steps to a tile beside her,
                        -- two out if one out has none, so the next swing needs
                        -- no walk back (sm39 budget: the 70 wave's scythe seats
                        -- 12-15 ticks moving, 2-3 attacks on her against the
                        -- script's 5)
                        if (st.party or 1) > 1 and st.role ~= 2 and b ~= nil and QD.raid._play_gap(b, x, z) ~= 1 then score = score + 15 end
                        if best == nil or score < best then best, bx, bz = score, x, z end
                    end
                end
            end
        end
        if best ~= nil and ((st.party or 1) <= 1 or st.role == 2 or best < 20) then break end
    end
    return bx, bz
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
                if row.size == nil then row.size = P.boss_size end
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

-- ==========================================================================
-- owner_tob_normal 2026-10-06: THE MAIDEN TRIO AS STATE MACHINES.
--
-- The owner: "Are you modelling each role as a state machine?"  Each seat
-- runs one machine; the memory is the state, its index and the state a dodge
-- resumes (st.m.state, st.m.idx, st.m.resume).  On entering a state its
-- handlers are subscribed (st.on) and on leaving taken off (st.off); every
-- state names a handler for every event, and a handler returning nil stays.
-- The storm's prayer is a parallel rule subscribed once (storm_sent).
--
--   seat      state     tick: the one action           events -> state
--   all       DRAIN     Tonalztics special on her      spent / 14 ticks -> ON_BOSS
--   all       DODGE     step to the safe tile          landed -> the saved state
--   freezer   ON_BOSS   ranged set, bow on her        bar near a threshold -> PREAIM;
--                                                      crab_spawn -> CAST/1
--   freezer   PREAIM    magic set at home              crab_spawn -> CAST/1; no threshold ahead -> ON_BOSS
--   freezer   CAST/k    cast k at spawn+offset on the  last cast -> RETURN; crab_spawn -> CAST/1
--                       script's lane (else the next walker no seat takes)
--   freezer   RETURN    ranged set, home               spawn+return -> ON_BOSS; crab_spawn -> CAST/1
--   scythe    ON_BOSS   scythe on her from its tile    crab_spawn -> LANE/1; boss_phase(30) -> CLAWS
--   scythe    LANE/k    the seat's k-th lane crab      crab_gone (that lane) / window over -> LANE/k+1;
--                       inside its window              list done -> ON_BOSS
--   scythe    CLAWS     claws special on her           spent / 10 ticks -> ON_BOSS
--   DODGE               step to the safe tile          on it / landed -> the saved state; crab_spawn
--                                                      and boss_phase rewrite the saved state
--   any       (blood_thrown / pool_landed on my tile) -> DODGE, resume the same state and index
--
-- THE SCRIPT (maiden_normal_3.script.json, the 24 death-free Normal trio
-- rooms of reference/maiden_normal_3.json).  Per wave (1 = her 70 form, 2 =
-- 50, 3 = 30): the freezer's casts as {offset from the spawn, lane} -- its
-- "casts" medians 1/6/11/16 and "targets" in order (70: S1 N2 S3 S4out, 50: S1
-- S2 S3 N4out, 30: S1 N2 S3 N4out) -- and its return_to_boss (21 / 26 / 21);
-- each scythe seat's lanes as {lane, press from, leave}: the script's dps
-- "wave_adds_hit" N1 +4 (both seats, every wave) and N2 +9 (70 and 50), the
-- press sent three ticks before, as the crab comes into the seat's reach
-- (sm13: pressed at +3 the seats swung at +6, 58 of its 75, and it leaked at
-- +7); leave = the seat's return_to_boss (70: 9.5 /
-- 11, 50: 14 / 16.5, 30: 9 / 10).  After it the seat is ON_BOSS for the rest
-- of the wave, but for dps2's STACK: the frozen crabs on her east tiles
-- (7,0) (8,0) (8,1), where the streams hold 116 / 45 / 28 of the 24 rooms'
-- 20-tick-still crabs; the reference's non-freezer seats hit that stack 0.46
-- / 0.62 / 0.50 / 0.50 times a room at +10-19 / +20-29 / +30-39 / +40 of
-- the 70 wave (0.67 / 0.33 / 0.42 / 0.33 at 50), from (8,-1) (8,-2) (7,2)
-- (9,0); the scythe's arc takes the crabs sharing the tile (crabdmg.py:
-- scythe 3652 hp direct + 577 by its arc).  The freezer's four barrages
-- alone leave each crab ~40 of its 75 (sm13/sm14: mean 15-20 a crab a cast,
-- 1.5-2.2 crabs a cast): 0-1 killed a wave against the reference's 5 / 4.
-- Homes are the script's modal tiles from her south-west tile.
-- ==========================================================================
QD.RAID_MAIDEN_REF = {
    -- the owner's rule (22:00): a crab let in heals her 2 x its hp, a swing on
    -- her is ~40 (the streams' scythe swing on Maiden, 40.1, n=73)
    swing_on_her = 40,
    lanes = { [0] = "S1", [1] = "N1", [2] = "S2", [3] = "N2", [4] = "S3", [5] = "N3",
        [6] = "S4in", [7] = "S4out", [8] = "N4in", [9] = "N4out" },
    waves = {
        [1] = { casts = { { 1, "S1" }, { 6, "N2" }, { 11, "S3" }, { 16, "S4out" } }, ret = 21,
            seat = { [1] = { { "N1", 1, 9 }, { "N2", 6, 10 }, { "STACK", 10, 49 } }, [3] = { { "N1", 1, 9 }, { "N2", 6, 11 }, { "STACK", 10, 49 } } } },
        [2] = { casts = { { 1, "S1" }, { 6, "S2" }, { 11, "S3" }, { 16, "N4out" } }, ret = 26,
            seat = { [1] = { { "N1", 1, 9 }, { "N2", 6, 14 }, { "STACK", 10, 49 } }, [3] = { { "N1", 1, 9 }, { "N2", 6, 16 }, { "STACK", 10, 49 } } } },
        [3] = { casts = { { 1, "S1" }, { 6, "N2" }, { 11, "S3" }, { 16, "N4out" } }, ret = 21,
            seat = { [1] = { { "N1", 1, 9 }, { "STACK", 10, 49 } }, [3] = { { "N1", 1, 10 }, { "STACK", 10, 49 } } } },
    },
    -- each seat's tile per form (0 = her 100 form .. 3 = 30), the script's
    -- modal "tile" (the median where the mode's share is under 0.15):
    -- 100 dps1 (4,6) (the median: its mode (0,6) has 0.17, a 5-tile walk to
    -- the 70 tile on the spawn tick) dps2 (5,6) freezer (15,-1); 70 (5,6) (6,5) (15,0);
    -- 50 (5,6) (6,4) (12,-1); 30 (5,6) (6,5) (6,2)
    home = { [0] = { [1] = { 4, 6 }, [2] = { 15, -1 }, [3] = { 5, 6 } },
        [1] = { [1] = { 5, 6 }, [2] = { 15, 0 }, [3] = { 6, 5 } },
        [2] = { [1] = { 5, 6 }, [2] = { 12, -1 }, [3] = { 6, 4 } },
        [3] = { [1] = { 5, 6 }, [2] = { 6, 2 }, [3] = { 6, 5 } } },
    -- the thresholds and how near one the freezer puts its magic set on (W:643
    -- "hover their mouse over the S1's spawn position when Maiden is close to
    -- spawning")
    thresholds = { 0.70, 0.50, 0.30 }, prime = 0.05,
}

-- THE STATES: per state, the event -> handler table and the tick (filled in below)
QD.RAID_MAIDEN_EVENTS = { "crab_spawn", "crab_frozen", "crab_thaw", "crab_gone", "blood_thrown", "pool_landed", "boss_phase", "hit_taken" }
QD.RAID_MAIDEN_STATES = {
    OPEN    = { tick = "mz_open_tick",    on = { crab_spawn = "mz_stay", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_open_on_blood", pool_landed = "mz_open_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
    DRAIN   = { tick = "mz_drain_tick",   on = { crab_spawn = "mz_stay", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_drain_on_blood", pool_landed = "mz_drain_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
    DODGE   = { tick = "mz_dodge_tick",   on = { crab_spawn = "mz_dodge_on_spawn", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_dodge_on_blood", pool_landed = "mz_dodge_on_pool", boss_phase = "mz_dodge_on_phase", hit_taken = "mz_stay" } },
    F_ON_BOSS = { tick = "mz_f_on_boss_tick", on = { crab_spawn = "mz_f_on_boss_on_spawn", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_f_on_boss_on_blood", pool_landed = "mz_f_on_boss_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
    PREAIM  = { tick = "mz_preaim_tick",  on = { crab_spawn = "mz_preaim_on_spawn", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_preaim_on_blood", pool_landed = "mz_preaim_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
    CAST    = { tick = "mz_cast_tick",    on = { crab_spawn = "mz_cast_on_spawn", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_cast_on_blood", pool_landed = "mz_cast_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
    RETURN  = { tick = "mz_return_tick",  on = { crab_spawn = "mz_return_on_spawn", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_return_on_blood", pool_landed = "mz_return_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
    S_ON_BOSS = { tick = "mz_s_on_boss_tick", on = { crab_spawn = "mz_s_on_boss_on_spawn", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_s_on_boss_on_blood", pool_landed = "mz_s_on_boss_on_pool", boss_phase = "mz_s_on_boss_on_phase", hit_taken = "mz_stay" } },
    LANE    = { tick = "mz_lane_tick",    on = { crab_spawn = "mz_lane_on_spawn", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_lane_on_gone", blood_thrown = "mz_lane_on_blood", pool_landed = "mz_lane_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
    CLAWS   = { tick = "mz_claws_tick",   on = { crab_spawn = "mz_stay", crab_frozen = "mz_stay", crab_thaw = "mz_stay", crab_gone = "mz_stay", blood_thrown = "mz_claws_on_blood", pool_landed = "mz_claws_on_pool", boss_phase = "mz_stay", hit_taken = "mz_stay" } },
}
-- the parallel rule, every state: the blackstorm's prayer (W:590 "halved by
-- activating Protect from Magic")
QD.RAID_MAIDEN_ALWAYS = { storm_sent = "mz_always_on_storm" }

-- THE MACHINE: leave (unsubscribe), enter (subscribe), log the transition
function QD.raid.mz_go(st, v, name, idx)
    local m = st.m
    local S = QD.RAID_MAIDEN_STATES
    assert(S[name] ~= nil, "maiden trio: no state " .. tostring(name))
    if m.state ~= nil then
        for ev, h in pairs(S[m.state].on) do st.off(ev, h) end
    end
    m.state, m.idx = name, idx or 0
    for ev, h in pairs(S[name].on) do st.on(ev, h) end
    m.log = m.log or {}
    if #m.log < 120 then m.log[#m.log + 1] = v.tick .. ":" .. name .. "/" .. m.idx end
    if st.log then QD.ticklog.mark("state p" .. st.role .. " " .. name .. "/" .. m.idx) end
end

function QD.raid.mz_stay(st, v, ev) return nil end

-- ----- what every state reads -----
-- her form: 0 = 100, 1 = 70, 2 = 50, 3 = 30 (the wave index of the crabs out)
function QD.raid.mz_form(st)
    local s = st.boss_symbol or ""
    if s:find("_30", 1, true) then return 3 end
    if s:find("_50", 1, true) then return 2 end
    if s:find("_70", 1, true) then return 1 end
    return 0
end
-- the live crab on a lane (the newest one: the wave's), walking or frozen as asked
function QD.raid.mz_lane_crab(st, v, lane, frozen_only, walking_only, skip)
    skip = skip or {}
    if lane == "STACK" then
        local b = v.ev_boss or v.boss
        -- (sm26: the three named tiles fired 0-4 swings a room; ours freeze on
        -- (6,3) (7,0) (8,0) (10,0) ... -- any frozen crab within 3 of her, the
        -- one nearest me, worth a swing by the owner's rule: 2 x hp > 40)
        local best, bd = nil, nil
        for slot, a in pairs(st.ev.adds) do
            local g = QD.raid._play_gap(b, a.x, a.z)
            if not a.gone and a.ice and not skip[slot] and g >= 1 and g <= 4
                and 2 * QD.raid.mz_crab_hp(a) > QD.RAID_MAIDEN_REF.swing_on_her then
                local d = math.max(math.abs(a.x - v.me.x), math.abs(a.z - v.me.z))
                if bd == nil or d < bd then best, bd = { slot = slot, a = a }, d end
            end
        end
        return best
    end
    if lane:find("|", 1, true) then
        for one in lane:gmatch("[^|]+") do
            local t = QD.raid.mz_lane_crab(st, v, one, frozen_only, walking_only, skip)
            if t ~= nil then return t end
        end
        return nil
    end
    local b = v.ev_boss or v.boss
    local best = nil
    for slot, a in pairs(st.ev.adds) do
        if not a.gone and not skip[slot] and a.label ~= nil and QD.RAID_MAIDEN_REF.lanes[a.label] == lane and b ~= nil
            and QD.raid._play_gap(b, a.x, a.z) >= 2 and (not frozen_only or a.ice) and (not walking_only or not a.ice)
            and (best == nil or a.first > best.a.first) then best = { slot = slot, a = a } end
    end
    return best
end
-- the walker nearest her, in Ice Barrage's ten tiles (the fallback cast)
function QD.raid.mz_nearest_walker(st, v, skip)
    skip = skip or {}
    local b = v.ev_boss or v.boss
    local best, bg = nil, nil
    -- (not a lane a scythe seat's early list names: the dps kill those --
    -- owner sm3, the second cast went to N1 the seats killed at +7)
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    local theirs = {}
    for _, list in pairs(W.seat) do
        for _, e in ipairs(list) do if e[2] <= 5 then theirs[e[1]] = true end end
    end
    for slot, a in pairs(st.ev.adds) do
        if not a.gone and not a.ice and not skip[slot] and b ~= nil and not theirs[QD.RAID_MAIDEN_REF.lanes[a.label] or ""] then
            local g = QD.raid._play_gap(b, a.x, a.z)
            if g >= 2 and math.max(math.abs(a.x - v.me.x), math.abs(a.z - v.me.z)) <= 10 and (bg == nil or g < bg) then best, bg = { slot = slot, a = a }, g end
        end
    end
    return best
end
-- the lanes a scythe seat takes at the spawn (N1, N2): not the freezer's
function QD.raid.mz_seat_lanes(st)
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    local theirs = {}
    for _, list in pairs(W.seat) do
        for _, e in ipairs(list) do if e[2] <= 5 then theirs[e[1]] = true end end
    end
    return theirs
end
function QD.raid.mz_bunch_pick(st, v, skip, within, allow_ice)
    local b = v.ev_boss or v.boss
    if b == nil then return nil end
    local theirs = QD.raid.mz_seat_lanes(st)
    local best, bn, bg = nil, -1, nil
    for slot, a in pairs(st.ev.adds) do
        local g = QD.raid._play_gap(b, a.x, a.z)
        if not a.gone and not skip[slot] and g >= 2 and g <= (within or 99) and math.max(math.abs(a.x - v.me.x), math.abs(a.z - v.me.z)) <= 10
            and (a.ice or not theirs[QD.RAID_MAIDEN_REF.lanes[a.label] or ""]) then
            -- walkers first (a frozen crab cannot be frozen again: wiki_Freeze
            -- :7/:9; it counts for the damage only)
            local n, w = 0, 0
            for _, q in pairs(st.ev.adds) do
                if not q.gone and math.max(math.abs(q.x - a.x), math.abs(q.z - a.z)) <= 1 then
                    n = n + 1
                    if not q.ice then w = w + 1 end
                end
            end
            -- (the target is a walker; the score is every crab the barrage
            -- touches -- the frozen ones' damage is the kill (coordinator
            -- 21:50): sm31 svb fell back to lone walkers beside frozen bunches)
            local score = n * 10 + w
            if (allow_ice or not a.ice) and (score > bn or (score == bn and g < bg)) then best, bn, bg = { slot = slot, a = a, n = n }, score, g end
        end
    end
    return best
end
-- a walk through marked tiles is judged step by step (library
-- _play_safe_step, against the pools, the trails and the blood in flight:
-- sm31 svaplaymaide, the leader stepped (6431,98) <-> (6432,97) across
-- pools, 9 hits for 149, and died)
function QD.raid.mz_safe(st, v, fn, ...)
    local sh = v.shadows
    v.shadows = v.marks
    local a, b, c = fn(...)
    v.shadows = sh
    return a, b, c
end
function QD.raid.mz_floor_ok(st, v)
    local P, m = st.plan, st.m
    local b = v.boss
    return function(x, z)
        local under = b ~= nil and x >= b.x and x <= b.x + (b.size or 1) - 1 and z >= b.z and z <= b.z + (b.size or 1) - 1
        return not under and x >= P.floor[1] + (m.ox or 0) - 12 and x <= P.floor[3] + (m.ox or 0) and z >= P.floor[2] + (m.oz or 0) and z <= P.floor[4] + (m.oz or 0)
    end
end
function QD.raid.mz_walk_to(st, v, intent, x, z)
    local wx, wz = QD.raid.mz_safe(st, v, QD.raid._play_safe_step, st, v, x, z, QD.raid.mz_floor_ok(st, v))
    if wx == v.me.x and wz == v.me.z then return end
    if st.walk_target == nil or st.walk_target.x ~= wx or st.walk_target.z ~= wz then intent.walk = { x = wx, z = wz } end
end
-- the tile to swing at a crab from: edge-adjacent to its 2x2, unmarked, on
-- the floor, nearest me (sm37 _play_maiden: dps2's press on a stacked crab
-- pathed it onto a pool on her east edge, (6,0), three hits of 26 and dead)
function QD.raid.mz_crab_stand(st, v, a)
    local ok = QD.raid.mz_floor_ok(st, v)
    local best, bd = nil, nil
    for x = a.x - 1, a.x + 2 do
        for z = a.z - 1, a.z + 2 do
            local inside = x >= a.x and x <= a.x + 1 and z >= a.z and z <= a.z + 1
            local corner = (x == a.x - 1 or x == a.x + 2) and (z == a.z - 1 or z == a.z + 2)
            if not inside and not corner and ok(x, z) and not v.marks[x * 100000 + z] then
                local d = math.max(math.abs(x - v.me.x), math.abs(z - v.me.z))
                if bd == nil or d < bd then best, bd = { x = x, z = z }, d end
            end
        end
    end
    return best
end
-- a crab's hp from its health bar (what the screen shows); no bar drawn yet
-- = full (the Normal trio's 75: tob.constant ^tob_maiden_crab_hp_3)
function QD.raid.mz_crab_hp(a)
    if a.hr == nil or a.hs == nil or a.hs <= 0 or a.hr < 0 then return 75 end
    return 75 * a.hr / a.hs
end
-- put on the items of a set still in the pack (one block; nothing when worn)
function QD.raid.mz_wear(intent, items)
    local list = {}
    for _, item in ipairs(items) do
        local cr, n = QD.inv.count(item)
        if cr == "ok" and (tonumber(n) or 0) > 0 then list[#list + 1] = item end
    end
    if #list > 0 then intent.gear = list end
    return #list > 0
end
function QD.raid.mz_home(st, v)
    local b = v.boss
    local h = QD.RAID_MAIDEN_REF.home[QD.raid.mz_form(st)][st.role]
    return b.x + h[1], b.z + h[2]
end
-- walk home when more than a tile off it and no walk is under way
function QD.raid.mz_walk_home(st, v, intent)
    local hx, hz = QD.raid.mz_home(st, v)
    if math.max(math.abs(v.me.x - hx), math.abs(v.me.z - hz)) > 1 and not v.marks[hx * 100000 + hz] then
        QD.raid.mz_walk_to(st, v, intent, hx, hz)
        return true
    end
    return false
end
-- a special from the orb: the weapon on, the orb armed, the attack pressed;
-- answers true when the energy has fallen (spent)
function QD.raid.mz_special(st, v, intent, weapon, energy0)
    local _, e = QD.var.varp("varp300_sa_energy")
    if (tonumber(e) or 0) < energy0 - 400 then return true end
    if QD.raid.mz_wear(intent, { weapon }) then return false end
    local _, armed = QD.var.varp("varp301_sa_attack")
    if tonumber(armed) == 0 then intent.spec = true end
    intent.attack = true
    return false
end

-- ----- THE DODGE: an interrupt from any state -----
-- a freezer cast due this tick or the next is not given up for a dodge
-- while the hp holds (sm38 svb: a dodge on +1 put cast 1 at +2 and the
-- 5-tick cooldown chain put cast 4 at +17, after the 4s reached her --
-- three crabs in untouched, 450 healed, against one blood hit)
function QD.raid.mz_cast_due(st, v)
    local m = st.m
    if st.role ~= 2 or m.state ~= "CAST" or v.hp <= 50 then return false end
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    local c = W.casts[m.idx]
    return c ~= nil and v.tick >= st.ev.wave_tick + c[1] - 2
end
function QD.raid.mz_dodge(st, v, ev)
    if not ev.mine then return nil end
    local m = st.m
    if QD.raid.mz_cast_due(st, v) then return nil end
    local x, z = QD.raid._play_maiden_off_tile(st, v, ev.x, ev.z)
    if x == nil then return nil end
    if m.state ~= "DODGE" then m.resume = { state = m.state, idx = m.idx } end
    m.resume.x, m.resume.z = x, z
    QD.raid.mz_go(st, v, "DODGE", v.tick + math.max(ev.ticks or 1, 1))
    return { pri = 8, walk = { x = x, z = z }, why = "dodge" }
end
function QD.raid.mz_dodge_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_dodge_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
-- a wave lands mid-dodge: the dodge resumes into the wave's first state, the
-- one the saved state's crab_spawn handler would have entered (owner sm3: the
-- 30 wave spawned on a dodge tick and the freezer stood in PREAIM to the end)
function QD.raid.mz_dodge_on_spawn(st, v, ev)
    local m = st.m
    if st.role == 2 then
        if m.resume == nil or m.resume.state ~= "CAST" or m.cast_wave ~= st.ev.wave then m.resume = { state = "CAST", idx = 1 } end
    else
        local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
        if W.seat[st.role] ~= nil and (m.resume == nil or m.resume.state ~= "LANE" or m.lane_wave ~= st.ev.wave) then m.resume = { state = "LANE", idx = 1 } end
    end
    return nil
end
function QD.raid.mz_dodge_on_phase(st, v, ev)
    local m = st.m
    if st.role == 2 or m.resume == nil or m.resume.state ~= "S_ON_BOSS" then return nil end
    local _, e = QD.var.varp("varp300_sa_energy")
    local hr, has = QD.inv.has("dragon_claws")
    if QD.raid.mz_form(st) == 3 and (tonumber(e) or 0) >= 500 and hr == "ok" and has then m.resume = { state = "CLAWS", idx = 0 } end
    return nil
end
function QD.raid.mz_dodge_tick(st, v, intent)
    local m = st.m
    -- landed on the safe tile: the saved state again at once, its attack
    -- from here (owner sm4: the freezer stood 7 ticks waiting for the blood
    -- to land; the script's casts run every 5 ticks through the throws)
    local safe = m.resume ~= nil and m.resume.x == v.me.x and m.resume.z == v.me.z and not v.marks[v.me.x * 100000 + v.me.z]
    if v.tick >= m.idx or safe then
        local r = m.resume or { state = (st.role == 2) and "F_ON_BOSS" or "S_ON_BOSS", idx = 0 }
        m.resume = nil
        QD.raid.mz_go(st, v, r.state, r.idx)
    end
end

-- ----- OPEN (scythe seats): the bow from the run-in, in the ranged set -----
function QD.raid.mz_open_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_open_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_open_tick(st, v, intent)
    local m = st.m
    if m.idx == 0 then m.idx = v.tick end
    local last = st.swings[#st.swings] or -1000
    if last >= m.idx or v.tick - m.idx > 10 then QD.raid.mz_go(st, v, "DRAIN", 0) return end
    if QD.raid.mz_wear(intent, st.plan.opener_set) then return end
    intent.attack = true
end

-- ----- DRAIN: the Tonalztics special (W:249; TONALZTICS 23/16/15 of 24) -----
function QD.raid.mz_drain_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_drain_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_drain_tick(st, v, intent)
    local m = st.m
    if m.idx == 0 then m.idx = v.tick end
    local spent = QD.raid.mz_special(st, v, intent, st.plan.opener.weapon, 1000)
    if spent or v.tick - m.idx > 14 then
        intent.spec, intent.attack = nil, false
        if st.role == 2 then QD.raid.mz_go(st, v, "F_ON_BOSS") else QD.raid.mz_go(st, v, "S_ON_BOSS") end
    end
end

-- ----- THE FREEZER -----
function QD.raid.mz_f_on_boss_on_spawn(st, v, ev) QD.raid.mz_go(st, v, "CAST", 1) return nil end
function QD.raid.mz_f_on_boss_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_f_on_boss_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_f_on_boss_tick(st, v, intent)
    local R = QD.RAID_MAIDEN_REF
    local b = v.boss
    local nthr = R.thresholds[QD.raid.mz_form(st) + 1]
    if nthr ~= nil and b.health_ratio ~= nil and b.health_scale ~= nil and b.health_scale > 0 and b.health_ratio >= 0
        and b.health_ratio / b.health_scale <= nthr + R.prime then
        QD.raid.mz_go(st, v, "PREAIM")
        return
    end
    if QD.raid.mz_wear(intent, st.plan.ranged_set) then return end
    if QD.raid.mz_walk_home(st, v, intent) then return end
    intent.attack = true
end

function QD.raid.mz_preaim_on_spawn(st, v, ev) QD.raid.mz_go(st, v, "CAST", 1) return nil end
function QD.raid.mz_preaim_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_preaim_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_preaim_tick(st, v, intent)
    -- no threshold ahead (her 30 form): nothing to pre-aim for
    if QD.RAID_MAIDEN_REF.thresholds[QD.raid.mz_form(st) + 1] == nil then QD.raid.mz_go(st, v, "F_ON_BOSS") return end
    if QD.raid.mz_wear(intent, st.plan.magic_set) then return end
    if QD.raid.mz_walk_home(st, v, intent) then return end
    -- a swap does not end the bow's attack on her (raid seam33 THE HALT): a
    -- step off the tile ends it, so the first cast is the spawn's
    if st.engaged then
        intent.walk = { x = v.me.x, z = v.me.z + 1 }
        st.engaged = false
    end
end

function QD.raid.mz_cast_on_spawn(st, v, ev)
    -- (a burst lands on one tick: only the first crab of a new wave restarts)
    if st.m.idx > 1 or st.m.cast_wave ~= st.ev.wave then QD.raid.mz_go(st, v, "CAST", 1) end
    return nil
end
function QD.raid.mz_cast_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_cast_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_cast_tick(st, v, intent)
    local m = st.m
    m.cast_wave = st.ev.wave
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    if QD.raid.mz_wear(intent, st.plan.magic_set) then return end
    -- a cast whose click framed another npc (two crabs on one tile: "cast
    -- covered ... menu has no row for it", owner sm7 svaplaymaide t162) is
    -- cast again at once on the next pick: the same index, that slot skipped
    local skip = {}
    local last = m.casts[#m.casts]
    if last ~= nil and last.result == "cast" and last.tick < v.tick and last.tick >= v.tick - 2 and st.last_press ~= nil and st.last_press.tick == last.tick
        and st.last_press.spell ~= nil and st.last_press.answer ~= "pressed" and st.last_press.answer ~= "ok" and m.idx > 1 then
        last.result = "covered"
        m.idx = m.idx - 1
    end
    for i = #m.casts, math.max(1, #m.casts - 3), -1 do
        local r = m.casts[i]
        if r.result == "covered" and v.tick - r.tick <= 3 then skip[r.slot] = true end
    end
    local c = W.casts[m.idx]
    -- (the last cast's answer is read the tick after it is sent: RETURN waits
    -- for it, so a covered last cast is cast again -- sm9 sva t161)
    if c == nil then
        if last ~= nil and last.tick >= v.tick then return end
        QD.raid.mz_go(st, v, "RETURN")
        return
    end
    -- (sent the tick before: the cast the client sends at +k animates at the
    -- server's +k+1 -- owner sm4 svaplaymaide, casts sent +1/+6/+11/+16 drew
    -- at +1/+7/+12/+17 against the script's 1/6/11/16)
    -- (sm24: every cast sent a tick early drew at +0/+5/+10/+15 -- the first
    -- one draws on its send tick, the later ones a tick after theirs, behind
    -- the 5-tick cooldown; so cast 1 is sent at +1, casts 2-4 at +k-1)
    if v.tick < st.ev.wave_tick + c[1] - ((m.idx > 1) and 1 or 0) then return end
    -- (the script's lane, else the next walker to arrive: sm15 svb, the
    -- most-crabs pick froze the three 4s at (13,0) at +10, seven tiles out;
    -- the streams' frozen crabs sit at (7,0) (8,0) (8,1), one step from her,
    -- because each cast takes the crab that arrives next)
    -- casts 2-4 (coordinator 22:30): the crab whose barrage 3x3 holds the
    -- most live crabs RIGHT NOW (ties: nearest her), no prediction, no hold
    -- -- sm16 svb t123: 1083/1084/1085 on one tile and 1081 behind them went
    -- untouched while cast 2 took the lone 1080
    local t = nil
    if m.idx == 1 then
        t = QD.raid.mz_lane_crab(st, v, c[2], false, true, skip) or QD.raid.mz_nearest_walker(st, v, skip)
    else
        -- (only a bunch within 4 of her: sm27 cast 3 at +11 froze the 3s and
        -- 4s together on (12,0), seven out, where no seat reaches them; the
        -- streams' frozen crabs sit on (7,0) (8,0) (8,1) -- the bunch reaches
        -- there at +16, cast 4.  Further out: the next walker to arrive)
        -- (no walker within 4 of her yet: the cast waits, to the streams'
        -- latest offset c[3] -- sm29 svb cast 3 at +11 froze the three 4s
        -- together seven out, the only walkers left, where no seat reached
        -- them; the streams' casts 3/4 range [9-16] / [16-24])
        local b = v.ev_boss or v.boss
        local near = false
        for _, a in pairs(st.ev.adds) do
            if not a.gone and not a.ice and QD.raid._play_gap(b, a.x, a.z) <= 4 then near = true end
        end
        -- sm36: the script's lane again, else the next walker to arrive (the
        -- streams' frozen tiles (10,-7) S1 at +1, (7,0) (8,0) (8,1) the S3
        -- and S4 lanes at +11/+16: the stack forms because those lanes share
        -- her south-east corner, not because a cast picked a bunch)
        t = QD.raid.mz_lane_crab(st, v, c[2], false, true, skip) or QD.raid.mz_nearest_walker(st, v, skip)
    end
    if t == nil then
        local seen = {}
        for slot, a in pairs(st.ev.adds) do
            if not a.gone then seen[#seen + 1] = tostring(QD.RAID_MAIDEN_REF.lanes[a.label]) .. (a.ice and "F" or "W") .. "g" .. QD.raid._play_gap(v.ev_boss or v.boss, a.x, a.z) .. "d" .. math.max(math.abs(a.x - v.me.x), math.abs(a.z - v.me.z)) end
        end
        m.casts[#m.casts + 1] = { tick = v.tick, slot = -1, result = "none", wave = QD.raid.mz_form(st), form = #m.forms, why = c[2] .. " none:" .. table.concat(seen, "/") }
    end
    if t ~= nil then
        intent.cast = { spell = st.plan.freeze_spell, symbol = st.plan.crab[st.mode], slot = t.slot, why = "cast " .. m.idx .. " " .. c[2] }
        m.casts[#m.casts + 1] = { tick = v.tick, slot = t.slot, result = "cast", wave = QD.raid.mz_form(st), form = #m.forms,
            why = c[2] .. (QD.RAID_MAIDEN_REF.lanes[t.a.label] == c[2] and "" or (">" .. tostring(QD.RAID_MAIDEN_REF.lanes[t.a.label]))) .. (t.n and ("x" .. t.n) or "") .. "g" .. QD.raid._play_gap(v.ev_boss or v.boss, t.a.x, t.a.z) .. "@" .. (v.tick - st.ev.wave_tick) }
    end
    QD.raid.mz_go(st, v, "CAST", m.idx + 1)
end

function QD.raid.mz_return_on_spawn(st, v, ev) QD.raid.mz_go(st, v, "CAST", 1) return nil end
function QD.raid.mz_return_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_return_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_return_tick(st, v, intent)
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    -- (the script's boss attack lands at +ret: sent the tick before)
    if v.tick >= st.ev.wave_tick + W.ret - 1 then QD.raid.mz_go(st, v, "F_ON_BOSS") return end
    if QD.raid.mz_wear(intent, st.plan.ranged_set) then return end
    QD.raid.mz_walk_home(st, v, intent)
end

-- ----- THE SCYTHE SEATS -----
function QD.raid.mz_s_on_boss_on_spawn(st, v, ev)
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    if W.seat[st.role] ~= nil then
        QD.raid.mz_go(st, v, "LANE", 1)
        st.m.lane_wave = st.ev.wave
    end
    return nil
end
function QD.raid.mz_s_on_boss_on_phase(st, v, ev)
    -- her 30 form: the claws special (W:646 "utilise any remaining special
    -- attacks"; CLAW dps1|30 8, dps2|30 6 of 24 rooms)
    local _, e = QD.var.varp("varp300_sa_energy")
    local hr, has = QD.inv.has("dragon_claws")
    if QD.raid.mz_form(st) == 3 and (tonumber(e) or 0) >= 500 and hr == "ok" and has then QD.raid.mz_go(st, v, "CLAWS") end
    return nil
end
function QD.raid.mz_s_on_boss_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_s_on_boss_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_s_on_boss_tick(st, v, intent)
    if QD.raid.mz_wear(intent, st.plan.melee_set) then return end
    -- her edge without blood (library _play_reach: a pool or a trail on the
    -- edge tile the press would path to is walked around, not stood on)
    local b = v.boss
    local m = st.m
    local function ok(x, z)
        return x >= st.plan.floor[1] + m.ox - 12 and x <= st.plan.floor[3] + m.ox and z >= st.plan.floor[2] + m.oz and z <= st.plan.floor[4] + m.oz
    end
    local hx, hz = QD.raid.mz_home(st, v)
    if QD.raid._play_gap(b, hx, hz) == 1 and not v.marks[hx * 100000 + hz] and (v.me.x ~= hx or v.me.z ~= hz) then
        QD.raid.mz_walk_to(st, v, intent, hx, hz)
        return
    end
    local rx, rz, hold = QD.raid.mz_safe(st, v, QD.raid._play_reach, st, v, ok)
    if rx ~= nil then
        intent.walk = { x = rx, z = rz }
        return
    end
    if hold then return end
    intent.attack = true
end

function QD.raid.mz_lane_on_spawn(st, v, ev)
    if st.m.lane_wave ~= st.ev.wave then QD.raid.mz_go(st, v, "LANE", 1) end
    return nil
end
function QD.raid.mz_lane_on_gone(st, v, ev)
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    local list = W.seat[st.role] or {}
    local e = list[st.m.idx]
    if e ~= nil and QD.RAID_MAIDEN_REF.lanes[ev.label] == e[1] then QD.raid.mz_go(st, v, "LANE", st.m.idx + 1) end
    return nil
end
function QD.raid.mz_lane_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_lane_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_lane_tick(st, v, intent)
    local m = st.m
    m.lane_wave = st.ev.wave
    local W = QD.RAID_MAIDEN_REF.waves[math.max(QD.raid.mz_form(st), 1)]
    local list = W.seat[st.role] or {}
    local e = list[m.idx]
    if e == nil then QD.raid.mz_go(st, v, "S_ON_BOSS") return end
    local since = v.tick - st.ev.wave_tick
    if since > e[3] then QD.raid.mz_go(st, v, "LANE", m.idx + 1) return end
    -- the stack: one swing per frozen crab (a scythe swing on a size-2 crab is
    -- two hits, ~58 of its 75 after a barrage has touched it), then the next
    if m.stack_wave ~= st.ev.wave then m.stack_done, m.stack_swings, m.stack_slot = {}, 0, nil end
    m.stack_wave = st.ev.wave
    if e[1] == "STACK" and m.stack_slot ~= nil and (st.swings[#st.swings] or -1) > m.stack_t then
        m.stack_done[m.stack_slot] = true
        m.stack_slot = nil
        m.stack_swings = m.stack_swings + 1
    end
    -- the script's crab attacks a seat makes in a wave (attacks_add: 70 1.0,
    -- 50 2.0 / 3.0, 30 1.0 -- N1/N2 included): one stack swing a wave, then
    -- her (sm41 budget: sva dps1 walked (8,4) (7,2) (6,0) over four crabs
    -- for 20 ticks of the 70 wave, two swings)
    if e[1] == "STACK" and m.stack_swings >= 1 then QD.raid.mz_go(st, v, "LANE", m.idx + 1) return end
    local t = nil
    if e[1] == "STACK" and m.stack_slot ~= nil then
        local a = st.ev.adds[m.stack_slot]
        if a ~= nil and not a.gone then t = { slot = m.stack_slot, a = a } else m.stack_slot = nil end
    end
    t = t or QD.raid.mz_lane_crab(st, v, e[1], e[4] == true, false, (e[1] == "STACK") and m.stack_done or nil)
    -- (a lane crab absent at its press tick is skipped; the STACK is waited
    -- for, on her, until its window ends -- it forms as the 3s and 4s arrive)
    if t == nil and since >= e[2] and e[1] ~= "STACK" then QD.raid.mz_go(st, v, "LANE", m.idx + 1) return end
    if t == nil or since < e[2] then return QD.raid.mz_s_on_boss_tick(st, v, intent) end
    if QD.raid.mz_wear(intent, st.plan.melee_set) then return end
    if e[1] == "STACK" then
        local stand = QD.raid.mz_crab_stand(st, v, t.a)
        if stand == nil then m.stack_done[t.slot] = true return QD.raid.mz_s_on_boss_tick(st, v, intent) end
        if stand.x ~= v.me.x or stand.z ~= v.me.z then
            local adj = false
            local here = v.me.x * 100000 + v.me.z
            -- already beside it on a clean tile: press from here
            if not v.marks[here] and math.max(math.abs(v.me.x - (t.a.x + 0.5)), math.abs(v.me.z - (t.a.z + 0.5))) <= 1.5 then adj = true end
            if not adj then QD.raid.mz_walk_to(st, v, intent, stand.x, stand.z) return end
        end
    end
    intent.press = { symbol = st.plan.crab[st.mode], slot = t.slot, op = 2, why = "lane " .. e[1] }
    m.add_presses = (m.add_presses or 0) + 1
    if e[1] == "STACK" and m.stack_slot ~= t.slot then m.stack_slot, m.stack_t = t.slot, v.tick end
end

function QD.raid.mz_claws_on_blood(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_claws_on_pool(st, v, ev) return QD.raid.mz_dodge(st, v, ev) end
function QD.raid.mz_claws_tick(st, v, intent)
    local m = st.m
    if m.idx == 0 then
        local _, e = QD.var.varp("varp300_sa_energy")
        m.idx = v.tick * 10000 + (tonumber(e) or 0)
    end
    local start, e0 = m.idx // 10000, m.idx % 10000
    if QD.raid.mz_special(st, v, intent, "dragon_claws", e0) or v.tick - start > 10 then
        intent.spec, intent.attack = nil, false
        QD.raid.mz_go(st, v, "S_ON_BOSS")
    end
end

-- ----- the parallel rule -----
function QD.raid.mz_always_on_storm(st, v, ev)
    return { pri = 3, want = { protectfrommagic = true }, why = "storm" }
end

-- ----- THE SEAT'S DECIDE: the state's tick, then the prayers and the supplies -----
local MZ_ROLE_NAMES = { [1] = "dps1", [2] = "freezer", [3] = "dps2" }
function QD.raid._play_maiden_trio(st, v)
    local P, N = st.plan, st.numbers
    if st.m == nil then
        st.m = { ids = QD.raid._play_maiden_ids(st), forms = {}, casts = {}, log = {}, add_presses = 0, dodges = 0, attacks = {}, autos_since = 99 }
    end
    local m = st.m
    QD.raid._play_maiden_see(st, v)
    for _, p in ipairs(v.incoming) do v.marks[p.x * 100000 + p.z] = true end
    v.shadows = v.marks
    local intent = { want = { protectfrommagic = true } }
    local b = v.boss
    if b == nil then return intent end
    if m.role == nil then
        m.role = MZ_ROLE_NAMES[st.role] or "dps1"
        m.ox, m.oz = b.x - P.body[1], b.z - P.body[2]
        m.body_seen = { x = b.x, z = b.z, tick = v.tick }
        QD.raid.mz_go(st, v, (st.role == 2) and "DRAIN" or "OPEN", 0)
    end
    m.slugs_last = v.slugs
    -- the dodge interrupt from the tick too: blood under me that no event
    -- named (a trail, a pool I walked onto)
    -- (and a blood spawn beside me: it walks her edge laying its trail tile by
    -- tile, sm17 _play_maiden (6,2) (6,3) (6,4) under the leader, 239 hp)
    local slug_near = false
    for _, sl in ipairs(v.slugs or {}) do
        if math.max(math.abs(sl.x - v.me.x), math.abs(sl.z - v.me.z)) <= 1 then slug_near = true end
    end
    if m.state ~= "DODGE" and (v.marks[v.me.x * 100000 + v.me.z] or slug_near) then
        local it = QD.raid.mz_dodge(st, v, { mine = true, x = v.me.x, z = v.me.z, ticks = 1 })
        if it ~= nil then intent.walk = it.walk end
    end
    if m.state == "DODGE" then m.dodges = m.dodges + 1 end
    local s0 = m.state
    QD.raid[QD.RAID_MAIDEN_STATES[m.state].tick](st, v, intent)
    -- a state that changed on its tick runs the new state's tick on the same
    -- tick (one hop): its standing intent replaces the old one's
    if m.state ~= s0 and m.state ~= "DODGE" then
        local keep = intent.want
        for k in pairs(intent) do intent[k] = nil end
        intent.want = keep
        QD.raid[QD.RAID_MAIDEN_STATES[m.state].tick](st, v, intent)
    end
    -- PRAYERS: Protect from Magic always (the storm is magic, W:590); Piety on
    -- a scythe seat, Rigour on the freezer's bow (10Boot 0:02:45)
    if st.role == 2 then
        if m.state == "F_ON_BOSS" then intent.want.rigour = true end
    else
        intent.want.piety = true
    end
    -- SUPPLIES: the library's bite by the largest hit due (her storm, a pool
    -- under the seat), then the seat's own rules
    local here = v.me.x * 100000 + v.me.z
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, function(h)
        local storm = N.storm + 2
        if v.marks[here] then return storm + 2 * N.pool end
        return storm
    end)
    local function is_brew(item)
        for _, name in ipairs(QD.RAID_PLAY_BREWS) do if item == name then return true end end
        return false
    end
    local drink_ready = v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY
    if st.role == 2 then
        -- the freezer drinks no brew while it eats (role.freezer.phase.*.drinks 0;
        -- a dose costs Ice Barrage's 94 Magic) and restores Magic under 94
        if intent.drink ~= nil and intent.eat ~= nil and is_brew(intent.drink) then intent.drink = nil end
        local _, mg = QD.skill.read("magic")
        if drink_ready and mg ~= nil and mg.level < P.freeze_level then intent.drink = QD.raid._play_maiden_restore() or intent.drink end
    else
        -- a scythe seat eats at 45 or under and drinks no brew while a fish is
        -- left (eat_at_hp_pct 36 [14-75]; a brew takes the Strength it swings
        -- with); the super combat again under 112 (her storm drains, W:591)
        if v.hp > P.melee_eat_at and not v.marks[here] then
            intent.eat = nil
            if intent.drink ~= nil and is_brew(intent.drink) then intent.drink = nil end
        end
        local fr, fish = QD.inv.count("anglerfish")
        if intent.drink ~= nil and is_brew(intent.drink) and fr == "ok" and (tonumber(fish) or 0) > 0 and v.hp > 20 then intent.drink = nil end
        -- at 45 or under with no fish: the brew, never the super combat (sm25
        -- sva: out of fish at 43 the leader drank two combat doses and her
        -- next auto, 43, killed it)
        if v.hp <= P.melee_eat_at and intent.eat == nil and drink_ready and (fr ~= "ok" or (tonumber(fish) or 0) == 0) then
            for _, name in ipairs(QD.RAID_PLAY_BREWS) do
                local br, bn = QD.inv.count(name)
                if br == "ok" and (tonumber(bn) or 0) > 0 then intent.drink = name break end
            end
        end
        if intent.drink == nil and drink_ready and v.hp > P.melee_eat_at then
            local _, sg = QD.skill.read("strength")
            if sg ~= nil and sg.level < 112 then
                local doses = 0
                local combat = nil
                for k, dose in ipairs({ "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat" }) do
                    local cr, n = QD.inv.count(dose)
                    if cr == "ok" and (tonumber(n) or 0) > 0 then
                        doses = doses + k * tonumber(n)
                        if combat == nil then combat = dose end
                    end
                end
                if combat ~= nil and doses > (P.reboost_keep or 0) then intent.drink = combat end
            end
        end
    end
    if intent.drink == nil and drink_ready then
        local missing = v.prayer_base - v.prayer
        if missing >= QD.RAID_PLAY_RESTORE_AMOUNT then intent.drink = QD.raid._play_maiden_restore() end
    end
    -- the record: a scythe seat's Strength and Prayer every ten ticks
    if st.role ~= 2 and v.tick % 10 == 0 then
        local _, sgl = QD.skill.read("strength")
        m.samples = m.samples or {}
        if #m.samples < 30 then m.samples[#m.samples + 1] = v.tick .. ":" .. tostring(sgl and sgl.level) .. "/" .. tostring(v.prayer) end
    end
    return intent
end

function QD.raid._play_maiden_on_start(st)
    if st.party <= 1 then return end
    for ev, h in pairs(QD.RAID_MAIDEN_ALWAYS) do st.on(ev, h) end
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
    -- a party plays the state machines above; this decide is the Entry solo's
    if st.party > 1 then return QD.raid._play_maiden_trio(st, v) end
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
    -- (the record: a scythe seat's Strength, Prayer and lit Piety every ten
    -- ticks, for the harness's role row)
    if R ~= nil and R.melee and v.tick % 10 == 0 then
        local _, sgl = QD.skill.read("strength")
        m.samples = m.samples or {}
        if #m.samples < 30 then
            local _, mx = QD.var.varp("varp6287_com_maxhit")
            m.samples[#m.samples + 1] = v.tick .. ":" .. tostring(sgl and sgl.level) .. "/" .. tostring(v.prayer) .. (v.lit and v.lit.piety and "P" or "") .. "m" .. tostring(mx)
        end
    end
    -- owner_tob_normal M28: A SCYTHE SEAT EATS LATE.  The reference's dps eat
    -- at 36 percent of their hitpoints (role.dps2.eat_at_hp_pct 36 [14-75],
    -- maiden_normal_3.json) and drink nothing in the room (role.dps*.phase.*
    -- .drinks 0); ours ate at 78 (owner M1 report) and combo-drank brews that
    -- take 10% + 2 of the Strength the scythe swings with (wiki Saradomin brew)
    -- -- its three hits on her came to 39.4 a swing against the reference's
    -- 47 an attack.  Above P.melee_eat_at hitpoints, with no pool under the
    -- seat, no bite and no brew (a restore or the super combat still goes).
    if R ~= nil and R.melee and v.hp > P.melee_eat_at and not v.marks[me.x * 100000 + me.z] then
        if intent.eat ~= nil then m.late_eats = (m.late_eats or 0) + 1 end
        intent.eat = nil
        for _, name in ipairs(QD.RAID_PLAY_BREWS) do
            if intent.drink == name then intent.drink = nil end
        end
    end
    -- (and no brew at all while a fish is left: owner M28 svbplaymaide, the
    -- east seat drank six brew doses in the combo bites -- about 80 points of
    -- Strength -- and its swings on her fell from 48.7 a swing in her 70 form
    -- to 34.6 in her 50 form)
    if R ~= nil and R.melee and intent.drink ~= nil then
        local is_brew = false
        for _, name in ipairs(QD.RAID_PLAY_BREWS) do
            if intent.drink == name then is_brew = true end
        end
        local fr, fish = QD.inv.count("anglerfish")
        if is_brew and fr == "ok" and (tonumber(fish) or 0) > 0 and v.hp > 20 then
            intent.drink = nil
            m.brews_held = (m.brews_held or 0) + 1
        end
    end
    -- owner_tob_normal M1: THE FREEZER DRINKS NO BREW while it has a fish.
    -- A dose lowers Magic by 10% + 2 (wiki Saradomin brew) and Ice Barrage
    -- needs 94: the owner baseline's freezer (_play_maiden) combo-ate a fish
    -- and a brew at t203 and every barrage of the 30 percent wave was refused
    -- ("Your Magic level is not high enough for this spell", t219-t234), all
    -- six crabs walked in.  The reference's freezer drinks nothing in the
    -- room (role.freezer.phase.*.drinks 0 [0-0]).  A Magic under 94 is
    -- restored first, before anything else is drunk.
    if R ~= nil and R.freezer then
        if intent.drink ~= nil and intent.eat ~= nil then
            for _, name in ipairs(QD.RAID_PLAY_BREWS) do
                if intent.drink == name then intent.drink = nil end
            end
        end
        if v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY and mg ~= nil and mg.level < P.freeze_level then
            local rs = QD.raid._play_maiden_restore()
            if rs ~= nil then
                intent.drink = rs
                m.magic_restores = (m.magic_restores or 0) + 1
            end
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
            -- (owner_tob_normal M30: at 112, five under the boost's 118: her
            -- blackstorm drains a scythe seat's melee stats by (damage+1)/5 on
            -- half her hits, W:591, and the swings fell to 31-37 a swing in the
            -- later forms against 44-50 fresh, owner M28/M29 per-form means)
            if (at.level < 112 or sg.level < 112) and combat ~= nil then
                intent.drink = combat
                m.reboosts = (m.reboosts or 0) + 1
            elseif at.level < 90 or sg.level < 90 then intent.drink = QD.raid._play_maiden_restore() end
        elseif rg.level < floor_lv then intent.drink = QD.raid._play_maiden_restore() end
    end
    -- raid seam40: a scythe seat's press paths it to her edge, and that path
    -- goes through no skill; while blood is down it walks to an unmarked edge
    -- tile first (library _play_reach, Bloat's shape)
    -- (owner_tob_normal M12: the freezer on its scythe too: owner M11
    -- _play_maiden t283-t309, its press walked it onto a trail beside her and
    -- the pool step put it on the next one, 20 a tick on (6,0) / (7,-1))
    if ((R ~= nil and R.melee) or m.solo_melee ~= nil or m.fz == "melee") and intent.attack and intent.walk == nil and not moving and add == nil then
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
