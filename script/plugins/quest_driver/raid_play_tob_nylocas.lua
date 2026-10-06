-- quest-driver / raid_play_tob_nylocas: the Nylocas plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files).
-- Written by raid seam30 play_tob_nylocas from the sources line by line;
-- PLAY_NOTES.md "Nylocas, Entry solo" is its strategy table.
--
-- Sources, abbreviated in the comments below:
--   E   docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext
--       (the Entry page's own Nylocas section and its "Solo strategy", :153-176)
--   W   .../sources/wiki_Theatre_of_Blood_Strategies.wikitext (:702-776)
--   NT  docs/minigames/theater_of_blood/encounters/nylocas.tsv (our spec rows)
--   DMG OSRS-Content .../minigame_tob/scripts/tob_damage.rs2 (nulling, reflect)
--   NB  OSRS-Content .../minigame_tob/scripts/tob_nylocas_boss.rs2 (Vasilias)
--   NR  OSRS-Content .../minigame_tob/scripts/tob_nylocas.rs2 (the waves)
--
-- What the plan sees is what a person at the screen sees: every npc's colour,
-- size, tile and health bar (api_drive.npcs), the boss's form, the tick a
-- copy first appeared, its own swings (player_anim, the library) and its own
-- hitpoints, prayer and lit prayers.  It never reads a server register.

QD.raid._play_plan("tob_nylocas", {
    room = "nylocas",
    -- Vasilias drops in as the spawning form and turns melee (NB :95-106);
    -- the library follows st.boss_symbol, which the see step below moves to
    -- her current form every tick (maiden's pattern).
    boss = { entry = "nylocas_boss_spawning_story" },
    modes = {
        -- NT nylocas.max_hit_small_entry 1-5, max_hit_big_entry 1-10,
        -- explosion_entry 1-8 (E :161 "about 8 damage in Entry Mode"),
        -- vasilias_max_hit_entry 1-24, pillar_collapse_entry_min 30 (E :171
        -- "30+ damage"); hp: small 2, big 3 (E :161), cadence 3 (NT attackrate)
        entry = { small_hit = 5, big_hit = 10, explode = 8, boss_hit = 24, prayed_hit = 17, collapse = 40, cadence = 3,
            form = { melee = "nylocas_boss_melee_story", magic = "nylocas_boss_magic_story",
                ranged = "nylocas_boss_ranged_story", spawning = "nylocas_boss_spawning_story" },
            suffix = "_story", support = "tob_nylocas_support_story" },
    },
    -- the three protection prayers are the only prayers this plan lights:
    -- "always switch protection prayers ... When its form changes, the player
    -- should again switch prayers" (W :752)
    walk_prayers = { "protectfrommelee", "protectfrommagic", "protectfrommissiles" },
    down_prayers = {},
    prayer_of = { melee = "protectfrommelee", magic = "protectfrommagic", ranged = "protectfrommissiles" },
    -- One weapon per colour ("You will need all three attack styles for this
    -- room", E :155; "Ancient Magicks is highly recommended, and a fast ranged
    -- weapon such as a magic shortbow", E :157).  Speeds: abyssal whip 4,
    -- magic shortbow on rapid 3, a spell 5 (wiki item pages); the swing seqs
    -- as measured in build/quest_gate/tob_nylocas/ticklog.tsv (1658, 426,
    -- 1979; Ice Rush shares the rush/blitz cast 1978).
    loadout = {
        melee = { item = "abyssal_whip", speed = 4, seqs = { [1658] = true } },
        ranged = { item = "magic_shortbow", speed = 3, seqs = { [426] = true } },
        magic = { item = "lava_battlestaff", speed = 5, seqs = { [1978] = true, [1979] = true } },
    },
    -- the wave colours and kinds by symbol stem (NR :647-752)
    kinds = { "incoming", "fighting", "big_incoming", "big_fighting" },
    styles = { "melee", "ranged", "magic" },
    -- Room geometry local to the 64x64 square (NT nylocas.pillar_anchors
    -- 3289,4242 3300,4242 3289,4253 3300,4253 in region base 3264,4224 ->
    -- local 25,18 36,18 25,29 36,29; supports are 3x3).  "it's best to stay
    -- near the centre of the arena as much as possible, unless you are
    -- cleaning up greys" (E :162): home is the centre between the four.
    supports = { { 25, 18 }, { 36, 18 }, { 25, 29 }, { 36, 29 } },
    home = { 31, 24 },
    floor = { 19, 12, 44, 36 },
    -- NT nylocas.lifetime_small 52 (explodes on lifetime tick 52, i.e. 51
    -- ticks after the tick it appears), lifetime_big 53; the blast reaches two
    -- tiles from the footprint ("This damage can be avoided by being at least
    -- two tiles away", E :161; NR :1276 `npc_range(coord) <= 2`).  The T-1
    -- rule (ET 1.1): the tile read is the end of the tick before, so the
    -- raider is three tiles off by age `explode_age - 2` (margin one tick
    -- for the first-seen tick lagging the spawn).
    explode_age = 51, explode_age_big = 52, blast = 2,
    -- NT nylocas.flicker_first_wave 16, flicker_first_switch 5,
    -- flicker_hold 2: a flicker's colour is final from age 7, so from wave 16
    -- no copy younger than `settle_age` is hit (a wrong-colour hit nulls the
    -- raider on it for good: DMG :272 "this player is nulled on this nylocas
    -- from now on").
    flicker_wave = 16, settle_age = 7,
    -- reaches: the bow on rapid 7, a spell 10 (wiki Magic shortbow, Ice Rush)
    reach = { melee = 1, ranged = 7, magic = 10 },
    -- NB :445-464 / tob_nylocas.constant ^tob_vasilias_entry_window_ticks 15:
    -- the first window 14, every later one 15.
    first_window = 14, window = 15,
    decide = "_play_nylocas_decide",
    -- one client.log line a tick while the plan is iterated from the log
    trace = false,
})

-- The ids of the room's npcs, once per play (a symbol is a content name, an id
-- is what the npc rows carry).
function QD.raid._play_nylocas_ids(st)
    local P, N = st.plan, st.numbers
    local ids = { wave = {}, boss = {}, support = nil }
    for _, kind in ipairs(P.kinds) do
        for _, style in ipairs(P.styles) do
            local sym = "tob_nylocas_" .. kind .. "_" .. style .. N.suffix
            local r, id = api_drive.symbol("npc", sym)
            assert(r == "ok", "raid.play nylocas: no npc symbol " .. sym)
            ids.wave[id] = { symbol = sym, style = style, big = string.find(kind, "big", 1, true) ~= nil,
                fighting = string.find(kind, "fighting", 1, true) ~= nil }
        end
    end
    for form, sym in pairs(N.form) do
        local r, id = api_drive.symbol("npc", sym)
        assert(r == "ok", "raid.play nylocas: no npc symbol " .. sym)
        ids.boss[id] = { symbol = sym, form = form }
    end
    local r, id = api_drive.symbol("npc", N.support)
    assert(r == "ok", "raid.play nylocas: no npc symbol " .. N.support)
    ids.support = id
    return ids
end

-- Chebyshev distance from a tile to an npc's footprint (size n, south-west x,z).
function QD.raid._play_nylocas_dist(x, z, nx, nz, n)
    local dx, dz = 0, 0
    if x < nx then dx = nx - x elseif x > nx + n - 1 then dx = x - (nx + n - 1) end
    if z < nz then dz = nz - z elseif z > nz + n - 1 then dz = z - (nz + n - 1) end
    return math.max(dx, dz)
end

-- SEE (the room's part): every wave nylocas with its colour, size, tile, age
-- (ticks since this copy first appeared), the supports' bars and Vasilias.
function QD.raid._play_nylocas_see(st, v)
    local P, ny = st.plan, st.ny
    v.nylos, v.supports, v.vas = {}, {}, nil
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" or type(rows) ~= "table" then return end
    local O = st.origin
    for _, row in ipairs(rows) do
        -- a row with no health bar yet reads -1/-1 (maiden seam30, mz30b): alive
        local alive = row.health_ratio == nil or row.health_ratio ~= 0
        local w = ny.ids.wave[row.npc_id] or ny.ids.wave[row.base_npc_id]
        local b = ny.ids.boss[row.npc_id] or ny.ids.boss[row.base_npc_id]
        if w ~= nil then
            local s = ny.seen[row.slot]
            if s == nil or s.last < v.tick - 2 then
                local lx, lz = row.x - O.x, row.z - O.z
                local lane = lx <= 18 or lx >= 45 or lz <= 10
                s = { first = v.tick, last = v.tick, style = w.style, flicker = false, lane = lane }
                ny.seen[row.slot] = s
                -- a wave is the tick new copies walk out of the tunnels
                -- (NT nylocas.lane_tiles); a split appears on the platform
                if lane and ny.wave_ticks[v.tick] == nil then
                    ny.wave_ticks[v.tick] = true
                    ny.waves = ny.waves + 1
                end
            end
            s.last = v.tick
            if s.style ~= w.style then
                s.flicker = true
                s.style = w.style
                s.style_tick = v.tick
            end
            if alive then
                v.nylos[#v.nylos + 1] = { row = row, slot = row.slot, x = row.x, z = row.z, style = w.style, big = w.big,
                    fighting = w.fighting, symbol = w.symbol, size = w.big and 2 or 1, age = v.tick - s.first, seen = s }
            end
        elseif b ~= nil and alive then
            v.vas = { row = row, slot = row.slot, x = row.x, z = row.z, form = b.form, symbol = b.symbol }
            if st.plan.trace and ny.vas_id_seen ~= tostring(row.npc_id) .. "/" .. tostring(row.base_npc_id) then
                ny.vas_id_seen = tostring(row.npc_id) .. "/" .. tostring(row.base_npc_id)
                local m = {}
                for id, e in pairs(ny.ids.boss) do m[#m + 1] = id .. "=" .. e.form end
                api_drive.report("nyplay vas t=" .. v.tick .. " row " .. ny.vas_id_seen .. " -> " .. b.form .. " ids " .. table.concat(m, " "))
            end
            if st.boss_symbol ~= b.symbol then st.boss_symbol = b.symbol end
        elseif (row.npc_id == ny.ids.support or row.base_npc_id == ny.ids.support) then
            local frac = 1
            if row.health_ratio ~= nil and row.health_scale ~= nil and row.health_scale > 0 and row.health_ratio >= 0 then
                frac = row.health_ratio / row.health_scale
            end
            v.supports[#v.supports + 1] = { x = row.x, z = row.z, frac = frac, alive = alive }
        end
    end
    -- her size from the library's own read of her (npc.state carries it)
    if v.vas ~= nil then
        v.vas.size = (v.boss ~= nil and type(v.boss.size) == "number") and v.boss.size or 3
    end
end

-- A block of the plan's own: a loadout swap ("a gear swap is one tick",
-- PLAY_NOTES "Loadouts"; the library's SEND has no gear list), counted into
-- the record's inputs like the library's blocks (maiden's pattern).
-- `pray` (a prayer name or nil) rides in the same block, FIRST: the plan's
-- prayer goes out before its press, because a press can take a tick and the
-- library's own block comes after it (svbplaynyloc t664: her turn seen, the
-- cast pressed, the library's prayer read lit three ticks later on t667, her
-- first magic attack on t666 sent through Protect from Melee).
function QD.raid._play_nylocas_wear(st, v, style, pray)
    local ny = st.ny
    local L = style ~= nil and st.plan.loadout[style] or nil
    local r, d = QD.together(function()
        if pray ~= nil then QD.prayer.set(pray, true) end
        if L ~= nil then QD.player.equip(L.item) end
    end)
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + (L ~= nil and 1 or 0) + (pray ~= nil and 1 or 0)
    if pray ~= nil then
        -- the library's SEND reads v.lit: what this block lit is lit now
        for _, name in pairs(st.plan.prayer_of) do v.lit[name] = (name == pray) end
        ny.early_prayers = (ny.early_prayers or 0) + 1
    end
    if L == nil then
        st.blocks[r] = (st.blocks[r] or 0) + 1
        return r == "ok" or r == "split"
    end
    st.blocks[r] = (st.blocks[r] or 0) + 1
    ny.swaps = ny.swaps + 1
    if r ~= "ok" and r ~= "split" then
        st.refusals = st.refusals + 1
        if #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " wear " .. style .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 140) end
        return false
    end
    ny.worn = style
    st.weapon = L
    return true
end

-- The hit delay of a launched attack, in ticks (wiki Hit delay: ranged
-- 1 + floor((3 + d) / 6), magic 1 + floor((1 + d) / 3), melee 0).
function QD.raid._play_nylocas_flight(style, d)
    if style == "ranged" then return 1 + math.floor((3 + d) / 6) end
    if style == "magic" then return 1 + math.floor((1 + d) / 3) end
    return 0
end

-- THE NYLOCAS PLAN'S DECIDE (PLAY_NOTES.md "Nylocas, Entry solo").
function QD.raid._play_nylocas_decide(st, v)
    local P, N = st.plan, st.numbers
    if st.ny == nil then
        st.ny = { ids = QD.raid._play_nylocas_ids(st), seen = {}, wave_ticks = {}, waves = 0, worn = "ranged",
            swaps = 0, presses = 0, casts = 0, bursts = 0, holds = 0, escapes = 0, homes = 0, target = nil,
            doomed = {}, nulled = {}, frozen = {}, turns = {}, form = nil, next_turn = nil, prayer = nil, prayer_tick = -100,
            swing_seen = 0, flicker_cancels = 0, results = {}, vas_presses = {}, landed = nil }
        st.weapon = P.loadout.ranged
    end
    local ny = st.ny
    -- LIBRARY FAULT, worked around here (raid seam30, ny30h): the loop stops on
    -- the first npc_death row of the boss's slot `since` st.death_serial, and
    -- that serial starts at 0.  Vasilias takes a slot a wave nylocas died in
    -- (slot 1079: npc_spawn 10786 on t644, the loop answered `ok` on t644), so
    -- the room "ended" the tick she landed.  Until her slot is known the
    -- serial follows the log, so only a death after she is seen can count.
    if st.log and st.boss_slot == nil then
        local dr, drows = QD.ticklog.rows({ kind = "npc_death", since = st.death_serial })
        if dr == "ok" then
            for _, row in ipairs(drows) do st.death_serial = math.max(st.death_serial, row.serial) end
        end
    end
    QD.raid._play_nylocas_see(st, v)
    local dist = QD.raid._play_nylocas_dist
    local flight = QD.raid._play_nylocas_flight
    local intent = { want = {}, walk = nil, attack = false }
    local O, me = st.origin, v.me
    local home = { x = O.x + P.home[1], z = O.z + P.home[2] }
    local vas = v.vas

    -- my own swings (the library reads them off player_anim): the copy I was
    -- on is doomed until the hit has had time to land (2 or 3 hitpoints, E :161)
    while ny.swing_seen < #st.swings do
        ny.swing_seen = ny.swing_seen + 1
        local tk = st.swings[ny.swing_seen]
        local t = ny.target
        if t ~= nil then
            t.swung = tk
            if not t.vas then
                ny.doomed[t.slot] = tk + flight(t.style, t.d or 1) + 2
                for _, s in ipairs(t.also or {}) do ny.doomed[s] = tk + flight(t.style, t.d or 1) + 2 end
            end
        end
    end

    -- HER FORM.  She lands melee (W :754 "Vasilias will always spawn in its
    -- melee form"), turns every 15 ticks after a first 14 (NB :436-464), and
    -- the turn stops every player's attack ("The player will stop attacking
    -- when Vasilias changes forms", W :752; NB :176 p_stopaction).
    if vas ~= nil and vas.form ~= ny.form then
        if ny.form ~= nil and ny.form ~= "spawning" and vas.form ~= "spawning" then
            ny.turns[#ny.turns + 1] = { tick = v.tick, form = vas.form }
            ny.next_turn = v.tick + P.window
        elseif vas.form ~= "spawning" then
            ny.landed = v.tick
            ny.next_turn = v.tick + P.first_window
        end
        ny.form = vas.form
        ny.target = nil
    end

    -- PRAYER.  Vasilias: by her form, from the tick she is seen (W :752
    -- "always switch protection prayers to Protect from Melee before
    -- attacking ... When its form changes, the player should again switch";
    -- her first attack in a form comes 2-3 ticks after the turn, NB :146-150,
    -- so a switch read on the turn is in force for it).  The waves: the
    -- colour of the swinging majority among the aggros in reach (a big counts
    -- two, a grey only within two tiles: it swings adjacent, NR :1086), held
    -- until another colour is clearly heavier, so one prayer is on per hit.
    local pray_style = ny.prayer
    if vas ~= nil then
        pray_style = (vas.form == "spawning") and "melee" or vas.form
    else
        local weight = { melee = 0, ranged = 0, magic = 0 }
        for _, n in ipairs(v.nylos) do
            if n.fighting then
                local d = dist(me.x, me.z, n.x, n.z, n.size)
                local reach = (n.style == "melee") and 2 or 9
                if d <= reach then weight[n.style] = weight[n.style] + (n.big and 2 or 1) end
            end
        end
        local best, bw = nil, 0
        for _, s in ipairs(P.styles) do
            if weight[s] > bw then best, bw = s, weight[s] end
        end
        if best ~= nil and best ~= pray_style then
            local cur = pray_style ~= nil and weight[pray_style] or 0
            if pray_style == nil or bw >= cur + 2 or (cur == 0 and v.tick - ny.prayer_tick >= 3) then
                pray_style = best
            end
        end
    end
    if pray_style ~= ny.prayer then
        ny.prayer = pray_style
        ny.prayer_tick = v.tick
    end
    if pray_style ~= nil then
        intent.want[P.prayer_of[pray_style]] = true
        -- The protection prayers exclude each other: lighting one puts the
        -- other out on the server, and a press is a TOGGLE, so the library's
        -- "off" for the old one (_play_pray) after the new one's "on" lit the
        -- old one again (ny30d: Protect from Missiles held t67-362 while the
        -- plan asked for Magic 7 times).  The old one is left to the server.
        for _, name in pairs(P.prayer_of) do
            if v.lit[name] == true then intent.want[name] = true end
        end
    end

    -- THE BLAST.  A copy explodes 51 ticks after it appears (52 a big), within
    -- two tiles of its body (NT lifetime_small/big; E :161).  Danger runs from
    -- six ticks before to one after; a raider inside its reach leaves on sight.
    local danger = {}
    for _, n in ipairs(v.nylos) do
        local ea = n.big and P.explode_age_big or P.explode_age
        if n.age >= ea - 6 and n.age <= ea + 1 then danger[#danger + 1] = n end
    end
    local function in_support(x, z)
        for _, s in ipairs(P.supports) do
            local sx, sz = O.x + s[1], O.z + s[2]
            if x >= sx and x <= sx + 2 and z >= sz and z <= sz + 2 then return true end
        end
        return false
    end
    local function floor_ok(x, z)
        local lx, lz = x - O.x, z - O.z
        if lx < P.floor[1] or lx > P.floor[3] or lz < P.floor[2] or lz > P.floor[4] then return false end
        if in_support(x, z) then return false end
        if vas ~= nil and dist(x, z, vas.x, vas.z, vas.size) == 0 then return false end
        return true
    end
    local function unsafe(x, z, margin)
        for _, n in ipairs(danger) do
            if dist(x, z, n.x, n.z, n.size) <= P.blast + (margin or 0) then return true end
        end
        return false
    end
    if unsafe(me.x, me.z, 0) then
        local best, bx, bz = nil, nil, nil
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if (dx ~= 0 or dz ~= 0) and floor_ok(x, z) and not unsafe(x, z, 0) then
                    local score = math.max(math.abs(dx), math.abs(dz)) * 10
                        + math.max(math.abs(x - home.x), math.abs(z - home.z))
                    if best == nil or score < best then best, bx, bz = score, x, z end
                end
            end
        end
        if bx ~= nil then
            intent.walk = { x = bx, z = bz }
            ny.escapes = ny.escapes + 1
            ny.target = nil
        end
    end

    -- THE TARGET.
    local cur = ny.target
    local cur_row = nil
    if cur ~= nil and not cur.vas then
        for _, n in ipairs(v.nylos) do
            if n.slot == cur.slot then cur_row = n end
        end
        if cur_row ~= nil and cur.colour ~= nil and cur_row.style ~= cur.colour then
            -- a flicker turned under the press: never let the old colour land
            -- (DMG :272, the raider is nulled on it for good)
            ny.flicker_cancels = ny.flicker_cancels + 1
            cur_row = nil
            if intent.walk == nil then
                intent.walk = { x = me.x, z = me.z }
                if floor_ok(me.x + 1, me.z) then intent.walk = { x = me.x + 1, z = me.z } end
            end
        end
        if cur_row == nil then ny.target = nil cur = nil end
    end
    local pick = nil
    if vas ~= nil then
        if vas.form ~= "spawning" then
            local d = dist(me.x, me.z, vas.x, vas.z, vas.size)
            pick = { slot = vas.slot, style = vas.form, symbol = vas.symbol, vas = true, d = d, x = vas.x, z = vas.z }
        end
    else
        -- the support each chewer bites, and how much of it is left
        local function support_of(n)
            for _, sp in ipairs(v.supports) do
                if sp.alive and dist(n.x, n.z, sp.x, sp.z, 3) <= 1 and dist(sp.x, sp.z, n.x, n.z, n.size) <= 1 then return sp end
            end
            return nil
        end
        local alive_supports, lowest = 0, nil
        for _, sp in ipairs(v.supports) do
            if sp.alive then
                alive_supports = alive_supports + 1
                if lowest == nil or sp.frac < lowest.frac then lowest = sp end
            end
        end
        local best = nil
        local function consider(cand)
            if best == nil or cand.score < best.score then best = cand end
        end
        for _, n in ipairs(v.nylos) do
            local ea = n.big and P.explode_age_big or P.explode_age
            local d = dist(me.x, me.z, n.x, n.z, n.size)
            local lx, lz = n.x - O.x, n.z - O.z
            local on_floor = lx >= P.floor[1] and lx <= P.floor[3] and lz >= P.floor[2] and lz <= P.floor[4]
            local ok = (ny.doomed[n.slot] == nil or ny.doomed[n.slot] < v.tick) and not ny.nulled[n.slot]
                and (ny.blocked == nil or (ny.blocked[n.slot] or -1) < v.tick)
                -- from wave 16 a copy's colour may still turn (NT flicker_*)
                and (ny.waves < P.flicker_wave - 1 or n.age >= P.settle_age)
            if ok and n.style == "melee" then
                -- "cannot melee them until they reach said platform" (E :160),
                -- and never into a blast
                ok = on_floor and n.age < ea - 6 and not unsafe(n.x, n.z, 1)
            elseif ok then
                ok = d <= P.reach[n.style] + 6
            end
            local sp = (not n.fighting) and support_of(n) or nil
            n.support = sp
            if ok then
                -- the cost of the kill in ticks: the run to reach (two tiles a
                -- tick, wiki Energy: run) and a swap, weighed against what the
                -- copy costs while it lives
                local walk = math.max(0, d - P.reach[n.style]) / 2
                local score = walk * 6
                if n.fighting then
                    -- aggros first: they "must be killed as fast as possible"
                    -- (E :160); the one hitting through the prayer before all
                    local hitting = (n.style == "melee" and d <= 1) or (n.style ~= "melee" and d <= 8)
                    local covered = n.style == pray_style
                    if hitting and not covered then score = score - 45
                    elseif hitting then score = score - 30
                    else score = score - 20 end
                elseif sp ~= nil then
                    -- a chewer: "keep the pillars alive" (E :155); but "it's best to
                    -- let one that's low die and focus on the other three" (E :171)
                    if sp == lowest and sp.frac < 0.15 and alive_supports > 1 then score = score + 25
                    else score = score - 12 - (1 - sp.frac) * 12 end
                end
                -- "Focus the green (Ranged) Nylocas first" (E :162)
                if n.style == "ranged" then score = score - 4 end
                -- older first: nearer to biting and exploding (W :746
                -- "always kill newly spawned nylocas after dealing with aggro's,
                -- prioritising the smaller ones first")
                score = score - math.min(n.age, 45) * 0.2
                if n.big then score = score + 2 end
                if n.style ~= ny.worn then score = score + 5 end
                -- the one already pressed keeps its press unless another is
                -- clearly worth more (no target flapping, ny30f t328-335)
                if cur ~= nil and cur.slot == n.slot then score = score - 12 end
                consider({ score = score, n = n, d = d, style = n.style })
            end
        end
        -- THE FREEZE: "Ice barrage/burst any clumps of Nylocas you will not be
        -- dealing with. Frozen nylocas cannot attack the pillars until
        -- unfrozen ... all colours can be frozen" (E :164).  A clump of three
        -- or more on a support that is being eaten, centred on a blue when one
        -- is in it (the blues die to it; the others are nulled for the raider:
        -- "they will only be removed from the arena when they explode", E :164).
        for _, n in ipairs(v.nylos) do
            local sp = n.support
            local d = dist(me.x, me.z, n.x, n.z, n.size)
            if sp ~= nil and sp.frac < 0.7 and not (sp == lowest and sp.frac < 0.15 and alive_supports > 1)
                and d <= P.reach.magic and (ny.frozen[n.slot] or -1) < v.tick
                and (ny.waves < P.flicker_wave - 1 or n.age >= P.settle_age) then
                local clump, blues = {}, 0
                for _, o in ipairs(v.nylos) do
                    if (ny.frozen[o.slot] or -1) < v.tick and (ny.doomed[o.slot] == nil or ny.doomed[o.slot] < v.tick)
                        and o.x <= n.x + 1 and o.x + o.size - 1 >= n.x - 1 and o.z <= n.z + 1 and o.z + o.size - 1 >= n.z - 1 then
                        clump[#clump + 1] = o
                        if o.style == "magic" then blues = blues + 1 end
                    end
                end
                if #clump >= 3 and (n.style == "magic" or blues == 0) then
                    consider({ score = -18 - 3 * #clump - (1 - sp.frac) * 10 + (n.style ~= "magic" and 2 or 0), n = n, d = d,
                        style = "magic", spell = "ice_burst", clump = clump })
                end
            end
        end
        if best ~= nil then
            local n = best.n
            pick = { slot = n.slot, style = best.style, symbol = n.symbol, vas = false, d = best.d, x = n.x, z = n.z, big = n.big,
                spell = best.spell, clump = best.clump }
        end
    end

    -- PRESS?  Melee and the bow swing on by themselves once pressed (wiki
    -- Attack speed); a spell is one cast a click.  A new copy is pressed as soon
    -- as the old one is doomed, so the next swing is queued on the cooldown.
    local press = false
    if pick ~= nil and intent.walk == nil then
        local speed = P.loadout[pick.style].speed
        if cur == nil or cur.slot ~= pick.slot or cur.style ~= pick.style then
            press = true
        elseif pick.style == "magic" then
            press = cur.swung ~= nil and cur.swung >= cur.pressed and v.tick >= cur.swung + speed - 2
        elseif v.tick - math.max(cur.pressed, cur.swung or -1000) > speed + 3 then
            press = true
        end
        -- HER TURN: the turn stops the attack, and a hit of the old colour that
        -- lands on or after it is reflected and heals her (W :754; NB :176;
        -- DMG :278).  A projectile that would land within a tick of the
        -- predicted turn is not sent; a bow already swinging is stopped by a
        -- step (the library's walk clears the engagement).
        if pick.vas and ny.next_turn ~= nil and pick.style ~= "melee" then
            local start = math.max(v.tick + 1, st.last_swing + speed)
            if cur ~= nil and cur.slot == pick.slot and not press then
                start = math.max(v.tick + 1, (cur.swung or cur.pressed) + speed)
            end
            local lands = start + flight(pick.style, pick.d)
            if start < ny.next_turn and lands >= ny.next_turn - 1 then
                press = false
                ny.holds = ny.holds + 1
                if pick.style == "ranged" and cur ~= nil and cur.slot == pick.slot then
                    local sx = me.x + 1
                    if not floor_ok(sx, me.z) then sx = me.x - 1 end
                    intent.walk = { x = sx, z = me.z }
                    ny.target = nil
                end
            end
        end
    end
    local early = nil
    if pray_style ~= nil and v.prayer > 0 and v.lit[P.prayer_of[pray_style]] ~= true then
        local name = P.prayer_of[pray_style]
        if ny.early_sent ~= nil and ny.early_sent.name == name and v.tick - ny.early_sent.tick <= 3 then
            -- pressed already and not read back yet (a prayer reads lit a tick
            -- or two after its press): a second press is a toggle, it would put
            -- it OUT again; treat it as lit until the read catches up
            for _, o in pairs(P.prayer_of) do v.lit[o] = (o == name) end
            intent.want = { [name] = true }
        else
            early = name
            ny.early_sent = { name = name, tick = v.tick }
        end
    end
    if press and ny.worn ~= pick.style then
        QD.raid._play_nylocas_wear(st, v, pick.style, early)
    elseif early ~= nil then
        QD.raid._play_nylocas_wear(st, v, nil, early)
    end
    -- sent already: the library's block must neither re-press it nor light the
    -- old one again (the want keeps only what is lit now)
    if early ~= nil then intent.want = { [early] = true } end
    if press then
        local r, d
        if ny.worn == pick.style then
            local also = {}
            if pick.style == "magic" then
                -- Ice Burst only on a clump that is ALL blue: "freezing non-magic
                -- Nylocas means you will no longer be able to do damage to them"
                -- (E :164); a lone blue gets Ice Rush
                local spell, pure = "ice_rush", true
                if pick.spell ~= nil then
                    spell = pick.spell
                    ny.freezes = (ny.freezes or 0) + 1
                    for _, o in ipairs(pick.clump) do
                        ny.frozen[o.slot] = v.tick + 16
                        if o.style ~= "magic" then ny.nulled[o.slot] = true else also[#also + 1] = o.slot end
                    end
                elseif not pick.vas then
                    for _, n in ipairs(v.nylos) do
                        if n.slot ~= pick.slot and n.x <= pick.x + 1 and n.x + n.size - 1 >= pick.x - 1 and n.z <= pick.z + 1 and n.z + n.size - 1 >= pick.z - 1 then
                            if n.style == "magic" then also[#also + 1] = n.slot else pure = false end
                        end
                    end
                    if pure and #also > 0 then spell = "ice_burst" ny.bursts = ny.bursts + 1 else also = {} end
                end
                r, d = QD.player.cast(spell, pick.symbol, 1, 2, { slot = pick.slot, quick = true })
                ny.casts = ny.casts + 1
            else
                r, d = QD.player.attack(pick.symbol, 2, 1, { slot = pick.slot, quick = true })
            end
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
            ny.presses = ny.presses + 1
            ny.results[tostring(r)] = (ny.results[tostring(r)] or 0) + 1
            ny.last_press = tostring(r) .. ":" .. string.sub(tostring(d), 1, 160)
            if r ~= "ok" and r ~= "timeout" and #st.lines < 6 then
                st.lines[#st.lines + 1] = "t" .. v.tick .. " press " .. pick.style .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 140)
            end
            ny.target = { slot = pick.slot, style = pick.style, symbol = pick.symbol, vas = pick.vas, pressed = v.tick, d = pick.d, also = also,
                colour = (not pick.vas and pick.spell == nil) and pick.style or nil }
            if r ~= "ok" and r ~= "timeout" then
                -- the press did not land on this copy (another one stands on its
                -- pixels: `covered`, or it is off the frame): it is passed over for
                -- a few ticks and the next tick presses another (ny30g t146-167:
                -- one covered copy re-pressed every 7 ticks, nothing else hit)
                ny.blocked = ny.blocked or {}
                ny.blocked[pick.slot] = v.tick + 3
                ny.target = nil
                ny.misses = (ny.misses or 0) + 1
            end
            if pick.vas then ny.vas_presses[#ny.vas_presses + 1] = { tick = v.tick, style = pick.style, form = vas.form } end
            st.engaged = true
            st.engaged_tick = v.tick
            st.walk_target = nil
        end
    end

    -- HOME: nothing to hit and off the centre -> back to it (E :162)
    if pick == nil and intent.walk == nil and vas == nil then
        local far = math.max(math.abs(me.x - home.x), math.abs(me.z - home.z))
        if far > 2 and not unsafe(home.x, home.z, 0)
            and (st.walk_target == nil or st.walk_target.x ~= home.x or st.walk_target.z ~= home.z) then
            intent.walk = { x = home.x, z = home.z }
            ny.homes = ny.homes + 1
        end
    end
    if intent.walk ~= nil and intent.walk.x == me.x and intent.walk.z == me.z then intent.walk = nil end

    -- SUPPLIES: the most that can land before the next chance to eat
    local function threat(h)
        local swings = math.ceil(h / N.cadence)
        -- a floor of one big's max hit twice over: a copy turns aggro (NT
        -- nylocas.aggro_swap) or a split lands next to the raider between reads
        local total = 2 * N.big_hit
        for _, n in ipairs(v.nylos) do
            local d = dist(me.x, me.z, n.x, n.z, n.size)
            -- a copy of the prayed colour still counts while the switch is in
            -- flight (the prayer is read lit a tick after the press)
            local prayed = n.style == pray_style and v.lit[P.prayer_of[n.style]] == true
            if n.fighting and not prayed and d <= ((n.style == "melee") and 2 or 9) then
                total = total + (n.big and N.big_hit or N.small_hit) * swings
            end
            local ea = n.big and P.explode_age_big or P.explode_age
            if n.age + h >= ea and n.age <= ea and d <= P.blast + 1 then total = total + N.explode end
        end
        -- her attack every 4 ticks (NT vasilias_attackrate): off prayer up to
        -- 24, and through the prayer of a magic or ranged form still up to
        -- 17 (NB :370-383, ^tob_vasilias_prayed_max; only melee is fully
        -- protected, W :752)
        if vas ~= nil and vas.form ~= "spawning" then
            if vas.form ~= pray_style then
                total = total + N.boss_hit * math.ceil(h / 4)
            elseif vas.form ~= "melee" then
                total = total + N.prayed_hit * math.ceil(h / 4)
            end
        end
        -- the interlude: "During this brief interlude, the team should heal up
        -- and boost" (W :750): all 31 waves out, none left, she has not landed
        if vas == nil and #v.nylos == 0 and ny.waves >= 31 then
            total = math.max(total, v.hp_base - 12)
        end
        -- a support's bar under a quarter: its collapse ("30+ damage", E :171)
        -- can land on any tick from here
        for _, s in ipairs(v.supports) do
            if s.alive and s.frac < 0.25 then total = total + N.collapse end
        end
        return total
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    -- a brew drains the attack stats; a super restore puts them back ("undo
    -- the brews' stat drain", tob_nylocas.lua :35; maiden's plan does the same
    -- for its bow): a 2-hitpoint nylocas missed is a nylocas left biting
    if intent.drink == nil and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY and (ny.stat_check or 0) <= v.tick then
        ny.stat_check = v.tick + 5
        local _, rg = QD.skill.read("ranged")
        local _, mg = QD.skill.read("magic")
        if (rg ~= nil and rg.level < 88) or (mg ~= nil and mg.level < 88) then
            for _, name in ipairs(QD.RAID_PLAY_RESTORES) do
                local cr, cnt = QD.inv.count(name)
                if intent.drink == nil and cr == "ok" and cnt > 0 then intent.drink = name end
            end
            if intent.drink ~= nil then ny.stat_restores = (ny.stat_restores or 0) + 1 end
        end
    end
    if P.trace then
        local nf, w = 0, ""
        if v.tick % 20 == 0 then
            local sp = {}
            for _, s in ipairs(v.supports) do sp[#sp + 1] = string.format("%d,%d:%.2f%s", s.x - O.x, s.z - O.z, s.frac, s.alive and "" or "x") end
            api_drive.report("nyplay supports t=" .. v.tick .. " " .. table.concat(sp, " ") .. " refusals " .. st.refusals .. " " .. table.concat(st.lines, " | "))
        end
        for _, n in ipairs(v.nylos) do
            if n.fighting then nf = nf + 1 end
            if #w < 200 then
                w = w .. string.format(" %s%s%s@%d,%d/a%d", n.fighting and "F" or "i", n.big and "B" or "", string.sub(n.style, 1, 2), n.x - O.x, n.z - O.z, n.age)
            end
        end
        api_drive.report(string.format("nyplay t=%d me=%d,%d hp=%d n=%d f=%d waves=%d worn=%s pray=%s pick=%s press=%s walk=%s eat=%s drink=%s |%s",
            v.tick, me.x - O.x, me.z - O.z, v.hp, #v.nylos, nf, ny.waves, ny.worn, tostring(pray_style),
            pick and (pick.style .. "/" .. pick.slot .. "/d" .. pick.d) or "none", tostring(press),
            intent.walk and (intent.walk.x - O.x .. "," .. intent.walk.z - O.z) or "-", tostring(intent.eat), tostring(intent.drink), w .. (press and (" PRESS " .. tostring(ny.last_press)) or "")))
    end
    return intent
end
