-- quest-driver / raid_play_tob_verzik: the Verzik Vitur plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files).
-- Written by raid seam30 play_tob_verzik from the sources, each decision with
-- its line; PLAY_NOTES.md "Verzik" is its strategy table.
--
-- Sources (short names used below):
--   W   docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Strategies.wikitext
--   ET  docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md
--   V   docs/minigames/theater_of_blood/encounters/verzik.tsv (our spec)
--   K   test/raids/tob_verzik.lua (the kept room: its tiles and kit, measured)
-- The owner's rulings bind it: the urnbomb reads Protect from Missiles at its
-- LANDING (V verzik.p2_bomb_prayer_read_tick), and P3's autos read the
-- protection ON HIT (V verzik.p3_prayer_read, Blert-sourced).
--
-- What the plan SEES (a person at the screen): her form and animation, her
-- health bar, the projectiles in flight and the tile each is falling on
-- (QD.world.projectiles), the floor graphics (QD.world.spotanims), the npcs in
-- the room, its own hitpoints, prayer and tile, and its own swings (the
-- library's player_anim read).  Nothing else.

-- The weapons the room is played with (the kept kit, K:12-27), each with its
-- attack speed and the swing the player sees.  Fists: "a 4-tick punch" (K:11,
-- seq 422 measured in build/quest_gate/tob_verzik t50-62).  Dawnbringer: seq
-- 1167 every 4 ticks (the same run, t85/89/93).  Twisted bow: speed 6, one
-- less on rapid (wiki Twisted bow; K p2.rapid), seq 426.
QD.RAID_PLAY_VERZIK_WEAPONS = {
    fists = { speed = 4, seqs = { [422] = true } },
    dawnbringer = { item = "verzik_special_weapon", speed = 4, seqs = { [1167] = true } },
    bow_accurate = { item = "twisted_bow", speed = 6, seqs = { [426] = true } },
    bow_rapid = { item = "twisted_bow", speed = 5, seqs = { [426] = true } },
    -- raid seam34v (Normal trio): the P1 melee weapon the wiki recommends
    -- first for the capped shield (W:891 "weapon1 = Scythe of vitur"; W:881
    -- "weapons that hit several times are recommended"), speed 5 and swing
    -- seq 8056 as raid_play.lua's QD.RAID_PLAY_WEAPONS row
    scythe = { item = "scythe_of_vitur", speed = 5, seqs = { [8056] = true } },
    -- owner_verzik (the SLOW pace): the noxious halberd, speed 5 and reach 2
    -- (all.obj [noxious_halberd] attackrate 5, weapon_attackrange 2), swing
    -- seqs 428 human_spear_spike / 440 human_scythe_sweep (attack_anims_modern.obj;
    -- Blert attack_definitions.json NOXIOUS_HALBERD animationIds 428, 440)
    halberd = { item = "noxious_halberd", speed = 5, seqs = { [428] = true, [440] = true } },
    -- owner_verzik (the FAST pace's enrage dump): dragon claws, speed 4, the
    -- scratch 393/1067 and the special 7514 (Blert attack_definitions.json
    -- CLAW_SCRATCH / CLAW_SPEC; special_attack.obj [dragon_claws] sa_energy 500)
    claws = { item = "dragon_claws", speed = 4, seqs = { [393] = true, [1067] = true, [7514] = true } },
}

-- owner_verzik 2026-10-07: THE PACE.  t.raid.verzik_pace = "slow" before
-- t.raid.play plays the team that reaches her green ball.  Of the 27 Blert
-- Normal trio rooms (build/blert/verzik) five reach the ball (P3 197-291
-- ticks, the ball at P3+180..193); 0f9abe1a (no deaths, ball at P3+187) and
-- 85b10c82 are one team: the leader on the scythe and the other two on the
-- NOXIOUS HALBERD in every phase (P3: scythe 30/27 swings, halberds 30/21 and
-- 28/24, 23 and 13 of the halberds' 52 swings from distance 2).  Every other
-- room is three scythes and ends before the ball (P3 122-194).  So the slow
-- pace is that team's weapons, nothing held back: roles 2 and 3 fight P3
-- with the halberd (P1 and P2 with the scythe: the halberd seats' P2 in the
-- whole-room survey of 2026-10-07 starved -- 47 walk blocks unconfirmed beside
-- her 3x3, every fish gone, both dead at t441 on five names -- and P3 is the
-- phase the pace is for).  "fast" (nil) is the three-scythe team.
QD.raid.verzik_pace = QD.raid.verzik_pace
QD.RAID_PLAY_VERZIK_HALBERD_ROLES = { [2] = true, [3] = true }

QD.raid._play_plan("tob_verzik", {
    room = "verzik",
    boss = { entry = "verzik_phase1_story", normal = "verzik_phase1", hard = "verzik_phase1_hard" },
    -- her forms in order (V verzik.av.npc_form_entry 10830..10836; the npc
    -- configs tob_verzik.npc :54-145): the plan follows the one npc row
    -- through every retype.
    forms = {
        entry = {
            { "verzik_initial_story", "pre" }, { "verzik_phase1_story", "p1" },
            { "verzik_phase1_to2_transition_story", "t12" }, { "verzik_phase2_story", "p2" },
            { "verzik_phase2_to3_transition_story", "t23" }, { "verzik_phase3_story", "p3" },
        },
        -- raid seam34v: V verzik.av.npc_form_normal 8369..8374
        normal = {
            { "verzik_initial", "pre" }, { "verzik_phase1", "p1" },
            { "verzik_phase1_to2_transition", "t12" }, { "verzik_phase2", "p2" },
            { "verzik_phase2_to3_transition", "t23" }, { "verzik_phase3", "p3" },
        },
    },
    -- the adds, by what a person tells apart on the floor (tob_verzik.npc)
    adds = {
        entry = {
            crab = { "verzik_nylocas_melee_story", "verzik_nylocas_ranged_story", "verzik_nylocas_magic_story" },
            purple = { "tob_verzik_phase2_armourednylocas_story" },
            red = { "tob_verzik_phase2_bloodnylocas_story" },
            web = { "verzik_web_npc" },
        },
        -- raid seam34v: the Normal records (V verzik.av.adds_nylocas_forms
        -- 8381-8383, av.athanatos.npc 8384, av.reds.npc 8385, av.web.npc 8376;
        -- names from configs/all.npc.compack)
        normal = {
            crab = { "verzik_nylocas_melee", "verzik_nylocas_ranged", "verzik_nylocas_magic" },
            purple = { "tob_verzik_phase2_armourednylocas" },
            red = { "tob_verzik_phase2_bloodnylocas" },
            web = { "verzik_web_npc" },
        },
    },
    -- no symbol is attacked for these, they are only seen: the tornado is
    -- 10846 (tob_verzik.npc:8 "the tornado - 10841-10846"), a standing pillar
    -- 8379 (K spec.verzik.pillar_count "changed form 8379 to 8377")
    tornado_id = 10846, pillar_id = 8379,
    -- raid seam34v: the tornado per mode (V verzik.av.tornado.npc: Normal 8386)
    tornado_ids = { entry = 10846, normal = 8386 },
    -- P1 (V; ET 1.2): wind-up seq 8109 every 14 ticks (grade B), the bolt
    -- 1580 leaves 3 ticks into it, lands 3 later; cover and the prayer are
    -- settled on the launch tick (V verzik.p1_verdict_tick).  8111: the P1
    -- form dies, and every pillar falls on whoever is within 2 (V
    -- verzik.pillar_collapse_range; W:879 "players should stay away from them").
    p1_windup = 8109, p1_cadence = 14, p1_launch = 3, p1_flight = 3, p1_death = 8111,
    p1_bolt_proj = 1580,                   -- the bolt (V p1; cache_spotanim.txt)
    p1_safe_swings = 4,                    -- W:885 "safely attack four times with a 4-tick weapon"
    p1_fist_swings = 10,                   -- the cap row's sample (K tech.p1_cap_melee_ranged; triage: "lands enough to see it")
    p1_bow_swings = 2,                     -- the ranged half of the same row (the cap is 3: W:873)
    p1_specs = 2,                          -- W:875 Dawnbringer special 75-150; K's two presses at 100% energy
    pillar_reach = 2,                      -- V verzik.pillar_collapse_range
    -- P2 (V): 8114 cast, 8116 slam/stomp, every 4 ticks; her 3x3 is hard.
    p2_cast = 8114, p2_slam = 8116, p2_reds = 8117, p2_death = 8118, p2_absorb = 5,
    -- the reds clock (V verzik.p2_cadence 4, verzik.reds_attacks_between 7,
    -- both B) and the summon animation 8117's length (V
    -- verzik.av.reds_summon.seq: 10.00 ticks)
    p2_cadence = 4, p2_attacks_between = 7, p2_reds_anim = 10,
    bomb_proj = 1583, zap_proj = 1585, purple_proj = 1586, blood_proj = 1591,
    -- P3 (V): autos every 7 (5 enraged); 8125 ranged (1593) or the green ball
    -- (1598), 8124 magic (1594), 8123 melee; specials 14406 crabs, 8127 webs
    -- (1601), 8126 yellows (pools 1595).
    p3_ranged = 8125, p3_magic = 8124, p3_melee = 8123, p3_crabs = 14406, p3_webs = 8127, p3_yellows = 8126,
    p3_ranged_proj = 1593, p3_magic_proj = 1594, ball_proj = 1598, web_proj = 1601, pool_gfx = 1595,
    p3_cadence = 7, p3_enraged_cadence = 5,
    -- raid seam45 (the Normal trio's melee clock): the first attack after a
    -- summon (V verzik.reds_first_attack_after 12, B), and her gaps after a
    -- P3 special, each a tick or two early of what svaplayverzi measured
    -- (crabs 10, webs 42, yellows 21, the ball 12; QD.raid._verzik_p3_clock):
    -- a raider beside her stays out from two before until her attack shows
    -- the ticks after a summon the Matomenos are swung at (W:928 "until this
    -- animation ends": p2_reds_anim; measured s45 e20/e21/e22 on five names:
    -- the 10-tick window P2 270-310, every red killed 307-350, 5 ticks 259-335)
    p2_reds_window = 10,
    -- raid seam52 play_tob_verzik_last: the reds as Blert's trios treat them
    -- (seam52 verzik/refreds.py, refheal.py over the 20 rooms of
    -- verzik_normal_3.json): a summon that is NOT her last of P2 is swung at
    -- 6-11 times by the trio, spread over both reds from +1 to +39 (red end
    -- hitpoints 0-66 on the first summon, absorbed ~45 a summon); the LAST
    -- summon's reds are never absorbed (P3 comes first) and get 0-2 swings a
    -- raider.  So: a non-last summon, each red swung at until it dies or
    -- p2_reds_kill_window ticks pass, the third raider on the red with more
    -- left; the last one, only inside the absorb window (no swing on her
    -- there anyway).  Last = her bar at the summon is at most
    -- p2_reds_last_frac of what she lost since the summon before.
    -- (and, raid seam52 e2: or her P2 at most p2_reds_last_pct, what the trio
    -- takes off her in a summon's cycle with no swing on a red: e2 sva's
    -- fourth summon came at 6 % after a cycle of 7 % spent half on reds, and
    -- killing its reds put her first hit 25 ticks after it)
    p2_reds_kill_window = 20, p2_reds_last_frac = 0.9, p2_reds_last_pct = 14,
    p2_reds_first = 12, p3_gap_crabs = 10, p3_gap_webs = 40, p3_gap_yellows = 20, p3_gap_ball = 12,
    pool_life = 14,                        -- V verzik.p3_yellow_pool_lifetime (B)
    -- run from a tornado nearer than this (its walk: one tile a tick, s30 vz30h)
    tornado_run = 5,
    -- the enraged hitpoints band's floor (W:981 "keep health around 50-60")
    enrage_hp_floor = 45,
    -- the floor, local to the room's 64x64 square (K's tiles: 6421..6442 x
    -- 78..101 in square 6400,64, build/quest_gate/tob_verzik player_tile rows)
    floor = { 22, 15, 41, 34 },
    -- the kept P1 cover tile (K HIDE[1] 6426,93: the pillar south-west of
    -- her, W:887 "hide behind the pillar directly south-west of Verzik"),
    -- used when the pillar row is not in view
    hide = { 26, 29 },
    modes = {
        -- V entry: bolt 60 (30 prayed), urnbomb 16 (8 prayed), zap 48 (40%
        -- off with insulated boots: K:16), P3 auto 20 (10 prayed), melee 36,
        -- the green ball 75% of the Hitpoints level, yellows 80.
        entry = { bolt = 30, bomb = 8, zap = 29, crab = 26, blood = 23, auto = 10, melee = 36, ball = 74, blast = 80, tornado_pct = 50 },
        -- raid seam34v, V normal: bolt 137 (68 prayed, V p1_max_hit), urnbomb
        -- 44 (22 prayed, p2_bomb_max), zap 48 (25 with insulated boots,
        -- p2_zap_max), a nylocas 46 near (W:921), the blood spell 45 (22
        -- prayed, p2_heal_spell_max), P3 auto 33-34 (16-17 prayed,
        -- p3_auto_max), melee 63 (p3_melee_max), the ball 74, the blast 80
        normal = { bolt = 68, bomb = 22, zap = 25, crab = 46, blood = 22, auto = 17, melee = 63, ball = 74, blast = 80, tornado_pct = 50 },
    },
    -- ONE protection in the list, the one wanted this tick (decide writes
    -- slot 1): the protections exclude each other and a press is a toggle, so
    -- an "off" for the old one after the new one's "on" lights the old one
    -- again (the nylocas and sotetseg fixers' finding, seam30; s30 vz30g: 23
    -- blood spells landed under Missiles while the plan wanted Magic)
    walk_prayers = { "protectfrommagic", "piety", "rigour" },
    down_prayers = {},
    decide = "_play_verzik_decide",
})

-- ==========================================================================
-- SEE: the room as a person reads it this tick (her form and animation, the
-- adds, the pillars, what is in the air and on the floor).
-- ==========================================================================
function QD.raid._verzik_ids(st)
    local P = st.plan
    local ids = { form = {}, crab = {}, purple = {}, red = {}, web = {} }
    for _, f in ipairs(P.forms[st.mode]) do
        local sr, id = api_drive.symbol("npc", f[1])
        if sr == "ok" then ids.form[id] = { symbol = f[1], phase = f[2] } end
    end
    for kind, list in pairs(P.adds[st.mode]) do
        for _, sym in ipairs(list) do
            local sr, id = api_drive.symbol("npc", sym)
            if sr == "ok" then ids[kind][id] = sym end
        end
    end
    return ids
end

-- Chebyshev distance from a tile to an npc's footprint (0 = under it).
function QD.raid._verzik_dist(x, z, row)
    local n = row.size or 1
    local dx = math.max(row.x - x, x - (row.x + n - 1), 0)
    local dz = math.max(row.z - z, z - (row.z + n - 1), 0)
    return math.max(dx, dz)
end

function QD.raid._verzik_see(st, v)
    local P, vz = st.plan, st.vz
    v.crabs, v.purples, v.reds, v.webs, v.tornadoes, v.pillars = {}, {}, {}, {}, {}, {}
    v.boss = nil
    v.phase = nil
    local nr, rows = api_drive.npcs(0)
    if nr == "ok" then
        for _, row in ipairs(rows) do
            local id = row.npc_id
            local alive = row.health_ratio == nil or row.health_ratio ~= 0
            local f = vz.ids.form[id] or vz.ids.form[row.base_npc_id]
            -- one Verzik in the room; the client gives a new form a new row
            -- (s30 vz30b: P1 slot 68, P2 slot 78), so the form is the key
            if f ~= nil then
                vz.boss_slot = row.slot
                v.boss = row
                v.phase = f.phase
                -- her form changed: the library's reads and presses follow it
                if st.boss_symbol ~= f.symbol then
                    st.boss_symbol = f.symbol
                    vz.forms[#vz.forms + 1] = { tick = v.tick, symbol = f.symbol }
                end
            elseif vz.ids.crab[id] then
                -- raid seam51: a DYING nylocas too, until its row goes: its
                -- blast comes 4 ticks after its death (e2 sva: 8382 slots
                -- 1082/1081 npc_death t201/t210, blasts t205/t214 for 63 and
                -- 55 on the same raider, who had stopped running from it the
                -- tick its bar read 0; npc_free t206/t215)
                v.crabs[#v.crabs + 1] = { row = row, symbol = vz.ids.crab[id], dead = not alive }
            elseif alive and vz.ids.purple[id] then
                v.purples[#v.purples + 1] = { row = row, symbol = vz.ids.purple[id] }
            elseif alive and vz.ids.red[id] then
                v.reds[#v.reds + 1] = { row = row, symbol = vz.ids.red[id] }
            elseif vz.ids.web[id] then
                v.webs[#v.webs + 1] = { row = row, symbol = vz.ids.web[id] }
            elseif id == (P.tornado_ids[st.mode] or P.tornado_id) or row.base_npc_id == (P.tornado_ids[st.mode] or P.tornado_id) then
                v.tornadoes[#v.tornadoes + 1] = row
                vz.tornado_seen = (vz.tornado_seen or 0) + 1
            elseif id == P.pillar_id then
                v.pillars[#v.pillars + 1] = row
            end
        end
    end
    -- (raid seam51: the dying ones in P2 only, where their blasts were the
    -- intake -- e2 sva P2 crab blasts 194; in P3 8 -- and in P3 the e3 run
    -- that kept them ran from them for 8-9 ticks after the crabs special)
    if v.phase ~= "p2" then
        local live = {}
        for _, c in ipairs(v.crabs) do if not c.dead then live[#live + 1] = c end end
        v.crabs = live
    end
    -- her attack this tick: a new seq on her row (seq_tick is the client's
    -- tick; the server tick it started is v.tick minus its age, as Bloat's)
    v.attack = nil
    local b = v.boss
    if b ~= nil and b.seq_id ~= nil and b.seq_id >= 0 and b.seq_tick ~= nil and b.seq_tick ~= vz.last_seq_tick then
        vz.last_seq_tick = b.seq_tick
        v.attack = { seq = b.seq_id, tick = v.tick - math.max(0, v.api_now - b.seq_tick), seen = v.tick }
        -- raid seam51 play_tob_verzik_whole: the tick her attack is dated to
        -- in the Normal trio's P2 and P3 clocks is the plan tick that FIRST
        -- SAW the seq, not the row's age: the age dated the leader's every P2
        -- attack one early (seam50, s49 head_party4 sva: her 8114 true at
        -- 177,181,185,189,193,197 in the tick log, the leader's age-dated
        -- 176,180,...) and the members' half the time, while the seen tick
        -- equalled the tick log's in every sample.  P1 and Entry keep `tick`.
        v.attack.at = st.mode == "normal" and v.tick or v.attack.tick
        vz.attacks[#vz.attacks + 1] = v.attack
    end
    -- what is in the air: each projectile by the tile it falls on
    v.proj = {}
    v.shadows = {}
    local pr, projs = QD.world.projectiles(0)
    if pr == "ok" then
        for _, p in ipairs(projs) do
          -- only what is still in flight (a landed projectile can linger in
          -- the client's list with no cycles left)
          if p.cycles_left == nil or p.cycles_left > 0 then
            v.proj[#v.proj + 1] = p
            local id = p.spotanim_id
            -- a tile an urnbomb, an Athanatos or a web falls on is not stood on
            -- (W:909 "dodged by simply avoiding the tile the urn was thrown at";
            -- K "an Athanatos aimed at a tile lands 6 ticks later on whoever
            -- stands there"; W:957 webs "launch webs towards players")
            if id == P.bomb_proj or id == P.purple_proj or id == P.web_proj then
                v.shadows[p.dst_x * 100000 + p.dst_z] = true
            end
          end
        end
    end
    -- on the floor: the yellow pools (W:969 "each player must stand on a
    -- different yellow pool")
    v.pools = {}
    local sr, spots = QD.world.spotanims(0)
    if sr == "ok" then
        for _, s in ipairs(spots) do
            if s.spotanim_id == P.pool_gfx and (s.cycles_left == nil or s.cycles_left > 0) then v.pools[#v.pools + 1] = { x = s.x, z = s.z } end
        end
    end
    -- a pool lasts 14 ticks (V verzik.p3_yellow_pool_lifetime, B) from the
    -- first one seen; a graphic the client still lists after that is not a
    -- pool (s31 vz31a: the plan stood on 6424,90 from the yellows at t488 to
    -- its death at t539 and never ran from either tornado)
    if #v.pools > 0 and not vz.pools_prev then vz.pool_first = v.tick end
    vz.pools_prev = #v.pools > 0
    if vz.pool_first ~= nil and v.tick > vz.pool_first + P.pool_life then v.pools = {} end
    -- a web is a hard tile, and so is her body in P2 and P3 (W:957 "she turns
    -- into a hard NPC"; her 3x3 in P2, K 6431..6433 x 89..91)
    for _, w in ipairs(v.webs) do v.shadows[w.row.x * 100000 + w.row.z] = true end
end

-- the floor a raider may stand on: the room, off her body, off a standing
-- pillar (P1), never within a pillar's fall while she is dying (W:879)
function QD.raid._verzik_floor(st, v)
    local O, F = st.origin, st.plan.floor
    local b = v.boss
    return function(x, z)
        if x < O.x + F[1] or x > O.x + F[3] or z < O.z + F[2] or z > O.z + F[4] then return false end
        if b ~= nil and QD.raid._verzik_dist(x, z, b) == 0 then return false end
        for _, p in ipairs(v.pillars) do
            local d = QD.raid._verzik_dist(x, z, p)
            if d == 0 then return false end
            if st.vz.dying and d <= st.plan.pillar_reach then return false end
        end
        return true
    end
end

-- A loadout swap and its drinks, one block in one tick (PLAY_NOTES
-- "Loadouts"; the library's SEND has no gear list: raid_play_tob_maiden's
-- block, the same shape), counted into the record like the library's own.
function QD.raid._verzik_block(st, v, label, items, drinks)
    local n = #items + #drinks
    local r, d = QD.together(function()
        for _, item in ipairs(drinks) do QD.player.drink(item) end
        for _, item in ipairs(items) do QD.player.equip(item) end
    end)
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + n
    st.blocks[r] = (st.blocks[r] or 0) + 1
    if #drinks > 0 then
        st.last_drink = v.tick
        for _, item in ipairs(drinks) do st.drinks[#st.drinks + 1] = { tick = v.tick, item = item, hp = v.hp, prayer = v.prayer } end
    end
    if r ~= "ok" and r ~= "split" then
        st.refusals = st.refusals + 1
        if #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " " .. label .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 160) end
    end
    st.vz.swaps[#st.vz.swaps + 1] = { tick = v.tick, label = label, result = r }
    return r
end

-- An attack press on an add (the library's press goes to her only): the same
-- bookkeeping as raid_play.lua's SEND.
-- The engagement events this plan raises itself (presses and moves it sends
-- outside the executor, and weapon swaps).  The Normal plan has opted in to the
-- executor's raider_engage machine (st.engage_owned); the Entry plan has not, and
-- keeps the flag exactly as it was.
function QD.raid._verzik_engage(st, v, name, target)
    assert(st, "_verzik_engage: st")
    assert(v, "_verzik_engage: v")
    if st.engage_owned then return QD.raid._engage_event(st, v, name, target) end
    if name == "engaged" then
        st.engaged, st.engaged_tick = true, v.tick
    else
        st.engaged = false
    end
end

function QD.raid._verzik_press_add(st, v, add)
    local ar = QD.player.attack(add.symbol, 2, 1, { quick = true, slot = add.row.slot })
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
    st.attack_presses = (st.attack_presses or 0) + 1
    st.vz.add_presses = st.vz.add_presses + 1
    if ar == "ok" then
        -- a press sent outside the executor: the engagement machine hears it here
        QD.raid._verzik_engage(st, v, "engaged", "slot:" .. tostring(add.row.slot))
        st.walk_target = nil
        st.vz.target_slot = add.row.slot
    elseif #st.lines < 6 then
        st.lines[#st.lines + 1] = "t" .. v.tick .. " add attack " .. tostring(ar)
    end
end

-- The Dawnbringer's special: the combat tab's bar, then the press (K:1093-1098,
-- the same widget; a slow verb, so it goes out on its own, not in a block).
function QD.raid._verzik_special(st, v)
    QD.ui.tab("combat")
    QD.ticks(1)
    local wr, wid = QD.ui.widget("combat_interface:special_attack")
    local ir = "no widget"
    if wr == "ok" then ir = QD.ui.invoke(wid, 1) end
    local ar = QD.player.attack(st.boss_symbol, 2, 3, { quick = true })
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 2
    st.attack_presses = (st.attack_presses or 0) + 1
    st.vz.specs[#st.vz.specs + 1] = { tick = v.tick, bar = tostring(ir), press = tostring(ar) }
    if ar == "ok" then
        QD.raid._verzik_engage(st, v, "engaged", "boss")
        st.walk_target = nil
        st.vz.target_slot = nil
    end
end

-- The bow's rapid style (K p2.rapid: style slot 1, varp43_com_mode 1).
function QD.raid._verzik_rapid(st, v)
    QD.ui.tab("combat")
    QD.ticks(1)
    local sr, sw = QD.ui.widget("combat_interface:style_slot_1")
    local pr = "no widget"
    if sr == "ok" then pr = QD.ui.invoke(sw, 1) end
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
    st.vz.rapid = tostring(pr)
end

-- THE NYLOCAS (P2 and P3, W:925 and W:948 "identical to phase 2").  A
-- nylocas blasts everyone within 3 when it arrives OR dies (the server's
-- ~tob_verzik_crab_blast on [ai_queue3], tob.constant :2504-2509: 63/26/8 by
-- band, range 3), so it is shot only from 4 or more, and only the colour the
-- bow kills (tob_verzik.rs2: "the colour decides what kills a crab"); one
-- within 3 is run from (W:925 "running away from them"; K kited the first
-- round her body).  Returns the crab to shoot, or nil; `go` sends the run.
function QD.raid._verzik_crabs(st, v, ok, go)
    local me = v.me
    local shoot_crab, near_crab = nil, nil
    for _, c in ipairs(v.crabs) do
        local d = math.max(math.abs(c.row.x - me.x), math.abs(c.row.z - me.z))
        if d <= 3 and (near_crab == nil or d < near_crab.d) then near_crab = { row = c.row, d = d } end
        if not c.dead and string.find(c.symbol, "verzik_nylocas_ranged", 1, true) == 1 and d >= 4 then shoot_crab = c end
    end
    if near_crab ~= nil then
        local best, bx, bz = nil, me.x, me.z
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if ok(x, z) and not v.shadows[x * 100000 + z] then
                    local d = math.max(math.abs(x - near_crab.row.x), math.abs(z - near_crab.row.z))
                    if best == nil or d > best then best, bx, bz = d, x, z end
                end
            end
        end
        if best ~= nil and best > near_crab.d then
            go(bx, bz)
            st.vz.kites = (st.vz.kites or 0) + 1
            return nil
        end
    end
    return shoot_crab
end

-- ==========================================================================
-- raid seam34v play_tob_verzik_normal: P1 FOR THE NORMAL TRIO.
-- ==========================================================================
-- THE COVER TILE.  Of the standing pillars, the one whose shadow is nearest
-- her, and in that shadow the tile nearest her that is outside the pillar's
-- fall.  The shadow is the content's (tob_verzik.rs2
-- ~tob_verzik_behind_pillar, after Near Reality's SupportingPillar.kt): the
-- 4x4 box x-3..x, z-3..z off a WEST pillar's south-west tile (local x 25,
-- tob.constant ^tob_verzik_pillar_west_lx), the 3x3 box x+2..x+4, z-2..z off
-- an east one.  The fall is ^tob_verzik_pillar_collapse_range 2 from the
-- pillar's centre (foot + 1; W:877 "dealing heavy damage to anyone next to it
-- when it collapses"), so a tile 3 or more from the centre is never caught by
-- it, however many bolts the pillar has taken.  W:885 "The team should hide
-- behind the pillar directly south-west of Verzik, then move east once the
-- right one collapses": the nearest shadow first, the next one when it falls.
--
-- A pillar falls only when a bolt takes its last hitpoints, and a bolt is
-- launched only at a pillar a raider hides behind (W:877 "As long as one
-- player hides behind the pillar before the attack is launched, it will take
-- damage").  Its health bar shows once it is hit (185, 40-60 a bolt: V
-- verzik.pillar_hp, pillar_hit), so a person sees which pillar the next bolt
-- may bring down.  A pillar with more than 60 left cannot fall this bolt:
-- every tile of its shadow is cover, the two loose tiles nearer her too.  One
-- that may fall is hidden behind only from outside its reach, and scores a
-- tile worse, so the trio moves to a whole pillar first (W:885 "then move
-- east once the right one collapses").
-- How far from her a pillar's shadow is still worth walking to: past the near
-- row her bolts are tanked under Protect from Magic instead (W:887 "even tank
-- her attacks entirely, to avoid losing out on ticks").  Read inside the choice
-- AND by the caller, which is the whole point -- see the note in _verzik_cover.
-- THE EXPLODING NYLOCAS, as two declared machines (owner 2026-10-07: "You
-- need to add state machines for avoiding nylocas (they explode).  They follow
-- a player and explode.  The lua scripts can find which nylocas are following
-- them by looking at the face entity facet of the npcs.")
--
-- Content (tob_verzik.rs2 "The exploding nylocas"; tob.constant
-- ^tob_verzik_p2_nylo_*), shared by P2 and P3:
--   * ONE PER RAIDER, each locked on its own player for its life: its npc row's
--     `facing` is 32768 + that player's pid (entity_facets.h
--     WORLD_FACING_PLAYER_BASE), the same id space _verzik_is_tank reads.
--   * It walks to that player and detonates on ARRIVAL, npc_range <= 1
--     (_contact_range), or dies on its own after 25 ticks (_lifetime), or is
--     killed -- and every ending is the same blast at its 2x2 body, graded on
--     npc_range: <= 1 up to 63, 2 up to 26, 3 up to 8, 4+ nothing (_blast_*).
-- So avoiding one is three facts: MY crab must not arrive; it must not go off
-- beside a mate (it trails me, so I keep my mates out of its band); and nobody
-- else's crab may go off within 3 of me.
QD.RAID_PLAY_VERZIK_NYLO = {
    size = 2, contact = 1, life = 25, reach = 3,
    band = { [0] = 63, [1] = 63, [2] = 26, [3] = 8 },
    warn = 5,     -- ticks before its lifetime ends that MY crab's band counts too
}

-- Chebyshev gap from a tile to a crab's 2x2 body (npc_range's own measure).
function QD.raid._verzik_nylo_gap(x, z, row)
    local n = QD.RAID_PLAY_VERZIK_NYLO.size
    local gx = (x < row.x) and (row.x - x) or ((x > row.x + n - 1) and (x - row.x - n + 1) or 0)
    local gz = (z < row.z) and (row.z - z) or ((z > row.z + n - 1) and (z - row.z - n + 1) or 0)
    return math.max(gx, gz)
end

-- ONE CRAB: whose it is.  An instance per crab slot.  A slot outlives its crab
-- (the next rotation's crabs reuse slots), so GONE takes a fresh row back to
-- SPAWNED rather than being terminal.
local function nylo_lock(c, ev)
    if ev.owner == nil then return nil, "SPAWNED" end
    if ev.owner == c.my_pid then return nil, "HUNTING_ME" end
    return nil, "HUNTING_MATE"
end
local function nylo_gone() return nil, "GONE" end
local function nylo_dying() return nil, "DYING" end
-- DYING (2026-10-07, svbvzfastp3 P3+243..246): every ending -- arrived, timed
-- out or killed -- reaches the blast through the death sequence, so the blast
-- lands about three ticks AFTER the row reads dead (all three crabs died at
-- P3+243, the blast hit at +246).  This read a dead row as GONE, the seat
-- stopped avoiding it, walked back beside the corpse and took 62.  A dying crab
-- is everyone's hazard, whoever it hunted, until its row is freed.
QD.raid.sm_declare("verzik_nylo", {
    start = "SPAWNED",
    states = {
        SPAWNED      = { note = "on the floor, not yet facing anyone",
            on = { nylo_row = nylo_lock, nylo_dying = nylo_dying, nylo_gone = nylo_gone } },
        HUNTING_ME   = { note = "locked on me: it must not arrive",
            on = { nylo_row = nylo_lock, nylo_dying = nylo_dying, nylo_gone = nylo_gone } },
        HUNTING_MATE = { note = "locked on a mate: its blast must not reach me",
            on = { nylo_row = nylo_lock, nylo_dying = nylo_dying, nylo_gone = nylo_gone } },
        DYING        = { note = "dead, its blast still to land: nobody within its band",
            on = { nylo_dying = nylo_dying, nylo_gone = nylo_gone } },
        GONE         = { note = "blown and freed",
            on = { nylo_row = function() return nil, "SPAWNED" end } },
    },
})

-- ONE CRAB'S READING: every crab through its own `verzik_nylo` instance.
-- Returns the live crabs as { row, state, age }.
function QD.raid._verzik_nylo_read(st, v)
    assert(st, "_verzik_nylo_read: st")
    assert(v, "_verzik_nylo_read: v")
    local vz = st.vz
    -- once a tick: every caller reads the same steps (a second read would
    -- step each crab's machine twice)
    if vz.nylo_tick == v.tick then return vz.nylo_live end
    vz.nylo = vz.nylo or {}
    local live, seen = {}, {}
    for _, c in ipairs(v.crabs) do
        local slot = c.row.slot
        seen[slot] = true
        local rec = vz.nylo[slot]
        if rec == nil or rec.gone then rec = { first = v.tick } vz.nylo[slot] = rec end
        if c.row.facing ~= nil and c.row.facing >= 32768 then rec.owner = c.row.facing - 32768 end
        local ctx = { my_pid = st.my_pid }
        if c.dead then
            -- still a hazard until the row is freed: the blast is to come
            local m = QD.raid.sm_run(st, v, "verzik_nylo", ctx, { { name = "nylo_dying" } }, slot)
            live[#live + 1] = { row = c.row, state = m.state, age = v.tick - rec.first }
        else
            local m = QD.raid.sm_run(st, v, "verzik_nylo", ctx, { { name = "nylo_row", owner = rec.owner } }, slot)
            live[#live + 1] = { row = c.row, state = m.state, age = v.tick - rec.first }
        end
    end
    for slot, rec in pairs(vz.nylo) do
        if not seen[slot] and not rec.gone then
            rec.gone = true
            QD.raid.sm_run(st, v, "verzik_nylo", { my_pid = st.my_pid }, { { name = "nylo_gone" } }, slot)
        end
    end
    vz.nylo_tick, vz.nylo_live = v.tick, live
    return live
end

-- THE SORTIE: getting a swing in from inside any movement state (owner
-- 2026-10-07: "The problem with these state machines as they are is that they
-- seem to preclude attacking - you need an algorithm to identify when, in these
-- state machines, it is safe to get an attack in - for example, while running
-- and the attack timer is up, you can run 3 tiles to verzik to attack her,
-- attack, and then return to the statemachine.")
--
-- A movement state (avoid's WATCH, the ball's GATHER) asks this FIRST.  When
-- the weapon comes ready within reach of a short run, it looks for a STRIKE
-- tile: in reach of her body, at most three ticks' run away (two tiles a
-- tick), where the swing can be taken on the tick the weapon is ready with at
-- most one tick of waiting, safe on that tick, and -- when the state has a
-- goal and a deadline -- from which the goal is still reachable by the
-- deadline after the swing.  The cheapest such tile (run + wait + way back)
-- wins; with none, the state runs as before.
--   IDLE    no swing worth taking: the state has the tick.
--   GO      on the way to the strike tile; nothing pressed.
--   STRIKE  on it with the weapon ready: the attack is pressed.  An attack in
--           reach does not move the raider, and the next tick the state takes
--           over from wherever it stands.
-- Safety, read on the swing tick: no tornado can reach the tile by then (one
-- tile a tick from where it is now); no crab's blast band covers it, and my own
-- crab cannot close to contact by then.  Not used for the yellow pools: she is
-- invulnerable while she charges (wiki Strategies:977, "Verzik is invulnerable
-- while charging this attack"), and Blert's raiders all but stop attacking in
-- that window (0.034 attacks a tick against 0.452 the rest of P3).
QD.RAID_PLAY_VERZIK_SORTIE = { max_ticks = 3, run = 2, max_wait = 1 }

-- THE SLOW PACE'S HOLD (owner: the slow lane "kills p3 slow enough so that the
-- green orb appears... I would like to see at least 1 full cycle"; "the slow one
-- just needs to survive").  The halberd was its only throttle, and once the
-- seats took the swings the sortie finds, _vzslowp3 killed her with the ball in
-- the air (P3+180 thrown, "not a landing (she died first)").  So on the slow
-- pace, until one ball's chain has resolved, nobody takes her below
-- QD.RAID_PLAY_VERZIK_SLOW_FLOOR (enrage is at 20%, ~tob_verzik_enrage); after
-- it, the kill.
QD.RAID_PLAY_VERZIK_SLOW_FLOOR = 30
function QD.raid._verzik_slow_hold(st, v)
    assert(st, "_verzik_slow_hold: st")
    assert(v, "_verzik_slow_hold: v")
    local vz, b = st.vz, v.boss
    if vz.pace ~= "slow" or (vz.ball_resolved or 0) > 0 or b == nil then return false end
    -- a ratio of 0 on a living boss is no reading: her fresh P3 pool shows no
    -- bar until she is hit (content's ~tob_verzik_fresh_pool), and reading it
    -- as 0% held every swing -- so no hit ever brought the bar -- until the
    -- first ball resolved: _vzslow P3+0..200 without a single hit on her
    if b.health_ratio == nil or b.health_ratio <= 0 or b.health_scale == nil or b.health_scale <= 0 then return false end
    return b.health_ratio * 100 <= b.health_scale * QD.RAID_PLAY_VERZIK_SLOW_FLOOR
end
local function sortie_to(state) return function() return nil, state end end
local SORTIE_ON = { sortie_none = sortie_to("IDLE"), sortie_go = sortie_to("GO"), sortie_strike = sortie_to("STRIKE") }
QD.raid.sm_declare("verzik_sortie", {
    start = "IDLE",
    states = {
        IDLE   = { note = "no safe swing in reach of a short run: the state has the tick", on = SORTIE_ON },
        GO     = { note = "running to the strike tile", on = SORTIE_ON,
            enter = function(c) c.vz.sorties = (c.vz.sorties or 0) + 1 end },
        STRIKE = { note = "on it, weapon ready: swing", on = SORTIE_ON,
            enter = function(c) c.vz.strikes = (c.vz.strikes or 0) + 1 end },
    },
})

-- `f`: st, v, intent, ok (floor test), reach, tor (vz.tor), crabs (optional,
-- _verzik_nylo_read's), goal = {x, z} and deadline (both optional).  Returns
-- true when the sortie owns the tick (GO, STRIKE).
function QD.raid._verzik_sortie(f)
    assert(f, "_verzik_sortie: f")
    assert(f.intent, "_verzik_sortie: f.intent")
    assert(f.ok, "_verzik_sortie: f.ok")
    local st, v, intent = f.st, f.v, f.intent
    local vz, me, b = st.vz, v.me, v.boss
    local SO, NY = QD.RAID_PLAY_VERZIK_SORTIE, QD.RAID_PLAY_VERZIK_NYLO
    local reach = f.reach or 1
    local now = v.tick
    local ready = QD.raid._play_next_swing(st, v)
    if QD.raid._verzik_slow_hold(st, v) then ready = now + 999 end
    local best, bx, bz, bswing = nil, nil, nil, nil
    if b ~= nil and ready <= now + SO.max_ticks then
        local n = b.size or 1
        for x = b.x - reach, b.x + n - 1 + reach do
            for z = b.z - reach, b.z + n - 1 + reach do
                local dd = QD.raid._verzik_dist(x, z, b)
                local dme = math.max(math.abs(x - me.x), math.abs(z - me.z))
                local k = math.ceil(dme / SO.run)
                if dd >= 1 and dd <= reach and k <= SO.max_ticks and f.ok(x, z) and not v.shadows[x * 100000 + z] then
                    local arrive = now + k
                    local swing = math.max(arrive, ready)
                    local wait = swing - arrive
                    local safe = wait <= SO.max_wait
                    local horizon = swing - now
                    for _, e in pairs(f.tor or {}) do
                        if safe and math.max(math.abs(e.x - x), math.abs(e.z - z)) <= horizon + 1 then safe = false end
                    end
                    -- (a popping crab's band only: a chasing one is let come)
                    for _, c in ipairs(f.crabs or {}) do
                        if safe and c.state == "DYING" and QD.raid._verzik_nylo_gap(x, z, c.row) <= NY.reach then
                            safe = false
                        end
                    end
                    local back = 0
                    if safe and f.goal ~= nil then
                        back = math.ceil(math.max(math.abs(x - f.goal.x), math.abs(z - f.goal.z)) / SO.run)
                        if f.deadline ~= nil and swing + 1 + back > f.deadline then safe = false end
                    end
                    if safe then
                        local cost = k + wait + back
                        if best == nil or cost < best then best, bx, bz, bswing = cost, x, z, swing end
                    end
                end
            end
        end
    end
    local ev = "sortie_none"
    if best ~= nil then
        ev = (bx == me.x and bz == me.z and bswing <= now) and "sortie_strike" or "sortie_go"
    end
    local m = QD.raid.sm_run(st, v, "verzik_sortie", { vz = vz }, { { name = ev } })
    if m.state == "IDLE" then return false end
    if m.state == "STRIKE" then
        intent.walk = nil
        intent.attack = true
        return true
    end
    intent.walk = (bx ~= me.x or bz ~= me.z) and { x = bx, z = bz } or nil
    intent.attack = false
    vz.target_slot = nil
    return true
end

-- AVOID X: ONE LOOP FOR EVERYTHING THAT HUNTS A RAIDER (owner 2026-10-07:
-- "Make sure the logic for tornadoes and avoid nylocas coordinate with each
-- other - perhaps you need a generic 'avoid X' logic loop during P3. (You also
-- have to avoid nylocas in P2)").  Before this, the tornado dodge and the crab
-- kite were two branches of one if/elseif chain: whichever came first owned
-- the tick and the other's hazard was invisible to the tile it chose, so a
-- tornado dodge could step into a crab's band and a crab kite into a
-- tornado's next step.  Now every hazard adds into ONE field, every candidate
-- tile is scored against all of them at once, and one machine says how much
-- of the tick the field owns:
--   CLEAR  nothing within its horizon -- the rest of the plan plays the tick.
--   WATCH  something hunts me but nothing reaches my tile -- the field picks
--          the tile, which is often where I stand, and I swing from it.
--   DODGE  something reaches my tile now -- out of it first.
-- The sources (each a function of a tile; add a hazard by adding a source):
--   * nylocas: MY crab must not arrive (contact), its band counts as its life
--     runs out, and while it chases me my mates must not stand in its band;
--     anybody else's crab: its band, 63 / 26 / 8 at 1 / 2 / 3.
--   * tornadoes (P3): each one's next step toward me -- on or beside it is a
--     hit for half my hitpoints (^tob_verzik tornado_pct 50); two away, it
--     can be next tick.
local function avoid_to(state) return function() return nil, state end end
local AVOID_ON = { hazard_clear = avoid_to("CLEAR"), hazard_near = avoid_to("WATCH"), hazard_here = avoid_to("DODGE") }
QD.raid.sm_declare("verzik_avoid", {
    start = "CLEAR",
    states = {
        CLEAR = { note = "nothing hunting me within reach of its next step", on = AVOID_ON },
        WATCH = { note = "hunted, not reached: the field picks my tile, I swing from it",
            on = AVOID_ON, enter = function(c) c.vz.avoid_watch = (c.vz.avoid_watch or 0) + 1 end },
        DODGE = { note = "a hazard reaches my tile: out of it first",
            on = AVOID_ON, enter = function(c) c.vz.avoid_dodge = (c.vz.avoid_dodge or 0) + 1 end },
    },
})
QD.RAID_PLAY_VERZIK_AVOID = { tor_horizon = 4, nylo_horizon = 5, tor_hit = 500, tor_near = 40, wall = 15, crowd = 25, reach_bonus = 10, reach_miss = 25 }

-- The tick.  `f`: st, v, go, ok (floor test), reach, mates, and optionally
-- tor (vz.tor), side = {x, z} (my side of her), pool = {x, z, pull}.
-- Returns the state; in WATCH/DODGE it has moved the raider (f.go) unless
-- standing is best.
function QD.raid._verzik_avoid(f)
    assert(f, "_verzik_avoid: f")
    assert(f.intent, "_verzik_avoid: f.intent")
    assert(f.go, "_verzik_avoid: f.go")
    assert(f.ok, "_verzik_avoid: f.ok")
    local st, v = f.st, f.v
    local vz, me, b = st.vz, v.me, v.boss
    local NY, A = QD.RAID_PLAY_VERZIK_NYLO, QD.RAID_PLAY_VERZIK_AVOID
    local O, F = st.origin, st.plan.floor
    local crabs = QD.raid._verzik_nylo_read(st, v)
    -- the sources this tick
    local tor_next, near, here, chasing = {}, false, false, false
    for _, e in pairs(f.tor or {}) do
        local nx, nz = e.x, e.z
        if me.x > nx then nx = nx + 1 elseif me.x < nx then nx = nx - 1 end
        if me.z > nz then nz = nz + 1 elseif me.z < nz then nz = nz - 1 end
        tor_next[#tor_next + 1] = { nx, nz }
        local d = math.max(math.abs(e.x - me.x), math.abs(e.z - me.z))
        if d <= A.tor_horizon then near = true end
        if nx == me.x and nz == me.z then here = true end
    end
    -- ONLY A POPPING CRAB IS A HAZARD (owner 2026-10-07, watching watchvz:
    -- "they only need to try to maintain a good distance after starting the
    -- POP and they should try to stay close to verzik so they can continue to
    -- attack while they are being chased ... they should only run away when
    -- they are in the pop zone of the nylocas").  A crab that is chasing is let
    -- come: it pops on arrival (npc_range <= 1), and its blast lands about three
    -- ticks after its row reads dead (DYING) -- two ticks of running take a
    -- raider from contact to four out, past the 3-tile band.  So the band of a
    -- DYING crab is the whole of the crab source, whoever it hunted.
    for _, k in ipairs(crabs) do
        if k.state == "DYING" then
            local d = QD.raid._verzik_nylo_gap(me.x, me.z, k.row)
            if d <= NY.reach + 1 then near = true end
            if d <= NY.reach then here = true end
        end
    end
    local ev = here and "hazard_here" or (near and "hazard_near" or "hazard_clear")
    local m = QD.raid.sm_run(st, v, "verzik_avoid", { vz = vz }, { { name = ev } })
    if m.state == "CLEAR" then return "CLEAR" end
    -- the attack window: hunted but not reached, a swing that costs nothing
    -- is taken where I stand (the chain's attack selection presses it)
    if m.state == "WATCH" and QD.raid._verzik_sortie({ st = st, v = v, intent = f.intent, ok = f.ok,
            reach = f.reach, tor = f.tor, crabs = crabs }) then
        return m.state
    end
    -- the field: hazards, then the preferences that make a safe tile a good one
    local function field(x, z)
        local sc = 0
        for _, t in ipairs(tor_next) do
            local d = math.max(math.abs(t[1] - x), math.abs(t[2] - z))
            -- a tornado touches only on its raider's own tile (fdf77aae1c):
            -- its next step ON the tile is the hit, beside it is next tick's
            if d == 0 then sc = sc + A.tor_hit elseif d == 1 then sc = sc + A.tor_near elseif d == 2 then sc = sc + 10 end
        end
        for _, k in ipairs(crabs) do
            if k.state == "DYING" then
                local d = QD.raid._verzik_nylo_gap(x, z, k.row)
                if d <= NY.reach then sc = sc + (NY.band[d] or 0) * 4 end
            end
        end
        for _, mt in ipairs(f.mates or {}) do
            local dm = math.max(math.abs(mt.x - x), math.abs(mt.z - z))
            if dm <= 1 then sc = sc + A.crowd end
        end
        if b ~= nil then
            local db = QD.raid._verzik_dist(x, z, b)
            local reach = f.reach or 1
            if db >= 1 and db <= reach then sc = sc - A.reach_bonus else sc = sc + math.max(0, db - reach) * A.reach_miss end
        end
        local wall = math.min(x - (O.x + F[1]), (O.x + F[3]) - x, z - (O.z + F[2]), (O.z + F[4]) - z, 3)
        sc = sc + (3 - wall) * A.wall
        if f.pool ~= nil then
            sc = sc + math.max(math.abs(x - f.pool.x), math.abs(z - f.pool.z)) * (f.pool.pull or 10)
        end
        if f.side ~= nil then sc = sc + math.max(math.abs(x - f.side.x), math.abs(z - f.side.z)) * 2 end
        return sc + math.max(math.abs(x - me.x), math.abs(z - me.z))
    end
    local best, bx, bz = field(me.x, me.z), me.x, me.z
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = me.x + dx, me.z + dz
            if (dx ~= 0 or dz ~= 0) and f.ok(x, z) and not v.shadows[x * 100000 + z]
                and (b == nil or QD.raid._verzik_dist(x, z, b) >= 1) then
                local sc = field(x, z)
                if sc < best then best, bx, bz = sc, x, z end
            end
        end
    end
    if bx ~= me.x or bz ~= me.z then
        f.go(bx, bz)
        vz.target_slot = nil
    end
    return m.state
end

-- THE WEB RUN (owner 2026-10-07: "I never saw verzik do the spinning web phase
-- nor the players do the web run"; content f4b4d64ea0 now spins after Near
-- Reality's WebPassiveSpell: on the centre, 24 throws from 3 ticks into the
-- spin, each a web at the targeted raider's tile and two more 1-3 from it,
-- 4 ticks in flight, a raider moving off a web stuck 10 ticks, a web
-- exploding for 0-40 after 12; W:960-964 a raider who stands still is webbed
-- where he stands, "you have to dance around her").
-- So, from the spin until its last webs are down, the seat runs a ring of
-- tiles beside her body, every seat the same way round.  A web lands where I
-- stood 4 ticks before, so a tile is left inside 3 ticks -- which leaves room
-- for a swing when the weapon is ready (an attack in reach does not move
-- me).  A step never crosses a web: stepping OFF one is what binds.  A seat
-- already bound is the freeing logic's (P3, "a web on a teammate's tile").
--   OFF    no spin, or it is over: the rest of the plan plays the tick.
--   RUN    on the ring, moving on.
--   SWING  on a ring tile in reach, the weapon ready: the press, no step.
local function web_to(state) return function() return nil, state end end
local WEB_ON = { web_off = web_to("OFF"), web_run = web_to("RUN"), web_swing = web_to("SWING") }
QD.raid.sm_declare("verzik_web", {
    start = "OFF",
    states = {
        OFF   = { note = "no spin", on = WEB_ON },
        RUN   = { note = "round her on the ring, off every web", on = WEB_ON,
            enter = function(c) c.vz.web_runs = (c.vz.web_runs or 0) + 1 end },
        SWING = { note = "a ring tile in reach, weapon ready: swing, no step", on = WEB_ON,
            enter = function(c) c.vz.web_swings = (c.vz.web_swings or 0) + 1 end },
    },
})
-- span: spin + 3 + 24 throws + 4 in flight; stay: ticks a tile may be held
-- span 36: the special starts two ticks before the spin, its throws run to
-- spin + 26 and fly 4 -- 31 left the last throws landing on a seat already
-- back at a standstill (svavzslow seat 2, a web on its tile at t796, bound
-- through the tornado's touch)
QD.RAID_PLAY_VERZIK_WEB = { span = 36, stay = 2 }

-- The ring: the tiles one out from her body, clockwise from its north-west corner.
function QD.raid._verzik_web_ring(b)
    assert(b, "_verzik_web_ring: b")
    local n = b.size or 3
    local x0, z0, x1, z1 = b.x - 1, b.z - 1, b.x + n, b.z + n
    local ring = {}
    for x = x0, x1 do ring[#ring + 1] = { x, z1 } end
    for z = z1 - 1, z0, -1 do ring[#ring + 1] = { x1, z } end
    for x = x1 - 1, x0, -1 do ring[#ring + 1] = { x, z0 } end
    for z = z0 + 1, z1 - 1 do ring[#ring + 1] = { x0, z } end
    return ring
end

-- the tornadoes as tiles, for tor_reaches
-- ONE STEP AHEAD of what the client shows: the client's npc row is a tick
-- behind the server's tornado (svavzslow on e71545148: the seat read 1 where
-- the server had the tornado on its tile, t775; 2 against 1, t818), so every
-- dodge was a tile late.  Each is stepped once toward ME -- the cautious way
-- for a tornado chasing someone else too.
function QD.raid._verzik_tor_tiles(v)
    local out = {}
    local me = v.me
    for _, tr in ipairs(v.tornadoes or {}) do
        local x, z = tr.x, tr.z
        if tr.server_x ~= nil then
            -- the server's own tile (DriveNpcRow.server_x: the head of its
            -- route queue), no guess needed
            x, z = tr.server_x, tr.server_z
        elseif me ~= nil then
            if me.x > x then x = x + 1 elseif me.x < x then x = x - 1 end
            if me.z > z then z = z + 1 elseif me.z < z then z = z - 1 end
        end
        out[#out + 1] = { x = x, z = z }
    end
    return out
end

-- `f`: st, v, intent, ok (floor test), reach, tor.  True while the run owns the tick.
function QD.raid._verzik_web_run(f)
    assert(f, "_verzik_web_run: f")
    assert(f.intent, "_verzik_web_run: f.intent")
    assert(f.ok, "_verzik_web_run: f.ok")
    local st, v, intent = f.st, f.v, f.intent
    local vz, me, b, P = st.vz, v.me, v.boss, st.plan
    local W = QD.RAID_PLAY_VERZIK_WEB
    if v.attack ~= nil and v.attack.seq == P.p3_webs and vz.web_from ~= (v.attack.at or v.attack.tick) then
        vz.web_from = v.attack.at or v.attack.tick
        vz.web_tile, vz.web_since = nil, nil
    end
    local ev = "web_off"
    local on_web = false
    for _, w in ipairs(v.webs) do
        if w.row.x == me.x and w.row.z == me.z then on_web = true end
    end
    local active = vz.web_from ~= nil and v.tick <= vz.web_from + W.span and b ~= nil and not on_web
    local ring, idx = nil, nil
    if active then
        ring = QD.raid._verzik_web_ring(b)
        for i, t in ipairs(ring) do
            if t[1] == me.x and t[2] == me.z then idx = i end
        end
        if vz.web_tile ~= me.x * 100000 + me.z then vz.web_tile, vz.web_since = me.x * 100000 + me.z, v.tick end
        local held = v.tick - vz.web_since
        -- (no swing with a tornado able to reach my tile: in the enrage the
        -- webs are where the touches were -- 15 of 21 across three names on
        -- the power-through tree came 12-30 ticks into the spin, the seat
        -- held still two or three ticks)
        local tor_here = QD.raid._verzik_tor_reaches(QD.raid._verzik_tor_tiles(v), me, me, me.x, me.z)
        if idx ~= nil and held < W.stay and not tor_here and QD.raid._verzik_swing_window(st, v, f.reach, f.tor) then
            ev = "web_swing"
        else
            ev = "web_run"
        end
    end
    local m = QD.raid.sm_run(st, v, "verzik_web", { vz = vz }, { { name = ev } })
    if m.state == "OFF" then return false end
    if m.state == "SWING" then
        intent.walk = nil
        intent.attack = true
        return true
    end
    local tors = QD.raid._verzik_tor_tiles(v)
    local function free(x, z)
        if not f.ok(x, z) or v.shadows[x * 100000 + z] then return false end
        for _, w in ipairs(v.webs) do
            if w.row.x == x and w.row.z == z then return false end
        end
        if QD.raid._verzik_tor_reaches(tors, me, me, x, z) then return false end
        return QD.raid._verzik_dist(x, z, b) >= 1
    end
    local go = nil
    if idx == nil then
        -- onto the ring: its nearest free tile
        local bd = nil
        for _, t in ipairs(ring) do
            local d = math.max(math.abs(t[1] - me.x), math.abs(t[2] - me.z))
            if free(t[1], t[2]) and (bd == nil or d < bd) then bd, go = d, t end
        end
    else
        -- on round: two tiles if both are free, else one, else hold
        local n = #ring
        local t1, t2 = ring[(idx % n) + 1], ring[((idx + 1) % n) + 1]
        -- the other way round when a tornado or a web blocks the way on
        local r1, r2 = ring[((idx - 2) % n) + 1], ring[((idx - 3) % n) + 1]
        if free(t1[1], t1[2]) and free(t2[1], t2[2]) then
            go = t2
        elseif free(t1[1], t1[2]) then
            go = t1
        elseif free(r1[1], r1[2]) and free(r2[1], r2[2]) then
            go = r2
        elseif free(r1[1], r1[2]) then
            go = r1
        end
    end
    intent.attack = false
    vz.ball_lock = v.tick
    if go ~= nil then intent.walk = { x = go[1], z = go[2] } end
    return true
end

-- THE YELLOW POOL, TIMED (owner 2026-10-07: "Prioritize pools and you need to
-- plan ahead a bit so the yellow is safe when the projectile hits but timed so
-- the tornado is not on them - create an algorithm to handle that.  You have to
-- be on the yellow during the dangerous tick, but you can't be standing still
-- because of the tornado.")
--
-- The two facts it is built on.  The blast reads each raider's tile when her
-- queued hit fires (tob_verzik.rs2 ~tob_verzik_powerblast, npc_queue 4 at the
-- charge + 1): ^tob_verzik_yellow_charge_ticks 14 after the pools are seen, so
-- the DANGEROUS TICKS are blast - 1 and blast, which covers either order the
-- server takes npcs and players in.  And a tornado touches only by stepping
-- onto its raider's own tile, one tile a tick (fdf77aae1c, Blert 16 of 16),
-- while a raider runs two: standing still is the only way it catches up.
--
-- So the seat keeps moving until it must stand, through tiles from which the
-- pool is still reachable by the first dangerous tick, and is on it for exactly
-- those two ticks.  Two ticks of standing let a tornado that was kept two
-- behind close to one -- never onto the tile.
--   NONE    no charge on.
--   ARRIVE  nothing hunts me: straight to my pool and stand on it.
--   ORBIT   a tornado hunts me and there is time: one move a tick, never to a
--           tile the pool is out of reach from, clearest of the tornadoes and
--           the crabs, nearest the pool.
--   HOLD    on it for the dangerous ticks.
--   AFTER   the blast has landed; the plan has the tick back.
-- `hold` is 2, not 1 (2026-10-07, _vzfastp3 P3+151, two raiders dead): the
-- blast reads each raider's tile before movement on its tick, and a click on t
-- moves on t + 1 -- ORBIT stepped both onto their pools ON the blast tick and
-- the server read their tiles one short (6422,87 and 6424,84).  The same
-- margin the ball needed (QD.RAID_PLAY_VERZIK_BALL.before).
QD.RAID_PLAY_VERZIK_POOL = { hold = 2, tor_near = 4, run = 2 }
local function pool_to(state) return function() return nil, state end end
local POOL_ON = { pool_none = pool_to("NONE"), pool_arrive = pool_to("ARRIVE"), pool_orbit = pool_to("ORBIT"),
    pool_hold = pool_to("HOLD"), pool_after = pool_to("AFTER") }
QD.raid.sm_declare("verzik_pool", {
    start = "NONE",
    states = {
        NONE   = { note = "no charge on", on = POOL_ON },
        ARRIVE = { note = "nothing hunts me: straight to my pool", on = POOL_ON },
        ORBIT  = { note = "hunted with time to spare: keep moving, the pool always in reach by the dangerous tick",
            on = POOL_ON, enter = function(c) c.vz.pool_orbits = (c.vz.pool_orbits or 0) + 1 end },
        HOLD   = { note = "on my pool for the dangerous ticks", on = POOL_ON },
        AFTER  = { note = "the blast has landed", on = POOL_ON },
    },
})

-- The tick.  `f`: st, v, intent, ok (a floor test), pool = {x, z} (mine, from
-- the pairing), tor (vz.tor), crabs (optional, _verzik_nylo_read's).  Sets
-- intent.walk and clears the attack while the charge is on.  Returns true when
-- the pool owns the tick (ARRIVE, ORBIT, HOLD).
function QD.raid._verzik_pool_run(f)
    assert(f, "_verzik_pool_run: f")
    assert(f.intent, "_verzik_pool_run: f.intent")
    assert(f.ok, "_verzik_pool_run: f.ok")
    local st, v, intent = f.st, f.v, f.intent
    local vz, me, b, P = st.vz, v.me, v.boss, st.plan
    local PL = QD.RAID_PLAY_VERZIK_POOL
    local pool = f.pool
    local ev = "pool_none"
    local B, A, r, d = nil, nil, nil, nil
    if pool ~= nil and vz.pool_first ~= nil then
        B = vz.pool_first + P.pool_life
        A = B - PL.hold
        d = math.max(math.abs(pool.x - me.x), math.abs(pool.z - me.z))
        r = A - v.tick
        local hunted = false
        for _, e in pairs(f.tor or {}) do
            if math.max(math.abs(e.x - me.x), math.abs(e.z - me.z)) <= PL.tor_near then hunted = true end
        end
        if v.tick > B then
            ev = "pool_after"
        elseif d == 0 and r <= 0 then
            ev = "pool_hold"
        elseif hunted and PL.run * (r - 1) >= d + 1 then
            ev = "pool_orbit"
        else
            ev = "pool_arrive"
        end
    end
    local m = QD.raid.sm_run(st, v, "verzik_pool", { vz = vz }, { { name = ev } })
    if m.state == "NONE" or m.state == "AFTER" then return false end
    intent.attack = false
    vz.target_slot = nil
    if m.state == "HOLD" then
        intent.walk = nil
        return true
    end
    if m.state == "ARRIVE" then
        intent.walk = (d > 0) and { x = pool.x, z = pool.z } or nil
        return true
    end
    -- ORBIT: one move, the pool still reachable by A from wherever it lands
    local reach_left = PL.run * (r - 1)
    local best, bx, bz = nil, nil, nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = me.x + dx, me.z + dz
            local dp = math.max(math.abs(pool.x - x), math.abs(pool.z - z))
            if (dx ~= 0 or dz ~= 0) and dp <= reach_left and f.ok(x, z) and not v.shadows[x * 100000 + z]
                and (b == nil or QD.raid._verzik_dist(x, z, b) >= 1) then
                local sc = dp * 3
                for _, e in pairs(f.tor or {}) do
                    -- where it steps next if I am on x, z
                    local nx, nz = e.x, e.z
                    if x > nx then nx = nx + 1 elseif x < nx then nx = nx - 1 end
                    if z > nz then nz = nz + 1 elseif z < nz then nz = nz - 1 end
                    local c2 = math.max(math.abs(nx - x), math.abs(nz - z))
                    if c2 == 0 then sc = sc + 1000 elseif c2 == 1 then sc = sc + 40 end
                    -- BUILD THE GAP: the hold stands two ticks; with the click's
                    -- lag a tornado must be four back when I step on
                    sc = sc + math.max(0, PL.hold + 2 - c2) * 12
                end
                for _, k in ipairs(f.crabs or {}) do
                    local g = QD.raid._verzik_nylo_gap(x, z, k.row)
                    if g <= QD.RAID_PLAY_VERZIK_NYLO.reach then sc = sc + (QD.RAID_PLAY_VERZIK_NYLO.band[g] or 0) * 4 end
                end
                if best == nil or sc < best then best, bx, bz = sc, x, z end
            end
        end
    end
    if bx == nil then
        intent.walk = { x = pool.x, z = pool.z }
    else
        intent.walk = { x = bx, z = bz }
    end
    return true
end

-- THE GREEN BALL, as a machine (owner 2026-10-07: "Implement a fix for the
-- green ball. There should be a green ball state machine for managing that.")
--
-- Content (tob_verzik.rs2 [queue,tob_verzik_ball_land]) makes it a CHAIN, one
-- tick per hop, and that is what sharing has to satisfy.  On landing it hops to
-- a raider within 1 (^tob_verzik_p3_ball_bounce_range) of the raider it is on
-- that it has NOT yet visited; once it has landed on every raider in the room it
-- dissipates.  With only a visited neighbour, BOTH take 74; with none, the
-- holder takes 74.  For a trio: L lands on T0 (an unvisited raider must be
-- within 1 of T0), L+1 lands on T1 (the THIRD must be within 1 of T1), L+2
-- dissipates.  The old share put both mates "adjacent to the target", which can
-- leave them two apart: the second hop then found only T0 and hit T1 and T0
-- for 74 each -- the "NOT SHARED: 2 of 3, 74 damage" rows.  And it is a
-- homing projectile (~player_projectile at the target's uid): it lands
-- wherever the target is.
--
-- THE GREEN BALL IS A CHAIN IN ORB ORDER, ONE LINK AT A TIME (owner
-- 2026-10-07: "Do the chain in orb order, so the seat that runs to the bounce
-- target is always the seat that comes after the bounce target in orb order";
-- and, correcting the formation this replaced: "you should only group in ORB
-- ORDER for the green ball.  So once the ball is passed to another player,
-- that player is free to do whatever, and if you're the third person in the
-- list, you don't have to do anything until the ball is targeting the person
-- before you").  The formation before this put all three on a fixed line at
-- the first sighting, and each client built its line from its own floor test:
-- probecov6 had ring sizes 2, 3 and 2 on the three seats, the trio ended on
-- two tiles, and the crowd rule exploded the ball on all of them (74, 69, 62).
--
-- Every client reads the same thing, so every client names the same chain:
-- the ball projectile in flight (1598), and its target -- the raider at its
-- destination.  A projectile at a different raider is a HOP: the holder
-- before it is VISITED.  Per seat, against the current target:
--   NONE     no ball out.
--   HOLDING  it is coming at me: I stand (the next seat is coming to me), and
--            swing if she is in reach.
--   JOINING  it is coming at the raider before me in orb order (the first
--            unvisited seat after the target): to a tile beside them, inside
--            the 3x3 the bounce reads, before it lands.
--   CLEAR    visited, or later in the chain: free to fight, never inside the
--            target's 3x3 (a second valid raider there is a crowd, and a
--            visited one is a bounce backwards -- both explode).
--   AFTER    no ball for `linger` ticks: the chain has ended.
local function ball_to(state) return function() return nil, state end end
local BALL_ON = { ball_none = ball_to("NONE"), ball_hold = ball_to("HOLDING"), ball_join = ball_to("JOINING"),
    ball_clear = ball_to("CLEAR"), ball_after = ball_to("AFTER") }
QD.raid.sm_declare("verzik_ball", {
    start = "NONE",
    states = {
        NONE    = { note = "no ball out", on = BALL_ON },
        HOLDING = { note = "coming at me: I stand, swinging if she is in reach", on = BALL_ON },
        JOINING = { note = "coming at the seat before me: beside them before it lands", on = BALL_ON,
            enter = function(c) c.vz.ball_joins = (c.vz.ball_joins or 0) + 1 end },
        CLEAR   = { note = "visited or later: free, out of the target's 3x3", on = BALL_ON },
        AFTER   = { note = "the chain has ended", on = BALL_ON },
    },
})
-- range: the bounce reads the target's 3x3 (owner; ^tob_verzik_p3_ball_bounce_range).
-- linger: ticks after the last projectile before the chain is over (the land
-- and a hop's launch are a tick or two apart).
QD.RAID_PLAY_VERZIK_BALL = { range = 1, linger = 3 }

-- A swing this tick costs nothing: the weapon is ready, her body is in reach
-- from where I stand (so the attack does not walk me), and no tornado steps
-- onto my tile this tick (a tornado touches only on its raider's own tile,
-- fdf77aae1c).  Every movement state asks this first (owner 2026-10-07: "no
-- matter what the state, the seats have a window to attack").
function QD.raid._verzik_swing_window(st, v, reach, tor)
    assert(st, "_verzik_swing_window: st")
    assert(v, "_verzik_swing_window: v")
    local me, b = v.me, v.boss
    if b == nil or QD.raid._play_next_swing(st, v) > v.tick then return false end
    local db = QD.raid._verzik_dist(me.x, me.z, b)
    if db < 1 or db > (reach or 1) then return false end
    for _, e in pairs(tor or {}) do
        local nx, nz = e.x, e.z
        if me.x > nx then nx = nx + 1 elseif me.x < nx then nx = nx - 1 end
        if me.z > nz then nz = nz + 1 elseif me.z < nz then nz = nz - 1 end
        if nx == me.x and nz == me.z then return false end
    end
    return true
end


-- The seat of a raider row, by name against QD.party.names() (seat order is
-- orb order: the seats join in it).  Nil for a name the party does not know.
function QD.raid._verzik_seat_of(row)
    assert(row, "_verzik_seat_of: row")
    local want = QD.party._fold(row.name)
    for i, nm in ipairs(QD.party.names()) do
        if QD.party._fold(nm) == want then return i end
    end
    return nil
end

-- THE CHAIN ENDS ON ITS OWN CLOCK, every P3 tick, whoever owns the tick: a
-- ball nobody has seen in flight for `linger` ticks is over.  The run below
-- only closed it on a tick it was asked, and a seat whose ticks went to an
-- earlier state never closed it -- so its slow hold, released by the first
-- resolved ball, held for good (svbvzslow seat 1: no swing from t650 to its
-- death at t830, three tiles from her).
function QD.raid._verzik_ball_expire(st, v)
    assert(st, "_verzik_ball_expire: st")
    assert(v, "_verzik_ball_expire: v")
    local vz, bm = st.vz, st.vz.bm
    if bm == nil or bm.done then return end
    for _, p in ipairs(v.proj or {}) do
        if p.spotanim_id == st.plan.ball_proj then return end
    end
    if v.tick > bm.last + QD.RAID_PLAY_VERZIK_BALL.linger then
        bm.done = true
        vz.ball_resolved = (vz.ball_resolved or 0) + 1
    end
end

-- The tick.  `f`: st, v, intent, ok (floor test), tor (vz.tor), reach.
-- Returns true while the ball owns the tick.
function QD.raid._verzik_ball_run(f)
    assert(f, "_verzik_ball_run: f")
    assert(f.intent, "_verzik_ball_run: f.intent")
    assert(f.ok, "_verzik_ball_run: f.ok")
    local st, v, intent = f.st, f.v, f.intent
    -- once a tick: the enrage ring's own handler and the P3 chain both ask,
    -- and a second run would step the machine twice -- the second asker
    -- gets the first answer, and its walk, again
    if st.vz.ball_run_tick == v.tick then
        if st.vz.ball_run_walk ~= nil then intent.walk = st.vz.ball_run_walk end
        if st.vz.ball_run_owned then intent.attack = st.vz.ball_run_attack end
        return st.vz.ball_run_owned
    end
    local owned = QD.raid._verzik_ball_run_once(f)
    st.vz.ball_run_tick, st.vz.ball_run_owned = v.tick, owned
    st.vz.ball_run_walk, st.vz.ball_run_attack = intent.walk, intent.attack
    local bm = st.vz.bm
    if bm ~= nil and not bm.done then
        local m = QD.raid.sm_at(st, "verzik_ball")
        st.notes = st.notes or {}
        if #st.notes < 90 then
            st.notes[#st.notes + 1] = "b" .. v.tick .. (m and m.state or "?"):sub(1, 2) .. "h" .. tostring(bm.holder)
                .. "@" .. v.me.x .. "," .. v.me.z .. (intent.walk and (">" .. intent.walk.x .. "," .. intent.walk.z) or "")
        end
    end
    return owned
end

function QD.raid._verzik_ball_run_once(f)
    local st, v, intent = f.st, f.v, f.intent
    local vz, me, b, P = st.vz, v.me, v.boss, st.plan
    local BL = QD.RAID_PLAY_VERZIK_BALL
    local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
    -- the living seats on the floor, in orb order
    local ring = { { seat = st.role, pid = st.my_pid, x = me.x, z = me.z, me = true } }
    for _, m in ipairs(QD.raid._verzik_mates(st)) do
        local seat = QD.raid._verzik_seat_of(m)
        if seat ~= nil then ring[#ring + 1] = { seat = seat, pid = m.pid, x = m.x, z = m.z } end
    end
    table.sort(ring, function(r1, r2) return r1.seat < r2.seat end)
    -- the ball in flight, and the raider it is for
    local proj = nil
    for _, p in ipairs(v.proj or {}) do
        if p.spotanim_id == P.ball_proj then proj = p end
    end
    local bm = vz.bm
    if proj ~= nil then
        -- the target the projectile names (-(pid + 1) on the client's row,
        -- 32768 + pid in the facing space); the raider at its destination only
        -- when it names nobody -- svbvzslow t822-829: two seats on one tile
        -- made "nearest" a tie and the holder flipped seat by seat each tick
        local want = nil
        if proj.target ~= nil and proj.target < 0 then want = -proj.target - 1 end
        if proj.target ~= nil and proj.target >= 32768 then want = proj.target - 32768 end
        local tgt, td = nil, nil
        for _, r in ipairs(ring) do
            -- (checked against its destination: an encoding read wrong must
            -- not name a raider across the room)
            if want ~= nil and r.pid == want and cheb(r.x, r.z, proj.dst_x, proj.dst_z) <= 2 then tgt, td = r, -1 end
        end
        vz.ball_target_named = (vz.ball_target_named or 0) + ((td == -1) and 1 or 0)
        for _, r in ipairs(ring) do
            local d = cheb(r.x, r.z, proj.dst_x, proj.dst_z)
            if td == nil or d < td then tgt, td = r, d end
        end
        if bm == nil or bm.done then
            bm = { first = v.tick, visited = {}, holder = tgt.seat, hops = 0, last = v.tick }
            vz.bm = bm
            st.notes = st.notes or {}
            if #st.notes < 24 then
                st.notes[#st.notes + 1] = "ball" .. v.tick .. "s" .. tgt.seat .. "@" .. me.x .. "," .. me.z .. "n" .. #ring
            end
        elseif bm.holder ~= tgt.seat then
            bm.visited[bm.holder] = true
            bm.holder = tgt.seat
            bm.hops = bm.hops + 1
        end
        bm.last = v.tick
        bm.land = v.tick + math.ceil((proj.cycles_left or 0) / QD.RAID_PLAY_CYCLES_PER_TICK)
    end
    local ev = "ball_none"
    local H, nxt = nil, nil
    if bm ~= nil and not bm.done then
        if proj == nil and v.tick > bm.last + BL.linger then
            bm.done = true
            vz.ball_resolved = (vz.ball_resolved or 0) + 1
            ev = "ball_after"
        else
            local hi = nil
            for i, r in ipairs(ring) do if r.seat == bm.holder then hi = i end end
            if hi ~= nil then
                H = ring[hi]
                for k = 1, #ring - 1 do
                    local r = ring[((hi - 1 + k) % #ring) + 1]
                    if not bm.visited[r.seat] then nxt = r break end
                end
            end
            if st.role == bm.holder then
                ev = "ball_hold"
            elseif nxt ~= nil and nxt.me and proj ~= nil then
                ev = "ball_join"
            else
                ev = "ball_clear"
            end
        end
    end
    local m = QD.raid.sm_run(st, v, "verzik_ball", { vz = vz }, { { name = ev } })
    if m.state == "NONE" or m.state == "AFTER" then return false end
    -- THE TILES THE TORNADO GUARD MAY NOT DODGE ONTO this tick: it runs last
    -- and knew nothing of the ball -- svbvzslow t822-828, the target and a
    -- later seat each fled a tornado the same way, onto the third raider's
    -- tile, and the ball landed on a crowd.  The target keeps off every seat
    -- but the one joining it; the rest keep out of the target's 3x3.  (The
    -- joining seat's dodge is free: it chases the target anyway.)
    vz.guard_goal_tick, vz.guard_goal = nil, nil
    if m.state == "HOLDING" and nxt ~= nil then
        vz.guard_goal_tick, vz.guard_goal = v.tick, { x = nxt.x, z = nxt.z }
    elseif m.state == "JOINING" and H ~= nil then
        vz.guard_goal_tick, vz.guard_goal = v.tick, { x = H.x, z = H.z }
    end
    -- THE LANDING READS THE 3x3 AS IT STANDS (_vzslow t865-866: joined on
    -- one tile, then the holder and the joiner each dodged a tornado the
    -- other way, two apart on the landing, and the ball's final 74 hit the
    -- holder): in the last two ticks the pair dodge only inside each other's
    -- 3x3.  (The guard still dodges anywhere if nothing is left.)
    local landing = bm ~= nil and bm.land ~= nil and bm.land - v.tick <= 1
    if H ~= nil then
        vz.ball_forbid_tick = v.tick
        if m.state == "HOLDING" and landing and nxt ~= nil and cheb(me.x, me.z, nxt.x, nxt.z) <= BL.range then
            vz.ball_forbid = function(x, z) return cheb(x, z, nxt.x, nxt.z) > BL.range end
        elseif m.state == "JOINING" and landing and cheb(me.x, me.z, H.x, H.z) <= BL.range then
            vz.ball_forbid = function(x, z) return cheb(x, z, H.x, H.z) > BL.range end
        elseif m.state == "HOLDING" then
            vz.ball_forbid = function(x, z)
                for _, r in ipairs(ring) do
                    if not r.me and (nxt == nil or r.seat ~= nxt.seat) and cheb(r.x, r.z, x, z) <= BL.range + 1 then return true end
                end
                return false
            end
        elseif m.state == "CLEAR" then
            -- (a tile of margin: the target may dodge too, the same tick)
            vz.ball_forbid = function(x, z) return cheb(x, z, H.x, H.z) <= BL.range + 1 end
        else
            vz.ball_forbid_tick = nil
        end
    end
    -- a swing from where I stand, or no press at all (a press would walk me)
    local function stand()
        intent.walk = nil
        if QD.raid._verzik_swing_window(st, v, f.reach, f.tor) then
            intent.attack = true
            vz.ball_swings = (vz.ball_swings or 0) + 1
        else
            intent.attack = false
            vz.ball_lock = v.tick
        end
    end
    local function tile_ok(x, z)
        return f.ok(x, z) and not v.shadows[x * 100000 + z] and (b == nil or QD.raid._verzik_dist(x, z, b) >= 1)
    end
    local function in_reach(x, z)
        if b == nil then return false end
        local db = QD.raid._verzik_dist(x, z, b)
        return db >= 1 and db <= (f.reach or 1)
    end
    if m.state == "HOLDING" then
        stand()
        return true
    end
    if H == nil then return false end
    if m.state == "JOINING" then
        if cheb(me.x, me.z, H.x, H.z) <= BL.range then
            stand()
            return true
        end
        -- the tile beside them nearest me, with no other raider's 3x3 on it
        -- (a second valid one is a crowd), in her reach if one is
        local best, bx, bz = nil, nil, nil
        for dx = -BL.range, BL.range do
            for dz = -BL.range, BL.range do
                local x, z = H.x + dx, H.z + dz
                if (dx ~= 0 or dz ~= 0) and tile_ok(x, z) then
                    local sc = cheb(me.x, me.z, x, z) * 3
                    for _, r in ipairs(ring) do
                        if not r.me and r.seat ~= H.seat and cheb(r.x, r.z, x, z) <= BL.range then sc = sc + 50 end
                    end
                    if in_reach(x, z) then sc = sc - 2 end
                    if best == nil or sc < best then best, bx, bz = sc, x, z end
                end
            end
        end
        if bx == nil then return false end
        intent.walk = { x = bx, z = bz }
        intent.attack = false
        vz.ball_lock = v.tick
        return true
    end
    -- CLEAR: out of the target's 3x3, and not where a press would walk me into it
    local dH = cheb(me.x, me.z, H.x, H.z)
    if dH > BL.range then
        if dH <= BL.range + 1 then vz.ball_lock = v.tick end
        return false
    end
    local best, bx, bz = nil, nil, nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = me.x + dx, me.z + dz
            if tile_ok(x, z) and cheb(x, z, H.x, H.z) > BL.range + 1 then
                local sc = cheb(me.x, me.z, x, z) * 3
                for _, r in ipairs(ring) do
                    if not r.me and r.seat ~= H.seat and cheb(r.x, r.z, x, z) <= BL.range then sc = sc + 20 end
                end
                if in_reach(x, z) then sc = sc - 2 end
                if best == nil or sc < best then best, bx, bz = sc, x, z end
            end
        end
    end
    if bx ~= nil then intent.walk = { x = bx, z = bz } end
    intent.attack = false
    vz.ball_lock = v.tick
    vz.ball_clears = (vz.ball_clears or 0) + 1
    return true
end

-- THE TANK STEPS UNDER HER, whatever else is going on (owner 2026-10-07: "at
-- any given time, the Tank still needs to be stepping under - make sure
-- verzik's melee is implemented accurately and that the tank is still stepping
-- under").  Wiki Strategies:949-951: every attack she checks whether her
-- chosen target is in melee range, and "the primary tank should either walk
-- under or away from Verzik one tick before she attacks to avoid the melee
-- attack completely... Moving under Verzik will keep her more grounded".
-- Content: ~tob_verzik_tank_in_melee reads the tank's tile at the end of the
-- tick before her attack, and only npc_range 1 is a melee chance -- 0 (under
-- her) is safe.  Her melee, when it comes, hits every raider ADJACENT to her
-- for up to 63, unprayable, so a tank caught beside her costs the whole team.
--
-- An overlay over every P3 state (the ring, the ball, the pools, the avoid
-- loop) for the tank only, on the ticks it needs and no others:
--   CLEAR  her next attack is not due: the tank plays its state AND ATTACKS --
--          its swings, its sorties, the ring's swing (owner 2026-10-07: "The
--          tank should be attacking as well.").
--   UNDER  attack - 2, ONE click: under her body.  A click on t moves on t + 1,
--          so the tank is under at the end of attack - 1, where her check reads
--          it.
--   BACK   attack - 1: the attack is PRESSED, not a step.  It takes effect on
--          the attack tick itself -- after her check has read the tank under
--          her -- and the server walks it out to a melee tile and swings as
--          soon as the weapon is ready.
-- One tick under her per attack.  The first version held the tank under from
-- attack - 2 through the attack and stepped back the tick after, four ticks
-- with no press; in enrage she attacks every five and the tank swung 29 times
-- in ~600 ticks of _vzslowp3's P3 (dc3f76625).
-- The old hold stepped the tank OUT to range 2, and only on a tick no other
-- branch had taken; once the ball, the pools and the avoid loop came ahead of
-- it in the chain the tank could stand beside her through her attack.
QD.RAID_PLAY_VERZIK_TANK = { lead = 2 }
local function tank_to(state) return function() return nil, state end end
local TANK_ON = { tank_clear = tank_to("CLEAR"), tank_under = tank_to("UNDER"), tank_back = tank_to("BACK") }
QD.raid.sm_declare("verzik_tank", {
    start = "CLEAR",
    states = {
        CLEAR = { note = "her attack is not due: the tank plays its state", on = TANK_ON },
        UNDER = { note = "her attack is due: under her body, so her check finds no melee range", on = TANK_ON,
            enter = function(c) c.vz.tank_unders = (c.vz.tank_unders or 0) + 1 end },
        BACK  = { note = "her attack has gone: beside her again, in reach", on = TANK_ON },
    },
})

-- `f`: st, v, intent, next_attack (her next attack tick, _verzik_p3_clock),
-- reach, ok (floor test).  Returns true when the overlay owns the tick.
function QD.raid._verzik_tank_run(f)
    assert(f, "_verzik_tank_run: f")
    assert(f.intent, "_verzik_tank_run: f.intent")
    assert(f.ok, "_verzik_tank_run: f.ok")
    local st, v, intent = f.st, f.v, f.intent
    local vz, me, b = st.vz, v.me, v.boss
    local TK = QD.RAID_PLAY_VERZIK_TANK
    local A = f.next_attack
    local ev = "tank_clear"
    if b ~= nil and A ~= nil then
        if v.tick == A - TK.lead then
            ev = "tank_under"
        elseif v.tick == A - TK.lead + 1 then
            ev = "tank_back"
        end
    end
    -- a crab's threat outranks the step under (2026-10-07, svavzslowp3 P3+52:
    -- the tank stood under her while its own crab arrived, 52): the overlay
    -- yields, and the avoid loop takes the tank away from the crab -- which is
    -- out of her melee range too, the wiki's other answer ("walk under OR away")
    if ev == "tank_under" then
        local NY = QD.RAID_PLAY_VERZIK_NYLO
        for _, c in ipairs(v.crabs or {}) do
            local g = QD.raid._verzik_nylo_gap(me.x, me.z, c.row)
            local rec = (vz.nylo or {})[c.row.slot]
            local mine = rec ~= nil and rec.owner == st.my_pid and not c.dead
            if (mine and g <= NY.contact + 2) or (not mine and g <= NY.reach) then
                ev = "tank_clear"
                vz.tank_yields = (vz.tank_yields or 0) + 1
            end
        end
    end
    local m = QD.raid.sm_run(st, v, "verzik_tank", { vz = vz }, { { name = ev } })
    if m.state == "CLEAR" then return false end
    local n = b.size or 1
    local db = QD.raid._verzik_dist(me.x, me.z, b)
    if m.state == "UNDER" then
        if db > 0 then
            -- the footprint tile nearest me
            local ux = math.max(b.x, math.min(me.x, b.x + n - 1))
            local uz = math.max(b.z, math.min(me.z, b.z + n - 1))
            intent.walk = { x = ux, z = uz }
        else
            intent.walk = nil
        end
        intent.attack = false
        intent.spec = nil
        return true
    end
    -- BACK: press her.  The press lands on the attack tick, after her check
    -- read me under her; the server paths me to a melee tile and swings.
    intent.walk = nil
    intent.attack = true
    vz.tank_backs = (vz.tank_backs or 0) + 1
    return true
end

-- THE TORNADO GUARD, over every state (owner 2026-10-07: "And they are avoiding
-- tornados at all times.").  The avoid loop, the ring, the sortie and the
-- orbiting phases of the pool and the ball all score the tornadoes -- but a
-- state that HOLDS a tile (the pool's HOLD, the ball's WAIT and HOLDING, the
-- tank UNDER her) does not, and a tornado touches only by stepping onto its
-- raider's tile (fdf77aae1c), so a raider holding still is exactly the one it
-- catches: half the current hitpoints (wiki Strategies:988).
--
-- So, last thing in the tick, whatever the state decided: the tile I will be
-- on next tick (my walk's, else mine) is checked against every tornado's next
-- step -- toward where I am now and toward where I am going, whichever order
-- the server takes us in.  A tile it can step onto is not stood on: the nearest
-- tile within a run step that none can reach, nearest what the state wanted,
-- and clear of her body, the webs and the falling tiles.  The state is not
-- told; next tick it plans again from wherever I stand.
local function tor_step(e, tx, tz)
    local nx, nz = e.x, e.z
    if tx > nx then nx = nx + 1 elseif tx < nx then nx = nx - 1 end
    if tz > nz then nz = nz + 1 elseif tz < nz then nz = nz - 1 end
    return nx, nz
end
-- Can a tornado be ON x, z at the end of the tick my click moves me there?
-- Content (~tob_verzik_tornado_tick): each tick it first hits if it stands on
-- its raider's tile, else it walks one tile toward the raider's CURRENT tile;
-- one tile a tick, measured (27 of 27 steps, _vzslowp3).  A click on t moves
-- me on t + 1, so it has two steps before I land: toward where I am, then
-- toward wherever its next step finds me.  Any of those ending on x, z is a
-- touch.  (2026-10-07, dc3f76625: the guard looked one step ahead from the
-- plan's PREDICTED tornado tiles, and tornado hits of 28-43 kept landing.)
local function tor_reaches(tornadoes, me, mid, x, z)
    for _, e in ipairs(tornadoes or {}) do
        local ax, az = tor_step(e, me.x, me.z)
        local a2x, a2z = tor_step({ x = ax, z = az }, mid.x, mid.z)
        local b2x, b2z = tor_step({ x = ax, z = az }, x, z)
        if (e.x == x and e.z == z) or (ax == x and az == z) or (a2x == x and a2z == z) or (b2x == x and b2z == z) then
            return true
        end
    end
    return false
end
QD.raid._verzik_tor_reaches = tor_reaches
function QD.raid._verzik_tornado_guard(st, v, intent, ok)
    assert(st, "_verzik_tornado_guard: st")
    assert(v, "_verzik_tornado_guard: v")
    assert(intent, "_verzik_tornado_guard: intent")
    assert(ok, "_verzik_tornado_guard: ok")
    local vz, me, b = st.vz, v.me, v.boss
    -- the tornadoes as the client sees them, one step on (the row is a tick
    -- behind the server: QD.raid._verzik_tor_tiles)
    local tornadoes = QD.raid._verzik_tor_tiles(v)
    if #tornadoes == 0 then return false end
    -- where my last click has me at the end of this tick
    local mid = me
    if st.walk_target ~= nil and math.max(math.abs(st.walk_target.x - me.x), math.abs(st.walk_target.z - me.z)) <= 2 then
        mid = st.walk_target
    end
    local want = intent.walk
    local tx, tz = mid.x, mid.z
    if want ~= nil and math.max(math.abs(want.x - me.x), math.abs(want.z - me.z)) <= 2 then tx, tz = want.x, want.z end
    -- (my own tile too: `mid` is the walk I last sent, and a walk not taken
    -- -- bound in a web, or eaten by a press -- left me on a tile the check
    -- never looked at: svavzslow seat 3, t808 one from its tornado and no
    -- dodge, touched at t809)
    if not tor_reaches(tornadoes, me, mid, tx, tz) and not tor_reaches(tornadoes, me, me, me.x, me.z) then return false end
    local gx, gz = (want and want.x) or me.x, (want and want.z) or me.z
    -- a ball holder's dodge goes toward the seat joining it, and a joining
    -- seat's toward its holder (_vzslow t858-865: the holder fled its
    -- tornado away from the chain, the hop came late and the last link never
    -- reached the new holder -- the ball's final 74)
    if vz.guard_goal_tick == v.tick and vz.guard_goal ~= nil then gx, gz = vz.guard_goal.x, vz.guard_goal.z end
    -- the ball's forbidden tiles first; with none left, the tornado wins: a
    -- touch is half my hitpoints AND heals her (svbvzslow t823-830: two seats
    -- held 6436,92 under a ball with every dodge forbidden, and were caught)
    local best, bx, bz = nil, nil, nil
    for pass = 1, 2 do
        local honour = pass == 1 and vz.ball_forbid_tick == v.tick
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if ok(x, z) and not v.shadows[x * 100000 + z] and (b == nil or QD.raid._verzik_dist(x, z, b) >= 1)
                    and not tor_reaches(tornadoes, me, mid, x, z)
                    and not (honour and vz.ball_forbid(x, z)) then
                    local sc = math.max(math.abs(x - gx), math.abs(z - gz)) * 10 + math.max(math.abs(dx), math.abs(dz))
                    -- a dodge that keeps her in reach is a swing not lost:
                    -- the walk back after a dodge was what cancelled them (an
                    -- enrage of 290-325 ticks at 2-3 a tick, 60d10a2d0)
                    if b ~= nil and vz.reach_now ~= nil then
                        local db = QD.raid._verzik_dist(x, z, b)
                        if db >= 1 and db <= vz.reach_now then sc = sc - 15 end
                    end
                    if best == nil or sc < best then best, bx, bz = sc, x, z end
                end
            end
        end
        if bx ~= nil or vz.ball_forbid_tick ~= v.tick then break end
    end
    if bx == nil then return false end
    intent.walk = { x = bx, z = bz }
    intent.attack = false
    vz.tor_guards = (vz.tor_guards or 0) + 1
    if vz.bm ~= nil and not vz.bm.done then
        st.notes = st.notes or {}
        if #st.notes < 90 then st.notes[#st.notes + 1] = "g" .. v.tick .. ">" .. bx .. "," .. bz end
    end
    return true
end

QD.RAID_PLAY_VERZIK_COVER_REACH = 7
-- how far an npc row is listed from the client (the player-info view, 15)
QD.RAID_PLAY_VERZIK_NPC_VIEW = 15
-- ticks within which a second bolt row at one pillar is the same launch
QD.RAID_PLAY_VERZIK_BOLT_DEDUP = 7
QD.RAID_PLAY_VERZIK_PILLAR_HP = 185
QD.RAID_PLAY_VERZIK_PILLAR_HIT_MAX = 60
QD.RAID_PLAY_VERZIK_PILLAR_HIT_TOP = 60   -- a bolt takes 40-60 (tob.constant ^tob_verzik_pillar_hit_max)
function QD.raid._verzik_cover(st, v, ok)
    -- `me` was read below as a GLOBAL (be49fbb29): nil, so the first fallen
    -- pillar with no raider near enough to see it would end the script
    local b, O, me = v.boss, st.origin, v.me
    local cx = b.x + math.floor((b.size or 1) / 2)
    local best = nil
    -- owner_verzik 2026-10-07: THE BOLTS EACH PILLAR HAS TAKEN, seen as they
    -- fly (the bolt 1580 is aimed at the pillar's centre tile).  A pillar's bar
    -- is not always in view (the note below), and the probe run of watchverzik
    -- lost a raider 47 of 83 to a fall it read as a whole pillar: 6425,94 took
    -- bolts t81, t95, t109, t123 and fell on the fourth (t126) on p3 hiding on
    -- its loose tile.  185 hitpoints and 40-60 a bolt (V verzik.pillar_hp,
    -- pillar_hit; tob.constant ^tob_verzik_pillar_hit_min/_max) can leave it
    -- 60 or less after three: from the third bolt it MAY fall.
    local vzz = st.vz
    vzz.pillar_bolts = vzz.pillar_bolts or {}
    vzz.bolt_seen = vzz.bolt_seen or {}
    for _, pr in ipairs(v.proj or {}) do
        if pr.spotanim_id == st.plan.p1_bolt_proj then
            local key = tostring(pr.element_id) .. ":" .. tostring(pr.launched) .. ":" .. pr.dst_x .. "," .. pr.dst_z
            if not vzz.bolt_seen[key] then
                vzz.bolt_seen[key] = true
                -- ONE HIT A LAUNCH, whatever the rows say: one bolt read as two
                -- keys (probecov 2026-10-07: "60x2" after the first bolt, "60x4"
                -- after the second), so a pillar on 74 -- one bolt of 40-60 from
                -- safe -- read as may-fall and the trio gave up the near row
                -- with a covered bolt left in it.  Launches are 14 apart.
                vzz.pillar_bolt_tick = vzz.pillar_bolt_tick or {}
                for _, p in ipairs(v.pillars) do
                    if pr.dst_x >= p.x and pr.dst_x <= p.x + 2 and pr.dst_z >= p.z and pr.dst_z <= p.z + 2
                        and v.tick - (vzz.pillar_bolt_tick[p.slot] or -1000) > QD.RAID_PLAY_VERZIK_BOLT_DEDUP then
                        vzz.pillar_bolt_tick[p.slot] = v.tick
                        vzz.pillar_bolts[p.slot] = (vzz.pillar_bolts[p.slot] or 0) + 1
                    end
                end
            end
        end
    end
    -- owner_verzik 2026-10-07: THE SIX PILLARS ARE KNOWN, NOT DISCOVERED, so
    -- that all three raiders choose the SAME shadow.
    --
    -- `v.pillars` is the npc rows in MY view, and a raider standing at the near
    -- row cannot see the far one -- so each raider was ranking a different
    -- candidate list and they picked different pillars.  Measured: six bolt
    -- absorptions spread over FOUR attacks (1, 1, 2, 2), which is the trio
    -- split across two pillars, and 6425,94 fell on its third bolt at t131
    -- leaving the last six launches tanked by all three.
    --
    -- The positions are fixed (tob.constant ^tob_verzik_pillar_0..5_l[xz]: local
    -- x 25 west and 37 east, local z 18, 24, 30), so they are read from the
    -- room origin instead.  A pillar not in view is ASSUMED STANDING until it
    -- is seen to fall, which is the safe direction: the worst case is walking
    -- to a shadow that is not there, where the opposite error is tanking a
    -- bolt with cover available.
    --
    -- WHY ONE SHADOW FOR ALL THREE, sourced: the content charges a pillar ONE
    -- hit however many raiders hide behind it (~tob_verzik_p1_shot's $pillars
    -- bitmask -- "several players behind one pillar cost the pillar only one
    -- hit ... by construction"), and the wiki has the team hide behind one
    -- pillar together (W:885).  Blert's P1 positions agree: raiders beside her
    -- 51% and at the near pillar row 27%, never further out.  So a stacked
    -- trio gets SIX covered attacks out of the near row's two pillars, which
    -- is Blert's whole P1, and a split trio gets two.
    local seen = {}
    for _, pr in ipairs(v.pillars) do seen[pr.x * 100000 + pr.z] = pr end
    local known = {}
    for _, lz in ipairs({ 18, 24, 30 }) do
        for _, lx in ipairs({ 25, 37 }) do
            local px, pz = O.x + lx, O.z + lz
            local row = seen[px * 100000 + pz]
            local key = px * 100000 + pz
            vzz.pillar_was_seen = vzz.pillar_was_seen or {}
            vzz.pillar_fallen = vzz.pillar_fallen or {}
            -- a pillar whose npc row is in view but RETYPED away is rubble;
            -- v.pillars only carries the standing form, so a tile in view with
            -- no row and a raider close enough to see it is a fallen one --
            -- and so is one SEEN standing before and missing now from anywhere
            -- in view, for good (probecov4: the west near pillar fell, the
            -- trio moved to the east shadow 13 tiles from it, read it as
            -- "assumed standing" again, and walked for its rubble every launch
            -- from L127 to L183, tanking each bolt short of it at 6436,93)
            local dp = math.max(math.abs(px - me.x), math.abs(pz - me.z))
            if row ~= nil then
                vzz.pillar_was_seen[key] = true
            elseif dp <= 12 or (vzz.pillar_was_seen[key] and dp <= QD.RAID_PLAY_VERZIK_NPC_VIEW) then
                vzz.pillar_fallen[key] = true
            end
            if row ~= nil then
                known[#known + 1] = row
            elseif not vzz.pillar_fallen[key] then
                known[#known + 1] = { x = px, z = pz, slot = -(lx * 100 + lz), assumed = true }
            end
        end
    end
    for _, p in ipairs(known) do
        -- the lowest bar seen on it: the bar shows only for a while after a
        -- hit (s34v svb/sva: a bar gone read as a whole pillar, the raiders
        -- hid at both near pillars' loose tiles and both fell on them at t119)
        st.vz.pillar_hp = st.vz.pillar_hp or {}
        local hp = st.vz.pillar_hp[p.slot] or QD.RAID_PLAY_VERZIK_PILLAR_HP
        vzz.bar_bolts = vzz.bar_bolts or {}
        if p.health_ratio ~= nil and p.health_scale ~= nil and p.health_scale > 0 then
            local bar = math.floor(QD.RAID_PLAY_VERZIK_PILLAR_HP * p.health_ratio / p.health_scale + 0.5)
            if bar < hp then
                hp = bar
                vzz.bar_bolts[p.slot] = vzz.pillar_bolts[p.slot] or 0
            end
        end
        st.vz.pillar_hp[p.slot] = hp
        -- the bar, when one has been read, IS the pillar's hitpoints (185 *
        -- ratio / scale: probecov read 130, 74, 25 after one, two and three
        -- bolts); the bolts' count is only the estimate for a pillar whose bar
        -- this raider has never seen
        -- THE LEAST IT CAN HAVE LEFT, which every seat computes alike: the
        -- bar shows only for a while after a hit, so a seat that missed the
        -- third bolt's bar still read the second's 74 and hid there for a
        -- fourth (probecov5 L114: s2 west, s0 already going east).  So the
        -- last bar read, less the most a bolt takes for each bolt seen since,
        -- and never more than a whole pillar less the most for every bolt.
        local bolts = vzz.pillar_bolts[p.slot] or 0
        local since = bolts - (vzz.bar_bolts[p.slot] or 0)
        local least = math.min(hp - since * QD.RAID_PLAY_VERZIK_PILLAR_HIT_TOP,
            QD.RAID_PLAY_VERZIK_PILLAR_HP - bolts * QD.RAID_PLAY_VERZIK_PILLAR_HIT_TOP)
        if hp >= QD.RAID_PLAY_VERZIK_PILLAR_HP then
            least = QD.RAID_PLAY_VERZIK_PILLAR_HP - bolts * QD.RAID_PLAY_VERZIK_PILLAR_HIT_TOP
        end
        local may_fall = least <= QD.RAID_PLAY_VERZIK_PILLAR_HIT_MAX
        local west = (p.x - O.x) == 25
        local x0, x1, z0, z1 = p.x + 2, p.x + 4, p.z - 2, p.z
        local loose = { { p.x + 1, p.z - 1 }, { p.x + 3, p.z + 1 } }
        if west then
            x0, x1, z0, z1 = p.x - 3, p.x, p.z - 3, p.z
            loose = { { p.x + 1, p.z - 1 }, { p.x - 1, p.z + 1 } }
        end
        local tiles = {}
        for x = x0, x1 do
            for z = z0, z1 do tiles[#tiles + 1] = { x, z } end
        end
        for _, l in ipairs(loose) do tiles[#tiles + 1] = l end
        for _, tl in ipairs(tiles) do
            local x, z = tl[1], tl[2]
            -- the fall is measured from the pillar's EDGE (npc_range to its 3x3),
            -- not its centre: the survey on the plan fixes hid all three on
            -- 6425,91 -- four from 6425,94's centre, three from its edge --
            -- behind a pillar on 31, and its fall took 37, 64 and 54 (t161)
            local gx = (x < p.x) and (p.x - x) or ((x > p.x + 2) and (x - p.x - 2) or 0)
            local gz = (z < p.z) and (p.z - z) or ((z > p.z + 2) and (z - p.z - 2) or 0)
            local fall = math.max(gx, gz)
            -- (owner_verzik 2026-10-07: the fall reaches THREE from the centre
            -- now, ^tob_verzik_pillar_collapse_range 3 after Blert 280f7cef, the
            -- anim audit's content change: a tile four out is never caught)
            -- owner_verzik 2026-10-07: A WORN PILLAR IS NOT WORTH HIDING
            -- BEHIND, MEASURED.  Only a WEST pillar's far corner is both
            -- behind it and outside the 3-tile fall (an east pillar's whole
            -- shadow is inside it), so this test leaves the trio with no cover
            -- at all once the near row has taken its bolts: _play_verzik
            -- stopped hiding from L132 with all six pillars still standing
            -- (hides L64..L118, then none) and tanked 9 bolts for 329.
            --
            -- Letting a worn pillar count anyway was tried, on _vzslow, and it
            -- is WORSE on both sides of the trade: P1 229 ticks against 163
            -- and 245 hp a seat against 185, because the walk to a shadow five
            -- or six tiles out and back costs four ticks each way against a
            -- bolt worth 68 at its very worst under the prayer -- and the
            -- phase lengthening buys her more launches, which is the spiral.
            -- P2 then ran 964 ticks on a trio that arrived to it already
            -- eating. The answer to the tanked bolts is a SHORTER P1 (more
            -- Dawnbringer specials: Blert's P1 median is 85 ticks, six
            -- launches, against our 163), not more hiding. Kept as the test
            -- it was, with the number measured against it.
            -- owner_verzik 2026-10-07: THE REACH BOUND IS PART OF THE CHOICE,
            -- not a test on its answer.  The caller discards a cover tile
            -- further than this ("a far shadow is no cover"), so a choice that
            -- ranked the far row's WHOLE pillars above the near row's worn
            -- ones handed back a tile the caller then threw away, and the trio
            -- tanked with cover four tiles from it.  That is why _vzslow's P1
            -- did not move one hitpoint when worn pillars became cover again.
            if (fall >= 4 or not may_fall) and ok(x, z)
                and QD.raid._verzik_dist(x, z, b) <= QD.RAID_PLAY_VERZIK_COVER_REACH then
                -- owner_verzik 2026-10-07: A PILLAR THAT CANNOT FALL BEATS A
                -- NEARER ONE THAT MAY, outright rather than by a tile.  The
                -- near row is two pillars and three safe bolts each, and
                -- "several players behind one pillar cost the pillar only one
                -- hit" (tob_verzik.rs2 ~tob_verzik_p1_shot, the $pillars
                -- bitmask), so a trio that stacks has six covered attacks --
                -- Blert's P1 median is 85 ticks, which is six.  _vzslow spent
                -- its six absorptions over FOUR attacks (1, 1, 2, 2: the trio
                -- split across two pillars), 6425,94 fell on its third at
                -- t131, and the last six launches of that P1 were tanked by
                -- all three: 505 of P1's 556 damage, 168 a seat against
                -- Blert's 20.  A +1 on the distance was not enough to move
                -- them: a spent pillar two tiles nearer still won.
                local d = QD.raid._verzik_dist(x, z, b)
                -- the choice ranks on this; `d` stays the true distance, which
                -- the caller's "a far shadow is no cover" test reads
                local rank = d + (may_fall and 100 or 0)
                -- ties: the west pillar first (W:885 "the pillar directly
                -- south-west of Verzik"), then the tile nearer her centre line,
                -- so all three raiders read the same tile from anywhere
                local side = math.abs(x - cx) + (west and 0 or 100)
                if best == nil or rank < best.rank or (rank == best.rank and side < best.side) then
                    best = { x = x, z = z, d = d, rank = rank, side = side, pillar = p, hp = hp, may_fall = may_fall }
                end
            end
        end
    end
    return best
end

-- THE DAWNBRINGER, shared.  W:875 "its special attack deals 75-150 magic
-- damage against her shield ... thus requiring players to drop the Dawnbringer
-- for the next player (in orb order) to use"; the owner (2026-10-05): "the
-- players should share it using their special attack".  One per raid
-- (tob_xarpus.rs2 [proc,tob_dawnbringer_take]): p1 starts with it.  Its
-- special costs 350 of the orb's 1000 (skill_combat special_attack.obj
-- [verzik_special_weapon] sa_energy 350), so every raider has two.  The
-- holder wields it, arms the special from the orb with the attack press
-- (raid seam32 intent.spec; DRIVER_NOTES "Bloat: Defence reads 80 of 80"), and
-- SEES each special as the 350 it spends (varp300).  Spent, the scythe goes
-- back on and the sword is dropped on the cover tile (W:887 "'416' or 'pillar
-- drop', is to simply drop the Dawnbringer behind the pillar"), where the
-- others hide.  The order is seen, not told: p(r) takes the sword the
-- (r-1)th time it appears on the floor.
-- How long the bounce chain is held together after the ball's projectile has
-- gone. The hops are one a tick (_vzslowp3: landed on p1 at t267, hopped to p2
-- at t268), so a trio needs two; four leaves room for a hop that waits a tick
-- and still ends the hold well before her next special.
QD.RAID_PLAY_VERZIK_BALL_CHAIN_TICKS = 4
-- the ledger's reading of a ball: hops are 6-8 ticks apart (content
-- 552c599394), a throw is a rotation apart; three hops land within 30
QD.RAID_PLAY_VERZIK_BALL_HOP_GAP = 12
-- the enrage played on her, not circled (see the P3 tick)
QD.RAID_PLAY_VERZIK_POWER_THROUGH = true
QD.RAID_PLAY_VERZIK_BALL_CHAIN_SPAN = 30
QD.RAID_PLAY_VERZIK_DAWN_COST = 350

-- ported to raid_sm 2026-10-07: THE DAWNBRINGER, three states.
--   ABSENT  not carrying it.  It is taken off the floor on my turn: p(r)
--           takes the sword the (r-1)th time it appears there, which is how
--           the order is SEEN rather than told.
--   HELD    carrying it: wielded, then a special every five ticks for as long
--           as the orb holds the 350 (owner_verzik 2026-10-07: as many as the
--           orb holds, not two -- Blert's 27 Normal trio rooms spend 9-11 a
--           room, 3-4 a raider, 111.5 a special, and the orb's regeneration
--           while the staff goes round pays for the third; with two a raider
--           the shield took 6, P1 ran 123-151 ticks against Blert's median 85,
--           and the trio tanked 4-5 bolts each).  Spent, the main weapon goes
--           back on and the sword is dropped where the others hide (W:887
--           "'416' or 'pillar drop'").
--   DONE    dropped, or kept because I am the last holder (nobody is left to
--           take it, and her shield breaking destroys it anyway,
--           tob_verzik.rs2 ~tob_verzik_shield_broken).
--
-- HELD is deliberately ONE state and not the four it looks like (wield, arm,
-- unwield, drop).  The old body did two of those in a single tick -- it saw
-- the orb go flat and put the main weapon back on the SAME tick -- and it
-- re-checked the weapon in hand every tick, so a swap by any other part of
-- the plan was undone.  Splitting it into sub-states either delays the
-- unwield by a tick or drops the re-check.  Neither is worth a prettier
-- declaration, so the compound stays whole and this comment says why.
-- ==========================================================================
-- raid seam53 hierarchy, owner 2026-10-07 ("you should be using state machines
-- or hierarchical state machines; why did you go back to boolean soup?").
--
-- THE SWORD'S TURN, AS TWO MACHINES, because two clocks cross here.
--
-- Her bolt cadence is 14 ticks, exact in all 105 of Blert's P1 gaps, and it
-- cuts across every stage of the sword's turn: a holder on a cover tile cannot
-- arm a special, whatever stage its turn is at.  So the BOLT CYCLE is the
-- parent and the TURN is its child, live only while exposed:
--
--   verzik_bolt   EXPOSED   her next bolt is far enough off to use the sword
--                 HIDING    behind a pillar for the launch; the child is
--                           DORMANT, which is the layer's resume semantics --
--                           suspended, not cancelled, so the turn picks up at
--                           the stage it reached (raid_sm.lua:52-101)
--
--   verzik_sword  ABSENT      not carrying it; take it on my turn
--                 WIELD       carrying it, putting it on
--                 ARMED       wielded, ready to fire
--                 FIRED_ONCE  one special spent
--                 BETWEEN     inside the five-tick spacing
--                 FIRED_TWICE two spent
--                 SPENT       the orb cannot pay for another
--                 PASSING     main weapon back on, dropping it for the next
--                 DONE        dropped, or nobody left to pass to
--
-- WHY THE STAGES ARE WORTH THE LINES.  Before this, "the special was
-- suppressed because I am hiding" and "my turn is over" were the SAME THING:
-- both were the absence of a spec intent from inside one `held` body.  A seat
-- could walk off its turn holding 300 unspent energy and nothing could point
-- at it.  Measured per seat, that is exactly what happened -- 2, 2, 0 specials
-- on the two 163-tick P1s, the third seat ending with a FULL 1000 orb -- and a
-- room total of six hid it, because six is also what two each looks like.
--
-- Now the fault is ASSERTABLE: PASSING carries a premise that it has nothing
-- left to fire.  If a turn reaches PASSING while the orb still holds the 350,
-- the premise breaks and the machine goes back to ARMED and fires it, instead
-- of carrying the special away unspent.  The premise is both the check and the
-- fix, which is why it is a premise rather than a counter.
QD.raid.sm_declare("verzik_bolt", {
    start = "EXPOSED",
    states = {
        EXPOSED = { note = "her bolt is far enough off to use the sword",
            children = { "verzik_sword" },
            on = { bolt_cycle = function(c, ev) return (ev.hiding and nil or nil), (ev.hiding and "HIDING" or nil) end } },
        HIDING  = { note = "behind a pillar for the launch; the turn is suspended",
            on = { bolt_cycle = function(c, ev) return nil, (ev.hiding and nil or "EXPOSED") end } },
    },
})

QD.raid.sm_declare("verzik_sword", {
    start = "IDLE",
    states = {
        -- THE HANDOVER IS TWO STAGES WITH THEIR OWN TIMING, not a consequence
        -- of where anyone happens to stand.  IDLE is "the sword is not on the
        -- floor in my view" -- somebody else has it, or it has not been
        -- dropped yet -- and CLAIMING is "it is lying there and I am going for
        -- it".  Before this both were one ABSENT, so a sword lying unclaimed
        -- and a sword in another raider's hand were the same stage, and the
        -- phase's biggest cost was invisible: 93 of P1's ticks with the sword
        -- on the floor, against Blert's 4-to-7-tick gaps between specials.
        IDLE        = { note = "the sword is not on the floor in my view", on = {
            bolt_cycle = function(c, ev) return QD.raid._verzik_sword_idle(c, ev) end } },
        -- The premise is the loud part: I am only claiming while it is still
        -- there to claim.  If it goes -- someone else reached it first -- this
        -- breaks straight back to IDLE instead of pressing at an empty tile.
        -- "There" is the floor OR MY PACK: my own take empties the floor, and
        -- the premise is read before the handler, so a floor-only premise
        -- broke on the take's own success and left the sword unwielded in
        -- the claimer's pack (probetake on 76f061b63: CLAIMING>IDLE/
        -- premise_broken on both claimers, the room's specials stopping at 2).
        CLAIMING    = { note = "it is lying there and I am going for it",
            premise = function(c, ev) return c.floor_now == true or c.in_pack == true end,
            broken = "IDLE",
            on = { bolt_cycle = function(c, ev) return QD.raid._verzik_sword_claiming(c, ev) end } },
        -- A FULL PACK TAKES NOTHING: pickup.rs2 [label,pickup_obj_floor]
        -- refuses with ~inv_no_space_message.  Every slow run on 76f061b63
        -- had the second seat stand on the sword pressing Take (ten times in
        -- _vzslow) with 28 of 28 slots used, so the sword lay there for the
        -- rest of P1 and the room got two specials of Blert's nine to eleven.
        -- ROOM is the claim's own step: one food out of the pack, then back.
        ROOM        = { note = "pack full: one food out to make the slot",
            premise = function(c, ev) return c.floor_now == true or c.in_pack == true end,
            broken = "IDLE",
            on = { bolt_cycle = function(c, ev) return QD.raid._verzik_sword_room(c, ev) end } },
        WIELD       = { note = "putting it on", on = {
            bolt_cycle = function(c, ev) return QD.raid._verzik_sword_wield(c, ev) end } },
        ARMED       = { note = "wielded and ready to fire", on = {
            bolt_cycle = function(c, ev) return QD.raid._verzik_sword_fire(c, ev, "FIRED_ONCE") end } },
        FIRED_ONCE  = { note = "one special spent", on = {
            bolt_cycle = function(c, ev) return QD.raid._verzik_sword_after(c, ev) end } },
        BETWEEN     = { note = "inside the five-tick spacing", on = {
            bolt_cycle = function(c, ev) return QD.raid._verzik_sword_between(c, ev) end } },
        FIRED_TWICE = { note = "two specials spent", on = {
            bolt_cycle = function(c, ev) return QD.raid._verzik_sword_after(c, ev) end } },
        SPENT       = { note = "the orb cannot pay for another", on = {
            bolt_cycle = function(c, ev) return QD.raid._verzik_sword_spent(c, ev) end } },
        -- THE PREMISE IS THE MEASUREMENT: a turn only passes the sword on when
        -- it has nothing left to fire.  Reaching here with the 350 still in
        -- the orb is the dropped special the owner asked about, and it goes
        -- back to ARMED to spend it rather than carrying it away.
        PASSING     = { note = "main weapon back on, dropping it for the next raider",
            premise = function(c, ev) return QD.raid._verzik_sword_may_pass(c, ev) end,
            broken = "ARMED",
            on = { bolt_cycle = function(c, ev) return QD.raid._verzik_sword_passing(c, ev) end } },
        DONE        = { note = "dropped, or nobody left to pass to" },
    },
})

-- The main weapon back in my hand -- the scythe, or the slow pace's halberd
-- (the plan header's table).  One place, because it is reached from two: the
-- orb going flat, and her shield breaking with the sword still in my hand.
function QD.raid._verzik_main_weapon_back(st, v, vz, intent)
    assert(st, "_verzik_main_weapon_back: st")
    assert(vz, "_verzik_main_weapon_back: vz")
    assert(intent, "_verzik_main_weapon_back: intent")
    local main = vz.main or "scythe"
    intent.gear = { QD.RAID_PLAY_VERZIK_WEAPONS[main].item }
    vz.held = main
    st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS[main]
    QD.raid._verzik_engage(st, v, "rearmed")
end

-- IDLE: nothing to do until the sword is lying there in view.  The moment it
-- is, decide whether this is my claim -- and if it is, move to CLAIMING, which
-- owns the walk and the press.
--
-- BLERT-SOURCED, THE SWORD GOES ROUND MORE THAN ONCE: 9-11 specials a room
-- (median 10) at 111.8 damage, about 1118 of the 1500 P1 pool, with 4-7 tick
-- gaps, so the sword is in somebody's hand for nearly all of P1.  A 1000 orb
-- at 350 a special buys TWO, and three raiders passing it once is six; the
-- other four come from the orb REGENERATING while it goes round.  So the turn
-- is modular: raider r claims on the (r-1)th appearance, the (r-1+party)th,
-- and so on, and a raider whose orb cannot pay leaves it for whoever can.
--
-- THE TURN DOES NOT STALL ON A RAIDER WHO CANNOT TAKE IT.  svavzslow is the
-- proof and the stage counts named it in three lines: p1 a clean turn and a
-- drop (PASSING 3/1), then p2 ABSENT 17/1 with every other stage 0/0 because
-- p2 DIED, and p3 ABSENT 219/1 waiting for an appearance that never came.  The
-- sword lay on the floor for the rest of P1 -- two full 1000 orbs unspent, 14
-- bolt launches tanked, 239 ticks against the 127-147 the other names ran.  So
-- the claim is a turn PLUS a timeout, ordered by role so the trio does not all
-- grab at once, and it recovers the sword from ANY stalled turn rather than
-- only from a death.
function QD.raid._verzik_sword_idle(c, ev)
    assert(c, "_verzik_sword_idle: c")
    assert(ev, "_verzik_sword_idle: ev")
    local st, v, dw = c.st, c.v, c.dw
    -- in my pack and not in my hand: a take that landed after CLAIMING let
    -- go (or a pick-up nobody planned) is still a sword to fire
    if c.in_pack and c.vz.held ~= "dawnbringer" then
        dw.took = v.tick
        dw.e_prev = ev.energy
        dw.lying_since = nil
        dw.took_late = (dw.took_late or 0) + 1
        return nil, "WIELD"
    end
    if not c.floor_now then
        dw.lying_since = nil
        return
    end
    dw.lying_since = dw.lying_since or v.tick
    if ev.energy < QD.RAID_PLAY_VERZIK_DAWN_COST then return end
    local turn = dw.appear - (st.role - 1)
    local mine = turn >= 0 and (st.party <= 0 or turn % st.party == 0)
    local lying = v.tick - dw.lying_since
    local waited = lying >= QD.RAID_PLAY_VERZIK_TURN_WAIT * st.role
    if not (mine or waited) then return end
    if waited and not mine then dw.took_abandoned = (dw.took_abandoned or 0) + 1 end
    dw.claim_from = v.tick
    return nil, "CLAIMING"
end

-- CLAIMING: the walk and the press, as ONE stage with its own clock, so the
-- handover can be measured and bounded instead of being whatever the cover
-- logic happened to leave.  `dw.claim_ticks` is what it cost.
function QD.raid._verzik_sword_claiming(c, ev)
    assert(c, "_verzik_sword_claiming: c")
    assert(ev, "_verzik_sword_claiming: ev")
    local st, v, dw, intent = c.st, c.v, c.dw, c.intent
    if c.in_pack then
        dw.took = v.tick
        dw.e_prev = ev.energy
        dw.claim_ticks = (dw.claim_ticks or 0) + (v.tick - (dw.claim_from or v.tick))
        dw.lying_since = nil
        return nil, "WIELD"
    end
    if QD.raid._verzik_pack_free() == 0 then return nil, "ROOM" end
    -- not in the pack yet: ask for it as this tick's intent, so the bolt
    -- parent can still pull this raider into cover between presses
    intent.take = { obj = "verzik_special_weapon", op = 3 }
    c.busy = true
end

-- The empty slots in my pack (an empty slot reads obj -1, QD.inv.slot).
function QD.raid._verzik_pack_free()
    local free = 0
    for i = 0, QD.RAID_PLAY_VERZIK_PACK_SLOTS - 1 do
        local r, slot = QD.inv.slot(i)
        assert(r == "ok", "_verzik_pack_free: slot " .. i .. ": " .. tostring(slot))
        if slot.count == 0 then free = free + 1 end
    end
    return free
end
QD.RAID_PLAY_VERZIK_PACK_SLOTS = 28
-- the first P1 wind-up, earliest seen after the plan's first P1 tick (15, 16)
QD.RAID_PLAY_VERZIK_FIRST_WINDUP = 14

-- ROOM: one food out.  Eaten when at least half its heal lands under the
-- level (the eat policy's own test, raid_play.lua `lands`), else dropped:
-- a dropped fish costs no eat delay, and the specials the slot buys are
-- worth more than one fish.  Back to CLAIMING once the slot shows.
function QD.raid._verzik_sword_room(c, ev)
    assert(c, "_verzik_sword_room: c")
    assert(ev, "_verzik_sword_room: ev")
    local st, v, dw, intent = c.st, c.v, c.dw, c.intent
    if c.in_pack or QD.raid._verzik_pack_free() > 0 then return nil, "CLAIMING" end
    if dw.room_tick ~= nil and v.tick - dw.room_tick < QD.RAID_PLAY_VERZIK_ROOM_WAIT then
        c.busy = true
        return
    end
    local food, heal = nil, 0
    for _, f in ipairs(QD.RAID_PLAY_FOOD) do
        local r, n = QD.inv.count(f.item)
        if r == "ok" and n > 0 then food, heal = f.item, f.heal break end
    end
    assert(food, "_verzik_sword_room: a full pack with no food in it")
    dw.room_tick = v.tick
    dw.room_n = (dw.room_n or 0) + 1
    if intent.eat == nil and math.min(heal, v.hp_base - v.hp) * 2 >= heal
        and v.tick - st.last_eat >= QD.RAID_PLAY_EAT_DELAY then
        intent.eat = food
        dw.room_ate = (dw.room_ate or 0) + 1
    else
        local dr, dd = QD.player.drop(food)
        if dr ~= "ok" and #st.lines < 6 then
            st.lines[#st.lines + 1] = "t" .. v.tick .. " room drop " .. tostring(dr) .. ": "
                .. string.sub(tostring(dd), 1, 120)
        end
        dw.room_dropped = (dw.room_dropped or 0) + 1
    end
    c.busy = true
end
-- the pack update rides the next tick's inventory packet
QD.RAID_PLAY_VERZIK_ROOM_WAIT = 2

-- WIELD: the sword goes on.  Its own tick, because the equip IS the tick's
-- work -- the old body did this and the first special in one `held` pass and
-- the ordering was a comparison rather than a transition.
function QD.raid._verzik_sword_wield(c, ev)
    assert(c, "_verzik_sword_wield: c")
    assert(ev, "_verzik_sword_wield: ev")
    local st, v, vz, intent = c.st, c.v, c.vz, c.intent
    if vz.held ~= "dawnbringer" then
        intent.gear = { "verzik_special_weapon" }
        vz.held = "dawnbringer"
        st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS.dawnbringer
        QD.raid._verzik_engage(st, v, "rearmed")
        c.busy = true
        return
    end
    return nil, "ARMED"
end

-- ARMED / BETWEEN -> fire.  The five-tick spacing is a TRANSITION now: a
-- special arms here and the next one waits in BETWEEN until the gap is served.
-- (Its autos ignore her P1 cap too -- tob_damage.rs2 ~tob_verzik_p1_cap is the
-- weapon, not the swing.)
function QD.raid._verzik_sword_fire(c, ev, go)
    assert(c, "_verzik_sword_fire: c")
    assert(ev, "_verzik_sword_fire: ev")
    local st, v, dw, intent = c.st, c.v, c.dw, c.intent
    if ev.energy < QD.RAID_PLAY_VERZIK_DAWN_COST then return nil, "SPENT" end
    dw.arm_tick = v.tick
    intent.spec = true
    intent.attack = true
    c.busy = true
    return nil, go
end

-- FIRED_ONCE / FIRED_TWICE: one tick to see the energy go, then either the
-- spacing or the end of the turn.
function QD.raid._verzik_sword_after(c, ev)
    assert(c, "_verzik_sword_after: c")
    assert(ev, "_verzik_sword_after: ev")
    if ev.energy < QD.RAID_PLAY_VERZIK_DAWN_COST then return nil, "SPENT" end
    return nil, "BETWEEN"
end

function QD.raid._verzik_sword_between(c, ev)
    assert(c, "_verzik_sword_between: c")
    assert(ev, "_verzik_sword_between: ev")
    if ev.energy < QD.RAID_PLAY_VERZIK_DAWN_COST then return nil, "SPENT" end
    if c.v.tick < (c.dw.arm_tick or -1000) + 5 then return end
    return QD.raid._verzik_sword_fire(c, ev, "FIRED_TWICE")
end

-- SPENT: the main weapon goes back on, then the turn is passed.
function QD.raid._verzik_sword_spent(c, ev)
    assert(c, "_verzik_sword_spent: c")
    assert(ev, "_verzik_sword_spent: ev")
    local st, v, vz, dw, intent = c.st, c.v, c.vz, c.dw, c.intent
    if vz.held == "dawnbringer" then
        QD.raid._verzik_main_weapon_back(st, v, vz, intent)
        dw.unwield = v.tick
        -- the swap's tick is the turn's, not an attack's: probecov4's holder
        -- swapped at t96, pressed her the same tick, walked toward her and was
        -- a tile short of cover when the L100 bolt read it (58)
        c.busy = true
        return
    end
    return nil, "PASSING"
end

-- THE PREMISE ON PASSING: a turn only passes the sword on when it has nothing
-- left to fire.  If the orb has recovered past the cost while the turn was
-- ending -- or if the turn ever reaches here with it unspent, which is the
-- defect the owner asked about -- this breaks and the machine goes back to
-- ARMED to spend it.  `dw.premise_breaks` counts it so the row can say so.
function QD.raid._verzik_sword_may_pass(c, ev)
    assert(c, "_verzik_sword_may_pass: c")
    assert(ev, "_verzik_sword_may_pass: ev")
    if ev.energy >= QD.RAID_PLAY_VERZIK_DAWN_COST then
        c.dw.premise_breaks = (c.dw.premise_breaks or 0) + 1
        c.dw.premise_at = c.dw.premise_at or c.v.tick
        return false
    end
    return true
end

-- PASSING: dropped where the others hide (W:887 "'416' or 'pillar drop'"), so
-- the next raider in orb order finds it.  Back to ABSENT, not DONE: the sword
-- comes round again and this raider's orb regenerates past the cost by then.
-- DONE is for the raider with nobody left to pass to.
-- How long the sword lies unclaimed before a raider whose turn it is not takes
-- it anyway, multiplied by role so the trio does not all grab at once.  A
-- raider who has died never takes its turn, and before this the whole rotation
-- stalled behind it (svavzslow: the sword on the floor for the rest of P1).
QD.RAID_PLAY_VERZIK_TURN_WAIT = 5
-- How long a spent holder walks for its cover tile before dropping where it
-- stands: a shadow is within 7 of her (COVER_REACH) and the holder beside her,
-- so four ticks of running at two a tile, plus the click's tick and one spare.
QD.RAID_PLAY_VERZIK_PASS_WAIT = 6

function QD.raid._verzik_sword_passing(c, ev)
    assert(c, "_verzik_sword_passing: c")
    assert(ev, "_verzik_sword_passing: ev")
    local st, v, dw = c.st, c.v, c.dw
    -- owner_verzik 2026-10-07: THE DROP DOES NOT WAIT FOR COVER FOREVER, and
    -- the staged machine is what made this visible.  The cover tile is where
    -- the sword SHOULD land (W:887 "'416' or 'pillar drop'", so the next
    -- raider finds it where the team is hiding), but it was a REQUIREMENT:
    -- `c.on_cover and ...`, with no other way out of the stage.  A holder that
    -- could not reach cover held the sword indefinitely.
    --
    -- Measured, per seat, from the stage counts this rebuild gave:
    --   p1  ABSENT 120/2  WIELD 1/1  ARMED 1/1  FIRED_ONCE 1/1  BETWEEN 5/2
    --       FIRED_TWICE 1/1  SPENT 2/1  PASSING 4/1      -- a clean turn
    --   p2  ABSENT 20/1   WIELD 2/1  ARMED 1/2  ...       PASSING **30/1**
    --   p3  ABSENT **137/1** and every other stage 0/0    -- NEVER A TURN
    -- p2 sat in PASSING for THIRTY TICKS waiting to be on its cover tile, so
    -- the sword never appeared on the floor a second time, so p3's turn --
    -- which is the (role-1)th appearance -- never came at all.  That is the
    -- full 1000 orb and the two unspent specials the owner asked about, and it
    -- was invisible while suppression and completion were the same absence.
    --
    -- So: cover if I can get there, the tile I stand on if I have waited.  The
    -- next raider's take needs the sword IN VIEW, not on a particular tile.
    --
    -- AND THE LAST RAIDER PASSES IT ON TOO.  It used to keep the sword ("nobody
    -- is left to take it, and her shield breaking destroys it anyway"), which
    -- ends the circulation after ONE round and caps the room at six specials --
    -- two each.  Blert spends 9-11, which is three to four each, so the sword
    -- has to go round more than once: with the modular turn, raider 1 takes it
    -- again on the third appearance, by which time its orb has regenerated
    -- past the cost.  PASSING's premise already guarantees nobody passes a
    -- sword they could still fire, so circulating is safe by construction.
    dw.pass_from = dw.pass_from or v.tick
    -- THE DROP GOES ON THE COVER TILE (owner 2026-10-07, watching watchvz:
    -- "players should focus on dropping the dawnbringer in the column safe
    -- spot").  Waiting for the hide window to bring me there is what made the
    -- `waited` fallback necessary, and the fallback put the sword on 6427,93 --
    -- outside every shadow -- so its claimer took the bolt standing on it
    -- (svavzslow t72).  So the holder WALKS to the cover tile as soon as the
    -- orb is spent and drops it there; the next raider picks it up hiding.
    -- With no cover at all (every near shadow is out of reach) it drops where
    -- it stands.
    local cover = c.vz.cover
    local waited = cover == nil or v.tick - dw.pass_from >= QD.RAID_PLAY_VERZIK_PASS_WAIT
    if cover ~= nil and not c.on_cover and not waited then
        c.intent.walk = { x = cover.x, z = cover.z }
        c.intent.attack = false
        c.busy = true
    end
    if (c.on_cover or waited) and st.role < st.party then
        local dr, dd = QD.player.drop("verzik_special_weapon")
        st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
        dw.drop_result = tostring(dr) .. ": " .. string.sub(tostring(dd), 1, 120)
        local cr, n = QD.inv.count("verzik_special_weapon")
        if cr == "ok" and n == 0 then
            dw.dropped = v.tick
            dw.rounds = (dw.rounds or 0) + 1
            dw.pass_waited = (dw.pass_waited or 0) + (v.tick - dw.pass_from)
            dw.pass_from = nil
            return nil, "IDLE"
        end
    end
end

-- The tick's reading the machine acts on -- the specials SEEN as the energy
-- they spend, and whether the sword is lying anywhere in view -- then the
-- machine.  Returns `busy`: true on the ticks the Dawnbringer spent the tick.
function QD.raid._verzik_dawn(st, v, intent, hiding, on_cover, events)
    assert(st, "_verzik_dawn: st")
    assert(v, "_verzik_dawn: v")
    assert(intent, "_verzik_dawn: intent")
    assert(type(events) == "table", "_verzik_dawn: events")
    local vz = st.vz
    local dw = vz.dawn
    local _, energy = QD.var.varp("varp300_sa_energy")
    energy = tonumber(energy) or 0
    if dw == nil then
        local cr, n = QD.inv.count("verzik_special_weapon")
        local _, oid = api_drive.symbol("obj", "verzik_special_weapon")
        dw = { specs = {}, appear = 0, on_floor = false, e_prev = energy,
            arm_tick = -1000, obj = oid, took = nil, dropped = nil, refused = {} }
        vz.dawn = dw
        -- "am I carrying it?" is the caller's question, answered once, here.
        -- WIELD rather than ARMED: the sword still has to go on.
        if cr == "ok" and n > 0 then
            QD.raid.sm_force(st, v, "verzik_sword", "WIELD",
                { st = st, v = v, vz = vz, dw = dw, intent = intent }, "carries_sword")
        end
    end
    -- the special is SEEN as the energy it spends
    if energy <= dw.e_prev - (QD.RAID_PLAY_VERZIK_DAWN_COST - 50) then
        dw.specs[#dw.specs + 1] = v.tick
    end
    dw.e_prev = energy
    -- the floor: is the sword lying anywhere in view?
    local floor_now = false
    local orr, rows = api_drive.objs(0)
    if orr == "ok" and dw.obj ~= nil then
        for _, r in ipairs(rows) do
            if r.obj_id == dw.obj then floor_now = true dw.floor_at = { x = r.x, z = r.z } end
        end
    end
    if floor_now and not dw.on_floor then dw.appear = dw.appear + 1 end
    dw.on_floor = floor_now
    local pr, held_n = QD.inv.count("verzik_special_weapon")
    assert(pr == "ok", "_verzik_dawn: inv count: " .. tostring(held_n))
    local c = { st = st, v = v, vz = vz, dw = dw, intent = intent,
        hiding = hiding, on_cover = on_cover, floor_now = floor_now, in_pack = held_n > 0,
        busy = false }
    -- raid seam53 hierarchy: ONE event for both machines, carrying the tick's
    -- two readings -- whether a bolt has me on a cover tile, and what the orb
    -- holds.  The parent (verzik_bolt) switches on `hiding` and the layer
    -- steps the child (verzik_sword) only while EXPOSED, so hiding SUSPENDS
    -- the turn at whatever stage it reached instead of looking like its end.
    --
    -- Her shield destroying the sword in my hand is NOT handled here any more:
    -- that is p1's `exit` hook (the boundary) and verzik_weapon (the raider),
    -- which is where the layer's header says those two belong.
    local m = QD.raid.sm_run(st, v, "verzik_bolt", c,
        { { name = "bolt_cycle", hiding = hiding and true or false, energy = energy } })
    dw.state = m.state
    local sw = QD.raid.sm_at(st, "verzik_sword")
    dw.turn = sw and sw.state or nil
    return c.busy
end

-- P1, Normal trio (W:871-891).  Protect from Magic all phase (W:871), Piety.
-- Every bolt is HIDDEN from (W:883 "hide behind one pillar together, as the
-- magic attack will be launched each pillar that is hiding a player"; the
-- owner: "the pillars hide the bolts"): a Normal bolt is 68 under the prayer
-- (V verzik.p1_max_hit), the Entry plan's tank (W:887) is not affordable for
-- three raiders over the whole phase.  The bolt's cover is settled on its
-- launch tick, three after the wind-up (V p1_verdict_tick, p1_launch 3), the
-- first wind-up about 19 ticks in (V p1_first_windup, B), then every 14 (V
-- p1_cadence, B).  So: on the cover tile by the tick before the launch, out
-- on the launch tick, swing until the walk back would miss the next one
-- (W:883 "two hits by any 4 or 5-tick weapon before requiring to hide").
-- Returns the threat function.
function QD.raid._verzik_p1_normal(st, v, intent, ok, go, events)
    local P, N, vz, b, me = st.plan, st.numbers, st.vz, v.boss, v.me
    intent.want.protectfrommagic = true
    intent.want.piety = true
    vz.p1_start = vz.p1_start or v.tick
    if v.attack ~= nil and v.attack.seq == P.p1_windup then
        vz.W = v.attack.tick
        vz.windups[#vz.windups + 1] = v.attack.tick
    end
    if v.attack ~= nil and v.attack.seq == P.p1_death then
        vz.dying, vz.dying_tick = true, v.attack.tick
    end
    -- the next launch: from the last wind-up seen, else the first one's
    -- expected tick (V p1_first_windup 19)
    local L
    if vz.W ~= nil then
        L = vz.W + P.p1_launch
        while L < v.tick do L = L + P.p1_cadence end
    else
        -- our server's first wind-up came 16-17 ticks after the plan's first
        -- P1 tick (s34v: t57 from t41), two before V's 19 from the room
        -- start; the early figure, so the first bolt finds the trio hidden
        -- (s34v: every raider was hit by the t60 bolt with 19).  It is
        -- EARLIEST, not typical: svavzslow on 76f061b63 wound up 15 after the
        -- first tick (t69 from t54), the 16 here put the launch at t73 against
        -- the server's t72, and the trio reached cover at the end of t72 --
        -- the launch reads the tile of the tick before -- so every raider
        -- took the first bolt.  A tick early only hides one tick sooner; the
        -- wind-up, once seen, sets the launch exactly.
        L = vz.p1_start + QD.RAID_PLAY_VERZIK_FIRST_WINDUP + P.p1_launch
        while L < v.tick do L = L + P.p1_cadence end
    end
    local cover = QD.raid._verzik_cover(st, v, ok)
    -- a far shadow is no cover: past the near row (s34v vzn6 t183-236: the
    -- middle pillars' shadows are 10+ from her, the trio walked between
    -- 6424,83 and 6427,93 for 58 ticks and never reached her) the bolts are
    -- TANKED under Protect from Magic (W:887 "even tank her attacks
    -- entirely, to avoid losing out on ticks")
    if cover ~= nil and cover.d > QD.RAID_PLAY_VERZIK_COVER_REACH then
        if vz.tank_from == nil then vz.tank_from = v.tick end
        cover = nil
    end
    vz.cover = cover
    local on_cover = cover ~= nil and me.x == cover.x and me.z == cover.z
    local travel = 0
    if cover ~= nil then
        local dc = math.max(math.abs(me.x - cover.x), math.abs(me.z - cover.z))
        travel = math.ceil(dc / 2)
        -- a long walk -- the switch from one near pillar to the other goes
        -- round her body -- is longer than its straight line: probecov3's
        -- trio was two tiles short of 6438,93 at L128 and L142, coming from
        -- the west shadow
        if dc > 6 then travel = travel + 1 end
    end
    -- hide from (L - 2 - travel) through L - 1 (a walk sent on tick T moves
    -- on T + 1, two tiles a tick; one tick more for the walk round the
    -- pillar: s34v _play_verzik t84-88, the leader's route from 6430,98 to
    -- 6426,93 took four ticks, not three, and the bolt found it); the launch
    -- tick itself is the way out
    local hiding = cover ~= nil and v.tick < L and v.tick >= L - 2 - travel
    -- (NOT a tick early: probecov3 sent the way back on L - 1 from the cover
    -- tile and the launch read every seat one tile off it, 6428,93 -- the
    -- walk sent on L - 1 lands within L - 1's turn.  The launch tick is the
    -- way out.)
    -- the reading each launch was decided on: the tile the launch reads is the
    -- one I end L - 1 on, so this is taken then
    if v.tick == L - 1 and vz.cover_note ~= L then
        vz.cover_note = L
        st.notes = st.notes or {}
        local bolts = {}
        for slot, n in pairs(vz.pillar_bolts or {}) do bolts[#bolts + 1] = tostring(slot) .. "x" .. n end
        table.sort(bolts)
        local hps = {}
        for slot, hp in pairs(vz.pillar_hp or {}) do hps[#hps + 1] = tostring(slot) .. "=" .. hp end
        table.sort(hps)
        if #st.notes < 24 then
            st.notes[#st.notes + 1] = "L" .. L .. (cover ~= nil
                and (":" .. cover.x .. "," .. cover.z .. "s" .. tostring(cover.pillar.slot) .. (cover.may_fall and "F" or ""))
                or ":none") .. "@" .. me.x .. "," .. me.z .. (vz.held == "dawnbringer" and "D" or "")
                .. "[" .. table.concat(bolts, ",") .. "|" .. table.concat(hps, ",") .. "]"
        end
    end
    if hiding and vz.hide_log ~= L then
        vz.hide_log = L
        vz.hides = vz.hides or {}
        vz.hides[#vz.hides + 1] = { L = L, from = v.tick, travel = travel }
    end
    local busy = QD.raid._verzik_dawn(st, v, intent, hiding, on_cover, events)
    -- the most one hit can take in P1: a bolt that finds me out of cover (68
    -- prayed, V p1_max_hit) or a pillar's fall (70, V pillar_collapse_max);
    -- neither is tick-eatable (W:877), so a raider never stands below it
    local threat = function(h)
        return math.max(N.bolt, 70)
    end
    if vz.dying then
        if not ok(me.x, me.z) then go(me.x, me.z) end
        return threat
    end
    if hiding then
        if not on_cover then go(cover.x, cover.z) end
        intent.attack = false
        intent.spec = nil
        return threat
    end
    if busy then
        return threat
    end
    intent.attack = true
    return threat
end

-- raid seam34v: a trio's P2 side, two out of her body (V p2_scan_rule: a
-- raider adjacent or inside on T-1 is slammed or stomped).  W:904 "Trios: one
-- goes south, one east, and one west", which "maintains space for urnbombs,
-- lightning and the AoE heal" (W:904): p1 west, p2 east, p3 south.  `alt` 2
-- is the tile beside it on the same side, for a tile something falls on.
function QD.raid._verzik_p2_home(st, bx, bz, n, alt)
    local mid = math.floor(n / 2)
    local k = (alt == 2) and -1 or 0
    if st.role == 2 then return bx + n + 1, bz + mid + k end
    if st.role == 3 then return bx + mid + k, bz - 2 end
    return bx - 2, bz + mid + k
end

-- ==========================================================================
-- raid seam45 play_tob_verzik_melee_follows_blert: P2 AND P3 MELEE FOR THE
-- NORMAL TRIO, as the recorded trios play it.  Blert verzik_normal_3.json
-- (20 death-free Normal trio rooms): every role is melee; the scythe in P2
-- in 16 of 20 rooms per role, about 31 swings on her and 5-6 on the adds; in
-- P3 21-24 swings on her and none on an add.  Both phases rest on ET 1.1:
-- she scans on T from where the raiders stood at the END of T-1, so a raider
-- beside her steps out on the tick before her attack and back in on its tick.
--   P2 (V verzik.p2_scan_rule, C): a raider adjacent or inside on T-1 draws
--   the body slam (up to 45, knockback 3, a stun: tob_verzik.rs2
--   ~tob_verzik_body_slam), so "it's best to stay close to Verzik for every
--   tick except for the dangerous tick" (Plank2g, quoted in ET 1.3).
--   P3 (V verzik.p3_melee_predicate, C): her melee (up to 63 on everyone
--   beside her, unprayable, V p3_melee_max) is on the table only when her
--   tank stood adjacent on T-1 and never on her first P3 attack; W:953 "walk
--   under or away from Verzik one or two ticks before she attacks".  A
--   raider cannot tell which of three she is tanking, so every raider
--   beside her steps out.
-- The plan's tick is not the server's to the tick: an input sent on the
-- plan's tick t lands on the server's t or t+1 (a decide that pressed waits
-- out its one-tick settle), and her seq read off the npc row is dated to the
-- server's tick or one before it (s45 e2/e3, the members' clock rows against
-- the leader's tick log: 8114 dated t176 for t177, t188 for t189, t181 for
-- t181).  So the step out goes on EITHER of the plan's ticks T-2 and T-1
-- (whichever finds the raider beside her), no press is sent on them, and the
-- press back in waits for T: the raider stands two out at the end of the
-- server's T-1 whichever way the tick falls.  Measured against the one-tick
-- shapes first: a step on the plan's T-2 alone stood the leader out at the
-- end of T-2 and its press of T-1 had him beside her at the end of T-1 (e2:
-- slams t181, t197); a step on T-1 alone was late on t185 and t237 (e3).
-- ==========================================================================

-- Whether the melee trio dodges its tornado along her edge in the enrage.
-- Off: the client's tornado rows stay on the spawn tile (s31 vz31d; s45 e8:
-- "r6431,91;6432,91;6432,90" while the server walked them round her), the
-- plan's simulated tornado is a guess, and a dodge on the guess ran s45 e17
-- sva's p3 round the west half of the room for 65 ticks with no swing and two
-- touches anyway.  The trio POWERS THROUGH (W:983 "Teams with sufficient
-- experience can simply power through into enrage; they can either keep their
-- health low so the tornado heals little"), from the side of her the
-- tornadoes rise furthest from (the east edge).
QD.RAID_PLAY_VERZIK_DODGE = false  -- (the simulated dodge; the seen one below stays on)

-- How many of the plan's ticks the step out may be SENT on, ending on T-2
-- (1: T-2 alone; 2: T-3 and T-2).  raid seam51 play_tob_verzik_whole: the
-- timing note above measured the two-tick step against an attack dated one
-- early.  Dated by the tick that first saw it (QD.raid._verzik_see `at`), the
-- plan's tick t is the server's t, and an input sent on it lands on the
-- server's t+1 (s49 head_party4 sva leader, P2 log "177 press -> 178 d1,
-- 178 step -> 179 d2"): the step sent on T-2 stands the raider out at the end
-- of T-1, the tile her scan on T reads (ET 1.1), and the press sent on T-1
-- walks it back in on T, after her turn.  One tick-end out of every four, not
-- two: the two-tick step left each raider ready and not swinging 57-60 ticks
-- of a 319-tick P2 (seam50 uptime.py, about 12 swings a raider).
QD.RAID_PLAY_VERZIK_OUT_LEAD = 1

-- The one-tile step out of her reach: a floor tile two from her footprint,
-- beside me, nothing falling on it (an urnbomb is aimed at the tile of T-1,
-- W:909 "dodged by simply avoiding the tile the urn was thrown at"); the
-- straight step first, then the tile clear of the other raiders (the zap
-- bounces between neighbours, W:911).  From under her (an add's press can
-- path a raider through her body: s45 e5 _play_verzik t181, the leader
-- pressed the Athanatos at 6432,87 from 6429,89 and stood on 6431,89 inside
-- her until her stomp at t185) the nearest such tile is two away, a run's
-- one tick.  nil when there is none.
function QD.raid._verzik_step_out(st, v, ok, mates)
    local me, b = v.me, v.boss
    local best, bx, bz = nil, nil, nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = me.x + dx, me.z + dz
            if (dx ~= 0 or dz ~= 0) and ok(x, z) and not v.shadows[x * 100000 + z] and QD.raid._verzik_dist(x, z, b) >= 2 then
                local sc = math.max(math.abs(dx), math.abs(dz)) * 100 + (math.abs(dx) + math.abs(dz)) * 10
                -- owner_verzik 2026-10-07: she follows her tank now
                -- (tob_verzik.rs2 ~tob_verzik_p3_follow), a tile for each
                -- step out, so the step goes toward the room's middle: "Try
                -- to keep Verzik at the centre (phase start area) when
                -- possible. Moving away from Verzik will slightly displace
                -- her" (W:953)
                local O, F = st.origin, st.plan.floor
                local mx, mz = O.x + (F[1] + F[3]) / 2, O.z + (F[2] + F[4]) / 2
                sc = sc + math.max(math.abs(x - mx), math.abs(z - mz))
                for _, m in ipairs(mates or {}) do
                    if math.max(math.abs(m.x - x), math.abs(m.z - z)) <= 1 then sc = sc + 5 end
                end
                if best == nil or sc < best then best, bx, bz = sc, x, z end
            end
        end
    end
    return bx, bz
end

-- raid seam49 play_tob_verzik_round2: whether I am her P3 TANK.  Her melee
-- is checked against the tank alone, from where the tank stood at the end of
-- the previous tick (tob_verzik.rs2 ~tob_verzik_tank_in_melee; V
-- verzik.p3_melee_predicate), so only the tank has a dangerous tick: W:953
-- "the PRIMARY TANK should either walk under or away from Verzik one or two
-- ticks before she attacks".  She takes her tank on her first P3 tick and
-- keeps it for the phase while it comes back into reach at least every 17
-- ticks (~tob_verzik_pick_tank, ^tob_verzik_p3_aggro_timeout).  In the game
-- a raider sees whom she faces; this room does not turn her to the tank
-- (CONTENT_BUGS seam49 "P3 does not face its tank"), and its pick takes the
-- first raider in player order (huntall iterates by pid,
-- torirs_server_scripts.c SS_OP_HUNTALL), so the plan reads the tank as the
-- living raider in the room with the lowest pid.  Not known: I am the tank.
function QD.raid._verzik_is_tank(st, v)
    if st.party <= 1 or st.my_pid == nil then return true end
    -- owner_verzik 2026-10-07: WHOM SHE FACES.  She picks her tank at random
    -- and keeps it the phase (tob_verzik.rs2 ~tob_verzik_pick_tank, after
    -- W:946 and Blert's 27 trio rooms), and faces it while it is in her reach
    -- (~tob_verzik_p3_follow): her row's facing names it (a player as 32768 +
    -- its slot, entity_facets.h WORLD_FACING_PLAYER_BASE).  The last one seen
    -- is kept while she walks.
    local f = v.boss and v.boss.facing
    if f ~= nil and f >= 32768 then st.vz.tank_pid = f - 32768 end
    if st.vz.tank_pid ~= nil then return st.vz.tank_pid == st.my_pid end
    local pr, prow = api_drive.players()
    if pr ~= "ok" then return true end
    local b, low = v.boss, nil
    for _, r in ipairs(prow) do
        if r.pid ~= nil and QD.raid._verzik_on_floor(st, r) and (low == nil or r.pid < low) then low = r.pid end
    end
    return low == nil or low == st.my_pid
end

-- owner_verzik: a raider on the room's floor.  A raider who died stands in
-- the spectator cage, off the floor (tob_spectate.rs2: Verzik's cage tiles
-- (21..41, 37), the floor is 22..41 x 15..34) and is nobody's mate: no pool,
-- no ball, no tank.
--
-- The cage row is the test, not the floor box: `floor` is the box the plan
-- keeps itself inside (22..41), and raiders stand outside it -- probecov6's
-- target was on 6419,94 (local 19) when the ball came, two clients left it
-- out of their ring, and the chain split three ways.  Raiders were seen from
-- local x 19 to 43 and z 15 to 36; the cage is z 37 (tob_spectate.rs2:108).
QD.RAID_PLAY_VERZIK_ROOM = { x0 = 16, x1 = 46, z0 = 12, cage_z = 37 }
function QD.raid._verzik_on_floor(st, r)
    local O, R = st.origin, QD.RAID_PLAY_VERZIK_ROOM
    return r.x >= O.x + R.x0 and r.x <= O.x + R.x1 and r.z >= O.z + R.z0 and r.z < O.z + R.cage_z
end

-- The other raiders' tiles (api_drive.players), for the step's tie-break.
function QD.raid._verzik_mates(st)
    local mates = {}
    if st.party > 1 then
        local pr, prow = api_drive.players()
        if pr == "ok" then
            for _, r in ipairs(prow) do
                if not r.me and QD.raid._verzik_on_floor(st, r) then mates[#mates + 1] = r end
            end
        end
    end
    return mates
end

-- Her next P2 attack, from what she was seen to do: every 4 (V
-- verzik.p2_cadence, B); after the seventh attack since a summon the next
-- slot is the summon, 8 later (tob_verzik.rs2 ^tob_verzik_p2_reds_after_last;
-- s45 read off svaplayverzi: summons t352, t396, t440 each 8 after the
-- seventh), and the first attack after a summon 12 later (V
-- verzik.reds_first_attack_after, B; t308 -> t320).  Returns the tick and
-- whether that slot is a summon (no scan: the summon returns before
-- ~tob_verzik_body_slam).
function QD.raid._verzik_p2_clock(st, v)
    local P, vz = st.plan, st.vz
    local a = v.attack
    if a ~= nil then
        local at = a.at or a.tick
        -- raid seam51: her cadence is exact (V verzik.p2_cadence 4, B), so a
        -- cast seen ONE tick after the slot the clock named is that slot, seen
        -- late (the decide that pressed waits out a one-tick settle and sees
        -- her seq on the next tick: e1 sva t233 -> t237, all three raiders
        -- dated it 234, stepped out on 236 and were slammed)
        -- (never two in a row: a clock that really ran one late would lock
        -- in one early, so the second is taken as seen)
        local snap = vz.m2_N ~= nil and not vz.m2_summon_next and at == vz.m2_N + 1 and not vz.m2_snapped
        if snap then
            at = vz.m2_N
            vz.m2_snaps = (vz.m2_snaps or 0) + 1
        end
        vz.m2_snapped = snap
        a.at = at
        if a.seq == P.p2_cast or a.seq == P.p2_slam then
            vz.m2_N, vz.m2_summon_next = at + P.p2_cadence, false
            if vz.summon ~= nil and at > vz.summon and (vz.p2_count or 0) >= P.p2_attacks_between then
                vz.m2_N, vz.m2_summon_next = at + 2 * P.p2_cadence, true
            end
        elseif a.seq == P.p2_reds then
            vz.m2_N, vz.m2_summon_next = at + P.p2_reds_first, false
        end
    end
    return vz.m2_N, vz.m2_summon_next
end

-- P2, Normal trio, melee.  Each raider holds its own side (W:904 "one goes
-- south, one east, and one west": QD.raid._verzik_p2_home), beside her and
-- swinging on every tick but the one before her attack.  The adds: the
-- Athanatos first (W:927, it heals her every few ticks), then the Matomenos
-- (W:931; Blert 5-6 swings a role on adds); a nylocas is never swung at (it
-- blasts everyone within 3 when it dies, tob_verzik.rs2 ~tob_verzik_crab_blast)
-- but run from (W:925).  No swing on her across a summon: "DO NOT attack her
-- while she summons them or immediately after, as any damage dealt will
-- instead heal her" (Entry_Mode.wikitext:231; V verzik.reds_absorb_window 5).
-- Returns the threat function.
function QD.raid._verzik_p2_melee(st, v, intent, ok, go, nearest)
    local P, N, vz, b, me = st.plan, st.numbers, st.vz, v.boss, v.me
    vz.m2 = vz.m2 or { outs = 0, late = 0, waits = 0, add_presses = 0, kites = 0, log = {} }
    local M = vz.m2
    local d_boss = QD.raid._verzik_dist(me.x, me.z, b)
    local nxt, summon_next = QD.raid._verzik_p2_clock(st, v)
    -- the first ticks of the phase, as the plan read them (the ledger's
    -- play.melee_clock row): tick, her next attack, my distance, her seq seen
    if #M.log < 24 then
        M.log[#M.log + 1] = v.tick .. "N" .. tostring(nxt) .. "d" .. d_boss .. (v.attack and ("a" .. v.attack.seq .. "@" .. (v.attack.at or v.attack.tick)) or "")
    end
    -- not known: no attack seen yet this phase, or her slot passed unseen
    local unknown = nxt == nil or v.tick > nxt + 1
    -- (late: beside her at the end of T-1, the tile her scan reads)
    if v.tick == (nxt or -10) - 1 and d_boss <= 1 then
        M.late = M.late + 1
        M.late_ticks = M.late_ticks or {}
        if #M.late_ticks < 16 then M.late_ticks[#M.late_ticks + 1] = v.tick end
    end
    local _, cd = nearest(v.crabs)
    local threat = function(h)
        local t = N.zap + N.bomb
        if cd <= 3 then t = t + N.crab end
        if vz.reds_tick ~= nil then t = t + N.blood end
        return t
    end
    -- a nylocas within 4: away from it, onto the floor two out of her (its
    -- blast reaches 3, tob.constant ^tob_verzik_p2_nylo_blast_range, and it
    -- dies on its own 25 ticks after it rises, ^tob_verzik_p2_nylo_lifetime;
    -- s45 e16 svb/svd: p2 went for the Athanatos at 6432,87 among the
    -- nylocas and took 63 and 44-53 in seven ticks, dead at t212-213)
    local ok2 = function(x, z) return ok(x, z) and QD.raid._verzik_dist(x, z, b) >= 2 end
    if #v.crabs > 0 and QD.raid._verzik_avoid({ st = st, v = v, intent = intent, go = go, ok = ok2, reach = 1,
            mates = QD.raid._verzik_mates(st) }) ~= "CLEAR" then
        if intent.walk ~= nil then
            M.kites = M.kites + 1
            vz.target_slot = nil
            return threat
        end
    end
    -- under her (a press pathed me through her body): out at once, whatever
    -- the tick (V verzik.p2_scan_rule: "inside" draws the stomp, up to 82)
    if d_boss == 0 then
        local sx, sz = QD.raid._verzik_step_out(st, v, ok, QD.raid._verzik_mates(st))
        if sx ~= nil then
            intent.walk = { x = sx, z = sz }
            vz.steps = vz.steps + 1
            M.outs = M.outs + 1
            vz.target_slot = nil
            return threat
        end
    end
    -- (raid seam51: the "engaged from two out and not moving: press again"
    -- rule is gone: every raider SEES its own swings now (raid seam48), and
    -- the library's _play_attack presses again when none came for speed + 1)
    -- the step out, sent on the plan's T-2 (it lands on T-1; see
    -- QD.RAID_PLAY_VERZIK_OUT_LEAD), and whenever her clock is not known;
    -- the press back in is the ordinary one below, from T-1 on
    if unknown or (v.tick >= nxt - 1 - QD.RAID_PLAY_VERZIK_OUT_LEAD and v.tick <= nxt - 2) then
        if d_boss <= 1 then
            local sx, sz = QD.raid._verzik_step_out(st, v, ok, QD.raid._verzik_mates(st))
            if sx ~= nil then
                intent.walk = { x = sx, z = sz }
                vz.steps = vz.steps + 1
                M.outs = M.outs + 1
                vz.target_slot = nil
            end
        elseif nxt == nil then
            -- before her first P2 attack: my side's tile two out (W:904)
            local hx, hz = QD.raid._verzik_p2_home(st, b.x, b.z, b.size or 3, 1)
            if v.shadows[hx * 100000 + hz] then hx, hz = QD.raid._verzik_p2_home(st, b.x, b.z, b.size or 3, 2) end
            go(hx, hz)
        elseif st.engaged and vz.target_slot == nil then
            -- raid seam51: engaged on her from two out, the server would path
            -- the swing in on T-1, beside her for the scan: a click on my own
            -- tile clears it (as P3's hold)
            intent.walk = { x = me.x, z = me.z }
        end
        return threat
    end
    local purple = nearest(v.purples)
    local red = nearest(v.reds)
    -- THE ATHANATOS IS ONE SEAT'S JOB (owner 2026-10-07: "In p2, one of the
    -- party members needs to hit the athanatos nylocas with a poison weapon so
    -- it pops").  One poisonous hit bursts it ("If it is hit with a weapon
    -- capable of afflicting poison or venom (or when the serpentine helm is
    -- equipped), the Nylocas Athanatos will instead burst", W Nylocas
    -- Athanatos :54; tob_verzik.rs2 ~tob_verzik_athanatos_poisoned), and until
    -- then it heals her 9-10 every 5 ticks.  watchvz: it spawned at t225 and
    -- nobody pressed it until t247 -- it was skipped whenever ANY crab stood
    -- within 4 of it, which in P2 is nearly always -- so it healed her five
    -- times.  Now the seat nearest it when it is first seen (ties: orb order)
    -- takes it at once, everyone computing the same seat from the same rows,
    -- and the others stay on her.  Only a popping crab's band over it holds
    -- the poisoner back.
    if purple ~= nil then
        vz.poisoner = vz.poisoner or {}
        local slot = purple.row.slot
        if vz.poisoner[slot] == nil then
            local function gap(x, z) return math.max(math.abs(x - purple.row.x), math.abs(z - purple.row.z)) end
            local pick, pd, pseat = st.my_pid, gap(me.x, me.z), st.role
            for _, mt in ipairs(QD.raid._verzik_mates(st)) do
                local seat = QD.raid._verzik_seat_of(mt)
                local d = gap(mt.x, mt.z)
                if seat ~= nil and (d < pd or (d == pd and seat < pseat)) then pick, pd, pseat = mt.pid, d, seat end
            end
            vz.poisoner[slot] = pick
        end
        if vz.poisoner[slot] ~= st.my_pid then
            purple = nil
        else
            for _, k in ipairs(QD.raid._verzik_nylo_read(st, v)) do
                if k.state == "DYING" and QD.raid._verzik_nylo_gap(purple.row.x, purple.row.z, k.row) <= QD.RAID_PLAY_VERZIK_NYLO.reach + 1 then
                    purple = nil
                    break
                end
            end
        end
    end
    -- the Matomenos only while her summon animation plays: "players should
    -- focus on the Matomenos until this animation ends" (W:928; 8117 is 10
    -- ticks, V verzik.av.reds_summon.seq).  After it, her: a red left alive
    -- heals her its remaining health at the next summon (tob_verzik.rs2
    -- ~tob_verzik_absorb_reds), up to 150 (V verzik.reds_hp_3), and Blert's
    -- trios swing at an add only 5-6 times a role in all of P2.  Measured
    -- both ways on one name (s45 svaplayverzi): every red chased to its death
    -- (e8) made P2 517 ticks, the window (e7) 393.
    -- raid seam52: which summon this is (P.p2_reds_kill_window's note): her
    -- bar read on the first tick after it, against the bar at the one before
    -- (P2's OWN percent: the client's bar is over P2 and P3 together, one
    -- pool of 2 x ^tob_verzik_p23_hp_3 2625 -- e1 red policy rows read the
    -- first summon, P2's 35 %, at 63-66 and her last at 50 -- so P2 runs the
    -- bar from 100 to 50)
    local pct = nil
    -- (owner_verzik 2026-10-07: the overhead bar is P2's OWN since content
    -- 33187928c9 -- npc_setheadbarreserve leaves P3's pool out -- so it is
    -- read as it stands)
    if b.health_ratio ~= nil and b.health_scale ~= nil and b.health_scale > 0 then pct = b.health_ratio * 100 / b.health_scale end
    if vz.summon ~= nil and vz.red_policy_for ~= vz.summon and pct ~= nil then
        vz.red_policy_for = vz.summon
        vz.red_last = vz.red_prev_pct ~= nil and (pct <= P.p2_reds_last_pct or pct <= (vz.red_prev_pct - pct) * P.p2_reds_last_frac)
        vz.red_prev_pct = pct
        M.red_policy = M.red_policy or {}
        if #M.red_policy < 6 then M.red_policy[#M.red_policy + 1] = vz.summon .. (vz.red_last and "L" or "K") .. math.floor(pct) end
    end
    local red_window = vz.red_last and P.p2_absorb or P.p2_reds_kill_window
    if red ~= nil and vz.summon ~= nil and v.tick > vz.summon + red_window then red = nil end
    -- one red each: the pair split between the raiders by role, in slot
    -- order, so both die (s45 e6 sva: all three on the nearest red, the other
    -- absorbed whole four times, 588 healed); raid seam52: the third raider
    -- on the red with more left (seam51: two on the first red overkilled it
    -- while the second kept about half and healed her ~100 a summon), kept
    -- while it lives so the pick does not flip each tick
    if red ~= nil and #v.reds > 1 then
        local sorted = {}
        for _, r in ipairs(v.reds) do sorted[#sorted + 1] = r end
        table.sort(sorted, function(a, c) return a.row.slot < c.row.slot end)
        if st.role <= #sorted then
            red = sorted[st.role]
        else
            local keep = nil
            for _, r in ipairs(sorted) do if r.row.slot == vz.target_slot then keep = r end end
            if keep ~= nil then
                red = keep
            else
                red = sorted[1]
                for _, r in ipairs(sorted) do
                    if (r.row.health_ratio or 0) > (red.row.health_ratio or 0) then red = r end
                end
            end
        end
    end
    local add = purple or red
    -- her: never into a summon slot, never inside the absorb window after one
    -- (raid seam51: from the plan's T-1 of the summon slot, not T: a press
    -- or a repeat sent on T-1 is rolled on T, the summon's own tick, and
    -- heals her -- e1 svb tob_prepare_player_hit 37+18 on t303, 9 on t346,
    -- 23+10 on t390, each a summon tick.  The FIRST summon is not counted:
    -- "At 35% she stops attacking, summons two Matomenos" (tob_verzik.rs2
    -- ~tob_verzik_p2 notes, the reds at 35 % of P2), so once her bar reads
    -- 36 % or less with no summon yet, her next slot is held as one)
    -- (raid seam52: on P2's own percent, above; the bar's 36 never came in P2)
    local first_due = (vz.summons or 0) == 0 and pct ~= nil and pct <= 36
    local hold_her = ((summon_next or first_due) and nxt ~= nil and v.tick >= nxt - 1) or (vz.summon ~= nil and v.tick <= vz.summon + P.p2_absorb)
    if add ~= nil then
        local idle = v.tick - math.max(st.last_swing, st.engaged_tick) > st.weapon.speed + 2
        if vz.target_slot ~= add.row.slot or not st.engaged or idle then
            QD.raid._verzik_press_add(st, v, add)
            M.add_presses = M.add_presses + 1
        end
    elseif hold_her then
        M.waits = M.waits + 1
        if st.engaged then
            -- engaged, the scythe repeats on its own: one step clears it
            local sx, sz = me.x, me.z
            if d_boss <= 1 then sx, sz = QD.raid._verzik_step_out(st, v, ok, QD.raid._verzik_mates(st)) end
            if sx ~= nil then
                intent.walk = { x = sx, z = sz }
                vz.steps = vz.steps + 1
            end
        end
    else
        if vz.target_slot ~= nil then vz.target_slot = nil end
        intent.attack = true
    end
    return threat
end

-- Her next P3 attack, from what she was seen to do (s45, read off
-- svaplayverzi's P3, t481-679): an auto every 7, 5 enraged (V
-- verzik.p3_cadence, A; the enrage resets her clock to 5 from that tick,
-- tob_verzik.rs2 ~tob_verzik_check_enrage: t607 -> t612); the green ball's
-- next 12 later (t663 -> t675: "BALL consumes the attack slot and delays the
-- next auto", tob_verzik.rs2 ~tob_verzik_special); the crabs' 10 (t509 ->
-- t519); the yellows' 21 (t622 -> t643: the 14-tick charge, V
-- p3_yellow_pool_lifetime, then 7); the webs' 42 (t554 -> t596).  The webs
-- and the yellows STOP her clock until she re-acquires
-- ([proc,tob_verzik_p3_tick]), so those two are measured gaps, and a slot
-- that passes with nothing seen is not known.  Her first P3 attack is never
-- a melee (V verzik.p3_melee_predicate), so before it the clock is nil.
-- Returns (next tick, hold): hold is true on the ticks a raider beside her
-- steps out and does not press: the plan's T-2 and T-1 when the next tick is
-- SURE (an auto after an auto), and every tick from T-2 until her next attack is seen when it is
-- a measured gap (after a special or the enrage), or when a slot passed with
-- nothing seen.
function QD.raid._verzik_p3_clock(st, v, ball)
    local P, vz = st.plan, st.vz
    local a = v.attack
    local cad = vz.enraged and P.p3_enraged_cadence or P.p3_cadence
    if a ~= nil then
        local s, at = a.seq, a.at or a.tick
        -- raid seam51: an auto seen one tick after the sure slot is that
        -- slot, seen late (as P2's clock)
        local snap = vz.m3_sure and vz.m3_N ~= nil and at == vz.m3_N + 1 and not vz.m3_snapped
        if snap then
            at = vz.m3_N
            vz.m3_snaps = (vz.m3_snaps or 0) + 1
        end
        vz.m3_snapped = snap
        a.at = at
        if s == P.p3_ranged or s == P.p3_magic or s == P.p3_melee then
            vz.m3_A, vz.m3_N, vz.m3_sure = at, at + cad, true
        elseif s == P.p3_crabs then
            vz.m3_A, vz.m3_N, vz.m3_sure = at, at + P.p3_gap_crabs, false
        elseif s == P.p3_webs then
            vz.m3_A, vz.m3_N, vz.m3_sure = at, at + P.p3_gap_webs, false
        elseif s == P.p3_yellows then
            vz.m3_A, vz.m3_N, vz.m3_sure = at, at + P.p3_gap_yellows, false
        end
    end
    -- the green ball rides an auto: once it is seen in the air, her next is 12 on
    if ball and vz.m3_sure and vz.m3_A ~= nil and vz.m3_ball ~= vz.m3_A then
        vz.m3_ball = vz.m3_A
        vz.m3_N, vz.m3_sure = vz.m3_A + P.p3_gap_ball, false
    end
    -- the enrage restarts her clock at 5 from its tick; seen a tick late at
    -- most, so one early, and held until her attack shows
    if vz.enraged and not vz.m3_enrage_seen then
        vz.m3_enrage_seen = v.tick
        vz.m3_N, vz.m3_sure = v.tick + P.p3_enraged_cadence - 1, false
    end
    local N = vz.m3_N
    if N == nil then return nil, false end
    -- (raid seam51: on a sure slot the walk goes on T-2 alone -- an input
    -- sent on the plan's tick t lands on the server's t+1, so the raider
    -- stands out at the end of T-1 -- and the press back in goes on T-1 and
    -- lands on T, after her scan: one tick-end out of reach, not two)
    local hold = (v.tick > N + 1) or (v.tick >= N - 1 - QD.RAID_PLAY_VERZIK_OUT_LEAD and v.tick <= N - 2) or ((not vz.m3_sure) and v.tick >= N - 2)
    return N, hold
end

-- ==========================================================================
-- raid seam53 play_state_machines (2026-10-07): THE VERZIK ROLES AS DECLARED
-- MACHINES, on raid_sm.lua.
--
-- The owner: "The script state machines should be easily writable and easily
-- written to implement the states that it needs.  You need to fix that for
-- verzik."  Before this the plan held four machines in four hand-rolled
-- shapes -- { state = "opening", next = "crabs", seen = {} } and a chain of
-- string comparisons each -- and the phases were an if/elseif on her form.
-- Adding a state meant finding every chain that named its neighbours.  Below,
-- each role is DECLARED: its states by name, each state's event handlers by
-- event name, so one screen says what the states are and how they connect.
-- A handler is function(ctx, ev) -> intent, go; no `go` means stay.  A
-- transition to a state nobody declared aborts and names the typo.
-- ==========================================================================

-- THE EVENTS, derived ONCE a tick from the tick view, here and nowhere else,
-- so every machine on the raider sees the same set and no machine can grow a
-- private reading of the tick.  Run after QD.raid._verzik_see, whose v.crabs,
-- v.pools, v.webs and v.tornadoes it reads.  Keeping this separate from the
-- states is the point: the states say what to do, this says what happened.
--
-- A state that does not name an event ignores it; what a state ignores is
-- visible in the declaration by what is absent.
function QD.raid._verzik_events(st, v)
    assert(st, "_verzik_events: st")
    assert(v, "_verzik_events: v")
    local P, vz = st.plan, st.vz
    assert(vz, "_verzik_events: st.vz (call after the plan's own init)")
    local out = {}
    local function raise(name, e)
        e = e or {}
        e.name = name
        out[#out + 1] = e
    end
    -- HER FORM: the phase the library reads off her npc row, as an edge, and
    -- BEFORE `tick` -- the tick her form changes must be decided by the state
    -- she has just entered, which is what the old chain did by reading
    -- `phase` before it branched.
    local phase = v.phase or vz.phase
    if phase ~= nil and vz.sm_phase ~= phase then
        raise("form_change", { to = phase, from = vz.sm_phase })
        vz.sm_phase = phase
    end
    -- every tick, so a state can act without an event of its own
    raise("tick", { tick = v.tick, hp = v.hp })
    -- HER ATTACK SEQ on the tick it shows, and what that seq means: one of
    -- the three P3 specials starting, one of her autos (with the protection
    -- it calls for), her P1 wind-up, a reds summon, or her form's death
    local a = v.attack
    if a ~= nil then
        local at = a.at or a.tick
        raise("boss_seq", { seq = a.seq, at = at })
        if a.seq == P.p3_crabs then
            raise("special_crabs", { at = at })
        elseif a.seq == P.p3_webs then
            raise("special_webs", { at = at })
        elseif a.seq == P.p3_yellows then
            raise("special_yellows", { at = at })
        elseif a.seq == P.p3_ranged or a.seq == P.p3_magic or a.seq == P.p3_melee then
            local style = "melee"
            if a.seq == P.p3_ranged then style = "protectfrommissiles"
            elseif a.seq == P.p3_magic then style = "protectfrommagic" end
            raise("auto", { seq = a.seq, at = at, style = style })
        end
        if a.seq == P.p1_windup then raise("bolt_windup", { at = at }) end
        if a.seq == P.p2_reds then raise("reds_summon", { at = at }) end
        if a.seq == P.p1_death or a.seq == P.p2_death then raise("boss_death", { seq = a.seq, at = at }) end
    end
    -- THE GREEN BALL: in the air, with the tile it falls on and the ticks of
    -- flight left, or gone.  `ball_gone` is raised on every tick it is NOT in
    -- the air, because that is the condition the rotation's ball state leaves
    -- on (the old `elseif not ball and c.state == "ball"` arm), not an edge.
    local ball = nil
    for _, p in ipairs(v.proj or {}) do
        if p.spotanim_id == P.ball_proj then
            ball = { x = p.dst_x, z = p.dst_z, target = p.target,
                left = math.ceil((p.cycles_left or 0) / QD.RAID_PLAY_CYCLES_PER_TICK) }
        end
    end
    -- owner_verzik 2026-10-07: THE BALL LIVES PAST ITS PROJECTILE.  The share
    -- is not the flight, it is the BOUNCE CHAIN that follows it: the ball
    -- reaches its target, then hops to a raider within one tile
    -- (^tob_verzik_p3_ball_bounce_range 1), and so on until every raider has
    -- been visited, after which it dissipates harmlessly; a hop back to
    -- someone already visited hits BOTH.  The projectile is GONE for all of
    -- that, so a share that lives only while it flies cannot hold the trio
    -- together through the hops.
    --
    -- _vzslowp3 is the proof and it is not what either I or the coordinator
    -- first said: at the throw tick t260 all three raiders stood on ONE TILE,
    -- 6434,92, ring-running together. They split during the eight ticks of
    -- flight -- t261 p0 to 6435,94 while p1 and p2 went to 6432,94, three
    -- tiles apart -- so when the ball landed on p1 at t267 for 0 and hopped to
    -- p2 at t268, p0 was not adjacent, the chain died on p2 and p2 took the
    -- 74. There were no yellow pools in that room at all, so the pool
    -- priority had nothing to do with it.
    --
    -- So the reading holds for CHAIN_TICKS after the projectile goes, on the
    -- last tile it was homing to (a homing projectile's dst is its target's
    -- live tile, QD.world.projectiles, so that tile IS the carrier).
    if ball ~= nil then
        vz.ball_last, vz.ball_last_tick = ball, v.tick
    elseif vz.ball_last ~= nil and v.tick <= (vz.ball_last_tick or 0) + QD.RAID_PLAY_VERZIK_BALL_CHAIN_TICKS then
        ball = { x = vz.ball_last.x, z = vz.ball_last.z, target = vz.ball_last.target, left = 0, chain = true }
    end
    vz.sm_ball = ball
    if ball ~= nil then raise("ball_air", { x = ball.x, z = ball.z, left = ball.left }) else raise("ball_gone", {}) end
    -- THE YELLOWS CHARGING: her pools on the floor (V verzik.p3_yellow_pools),
    -- and the tick the charge began, which is what their life is measured from
    if #v.pools > 0 then
        raise("yellows_charging", { pools = #v.pools, first = vz.pool_first,
            left = (vz.pool_first or v.tick) + P.pool_life - v.tick })
    end
    -- A CRAB SPAWN: a nylocas seen this tick that was not in view last tick
    local crabs_now = {}
    for _, c in ipairs(v.crabs or {}) do crabs_now[c.row.slot] = true end
    for slot in pairs(crabs_now) do
        if vz.sm_crabs == nil or not vz.sm_crabs[slot] then raise("crab_spawn", { slot = slot }) end
    end
    vz.sm_crabs = crabs_now
    -- A TORNADO STEP: where each belief stands now and the tile it steps to
    -- next, which is one toward where I stood at the END of last tick
    -- (tob_verzik.rs2 ~tob_verzik_tornado_tick: it walks on my previous tile,
    -- so the lag is the mechanic and not an approximation)
    local prev = vz.prev_me or v.me
    for slot, e in pairs(vz.tor or {}) do
        local nx, nz = e.x, e.z
        if prev.x > nx then nx = nx + 1 elseif prev.x < nx then nx = nx - 1 end
        if prev.z > nz then nz = nz + 1 elseif prev.z < nz then nz = nz - 1 end
        raise("tornado_step", { slot = slot, x = e.x, z = e.z, next_x = nx, next_z = nz,
            d = math.max(math.abs(e.x - v.me.x), math.abs(e.z - v.me.z)) })
    end
    -- HER HITPOINTS and MINE, as thresholds rather than numbers: her enrage
    -- (W:981, a fifth of her bar) and my own band's floor
    local b = v.boss
    if b ~= nil and b.health_ratio ~= nil and b.health_ratio > 0 and b.health_scale ~= nil and b.health_scale > 0 then
        if b.health_ratio * 5 <= b.health_scale then raise("boss_enraged", { ratio = b.health_ratio, scale = b.health_scale }) end
    end
    if v.hp <= P.enrage_hp_floor then raise("hp_low", { hp = v.hp, floor = P.enrage_hp_floor }) end
    -- THE SPECIAL ATTACK ORB, read once here rather than once per machine
    -- (the Dawnbringer and the claw dump each used to read it, to the same
    -- value).  A special is SEEN as the energy it spends, so the machines
    -- keep their own previous reading; the event carries only what it is now.
    local _, energy = QD.var.varp("varp300_sa_energy")
    raise("orb", { energy = tonumber(energy) or 0 })
    return out
end

-- owner_verzik 2026-10-07, ported to raid_sm 2026-10-07: HER P3 SPECIAL
-- ROTATION.  She attacks four times between specials and throws them in a
-- fixed order: nylocas, webs, yellows, the green ball, then round again
-- (W:953-957; the Strategies Entry section W:254 "the cycle will repeat once
-- completed"; tob_verzik.rs2 ~tob_verzik_special_at, tob.constant
-- ^tob_verzik_special_*).  The ball rides a regular ranged attack
-- (~tob_verzik_special, "THE BALL RIDES A REGULAR ATTACK"), which is why
-- `ball` subscribes to no `auto` and leaves on `ball_gone` instead.
--
-- `owed` (the old c.next) is the special the rotation still owes, which the
-- decide reads to hold the slow pace back and to heal up before the ball.
-- Each state that owes one sets it on ENTER, beside the landmark row the
-- harness reads (QD.raid.verzik_p3_rows, the old c.seen).
QD.raid.sm_declare("verzik_rotation", {
    start = "opening",
    states = {
        -- before her first special: anything she throws names the state
        opening = { on = {
            special_crabs   = function(c) return nil, "crabs" end,
            special_webs    = function(c) return nil, "webs" end,
            special_yellows = function(c) return nil, "yellows" end,
            auto            = function(c) return nil, "autos" end,
            ball_air        = function(c) return nil, "ball" end,
        } },
        -- the three thrown specials: each owes the next one, and each leaves
        -- on her next auto, on the ball, or on another special out of order
        crabs = {
            enter = function(c) QD.raid._verzik_rotation_owe(c, "crabs", "webs") end,
            on = {
                special_webs    = function(c) return nil, "webs" end,
                special_yellows = function(c) return nil, "yellows" end,
                special_crabs   = function(c) return nil, "crabs" end,
                auto            = function(c) return nil, "autos" end,
                ball_air        = function(c) return nil, "ball" end,
            },
        },
        webs = {
            enter = function(c) QD.raid._verzik_rotation_owe(c, "webs", "yellows") end,
            on = {
                special_crabs   = function(c) return nil, "crabs" end,
                special_yellows = function(c) return nil, "yellows" end,
                special_webs    = function(c) return nil, "webs" end,
                auto            = function(c) return nil, "autos" end,
                ball_air        = function(c) return nil, "ball" end,
            },
        },
        yellows = {
            enter = function(c) QD.raid._verzik_rotation_owe(c, "yellows", "ball") end,
            on = {
                special_crabs   = function(c) return nil, "crabs" end,
                special_webs    = function(c) return nil, "webs" end,
                special_yellows = function(c) return nil, "yellows" end,
                auto            = function(c) return nil, "autos" end,
                ball_air        = function(c) return nil, "ball" end,
            },
        },
        -- the ball in the air.  It rides an auto, so `auto` is NOT handled
        -- here: the state holds until the ball is out of the air, and the
        -- rotation then owes the crabs again.
        ball = {
            enter = function(c) QD.raid._verzik_rotation_owe(c, "ball", "crabs") end,
            on = {
                ball_gone       = function(c) return nil, "autos" end,
                special_crabs   = function(c) return nil, "crabs" end,
                special_webs    = function(c) return nil, "webs" end,
                special_yellows = function(c) return nil, "yellows" end,
            },
        },
        -- between specials.  No `owed` is set here and no landmark row is
        -- written: `autos` is where she is, not something she threw.
        autos = { on = {
            special_crabs   = function(c) return nil, "crabs" end,
            special_webs    = function(c) return nil, "webs" end,
            special_yellows = function(c) return nil, "yellows" end,
            ball_air        = function(c) return nil, "ball" end,
        } },
    },
})

-- the landmark a thrown special writes on entry: the state entered, what the
-- rotation now owes, and the row the harness prints, in the style it has
-- always been in ("webs@412/hp54").  Each state names both itself and its
-- debt, so the declaration above reads as the whole rotation.
function QD.raid._verzik_rotation_owe(c, state, owed)
    assert(c, "_verzik_rotation_owe: c")
    assert(type(state) == "string", "_verzik_rotation_owe: state must be a state name")
    assert(type(owed) == "string", "_verzik_rotation_owe: owed must be a state name")
    local r = c.cyc
    r.next = owed
    r.seen[#r.seen + 1] = state .. "@" .. c.v.tick .. "/hp" .. tostring(c.v.hp)
end

-- Runs the rotation for this tick and returns the record the decide reads:
-- `state` (the machine's), `next` (what it owes) and `seen` (the landmarks),
-- the same three fields the hand-rolled table carried, plus whatever the
-- decide hangs on it (share, sharing, split_until).
function QD.raid._verzik_p3_cycle(st, v, events)
    assert(st, "_verzik_p3_cycle: st")
    assert(v, "_verzik_p3_cycle: v")
    assert(type(events) == "table", "_verzik_p3_cycle: events")
    local vz = st.vz
    local r = vz.cyc
    if r == nil then
        r = { state = "opening", next = "crabs", seen = {} }
        vz.cyc = r
    end
    local c = { st = st, v = v, vz = vz, cyc = r }
    local m = QD.raid.sm_run(st, v, "verzik_rotation", c, events)
    r.state = m.state
    return r
end

-- owner_verzik 2026-10-07, ported to raid_sm 2026-10-07: THE FAST PACE'S
-- SPECIAL DUMP.  "At this stage [the enrage], players should dump all melee
-- special attacks to end the phase as fast as possible" (W:992); the fast
-- Blert trios do (build/blert/verzik, the 22 rooms that end before her ball:
-- CLAW_SPEC 1-2 a raider in P3 in most, CHALLY_SPEC in the rest).  The claws,
-- special while the orb holds its 50%, then the scythe back on.  Only where a
-- swing goes anyway (the decide calls it in its "attack her" branch, never on
-- a held tick, a pool or a shared ball).  The slow pace never calls it
-- ("drop the spec dumping", the owner).
--
-- STATES: absent (no claws carried -- this raider never dumps), carried,
-- wielded, done.  CARRIED and WIELDED share one handler on purpose: the old
-- code branched on the ORB and on the WEAPON IN HAND, never on its own state
-- field, which it used only to stop once (none/done).  Splitting the handler
-- would have invented a distinction the play does not make; the two names are
-- kept because the trace is worth having and because a future author adding a
-- second special weapon has somewhere to put it.
QD.RAID_PLAY_VERZIK_CLAW_COST = 500
QD.raid.sm_declare("verzik_specdump", {
    start = "absent",
    states = {
        -- no claws in the inventory: nothing to dump, ever
        absent = {},
        -- the claws carried with the main weapon in hand
        carried = { on = { orb = function(c, ev) return QD.raid._verzik_dump_orb(c, ev) end } },
        -- the claws in hand: a special every four ticks while the orb holds
        wielded = { on = { orb = function(c, ev) return QD.raid._verzik_dump_orb(c, ev) end } },
        -- spent, the main weapon back on
        done = {},
    },
})

-- the dump's one handler: the orb this tick against the claws' cost, and the
-- weapon in hand.  Returns (nil, next state) exactly where the old body
-- assigned d.state.
function QD.raid._verzik_dump_orb(c, ev)
    assert(c, "_verzik_dump_orb: c")
    assert(ev, "_verzik_dump_orb: ev")
    local st, v, vz, d, intent = c.st, c.v, c.vz, c.d, c.intent
    local energy = ev.energy
    if d.e_prev ~= nil and energy <= d.e_prev - 450 then d.specs = d.specs + 1 end
    d.e_prev = energy
    if energy >= QD.RAID_PLAY_VERZIK_CLAW_COST then
        if vz.held ~= "claws" then
            intent.gear = { "dragon_claws" }
            vz.held = "claws"
            st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS.claws
            QD.raid._verzik_engage(st, v, "rearmed")
            return nil, "wielded"
        end
        if v.tick >= (d.arm_tick or -1000) + 4 then
            d.arm_tick = v.tick
            d.arms = d.arms + 1
            intent.spec = true
            intent.attack = true
        end
        return
    end
    if vz.held == "claws" then
        local back = vz.p3_main or vz.main or "scythe"
        intent.gear = { QD.RAID_PLAY_VERZIK_WEAPONS[back].item }
        vz.held = back
        st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS[back]
        QD.raid._verzik_engage(st, v, "rearmed")
        return nil, "done"
    end
end

function QD.raid._verzik_spec_dump(st, v, intent, events)
    assert(st, "_verzik_spec_dump: st")
    assert(v, "_verzik_spec_dump: v")
    assert(intent, "_verzik_spec_dump: intent")
    assert(type(events) == "table", "_verzik_spec_dump: events")
    local vz = st.vz
    local d = vz.dump
    if d == nil then
        local cr, n = QD.inv.count("dragon_claws")
        d = { specs = 0, arms = 0, e_prev = nil }
        vz.dump = d
        -- which of the two starts this raider is in is an inventory question,
        -- answered once, here, where the knowledge is
        if cr == "ok" and n > 0 then
            QD.raid.sm_force(st, v, "verzik_specdump", "carried", { st = st, v = v, vz = vz, d = d, intent = intent }, "carries_claws")
        end
    end
    QD.raid.sm_run(st, v, "verzik_specdump", { st = st, v = v, vz = vz, d = d, intent = intent }, events)
    d.state = QD.raid.sm_at(st, "verzik_specdump").state
end


-- owner_verzik 2026-10-07, ported to raid_sm 2026-10-07: THE ENRAGE RING RUN,
-- copied from Blert's Normal trios (build/blert/verzik, 27 rooms, every
-- raider every tick of the enrage): beside her footprint 62% of ticks, under
-- it 14%, two or more out 24%; moving two tiles a tick on 67%; 13 swings a
-- team in a 25-35 tick enrage (~0.43 a tick); 0-2 tornado touches a room.
-- The rule that keeps the tornado off: it touches only its own raider, on the
-- raider's tile, at its turn (tob_verzik.rs2 ~tob_verzik_tornado_tick,
-- OSRS-Content fdf77aae1c), and it steps one tile toward where I stood at the
-- END of the tick before -- so every tile I end a tick on is two or more from
-- its next step.  Checked offline (scratchpad vz/sim3.py: three raiders,
-- their own tornadoes, her follow and her melee): a 30-tick median enrage
-- (max 36), 0 touches, 0.53 swings a tick; with one decide in ten lost, 34.5
-- (max 55), 0.3 touches, 0.47.
--
-- STATES, one a tick.  Every state handles every event and every event names
-- the state it goes to, so the five states and five events below ARE the
-- whole transition matrix -- which is why they share one handler table
-- rather than repeating it five times:
--   PROTECT  the yellows charge: my pool, reached through safe tiles
--            (`pool_walk`); the pool itself once I am standing on it
--            (`pool_stand`, which presses nothing -- a press paths off the pool)
--   SHARE    the green ball in the air: within its last four ticks of flight
--            the trio takes tiles beside the target, each safe from its own
--            tornado's next step; the target holds on a safe tile
--   SWING    my weapon ready, a tile in my reach under me, my tornado's next
--            step two or more away, and not on her melee scan (the two ticks
--            before her attack, beside her): stand and swing
--   RING     otherwise the ring tile two-step: on or beside her, in reach when
--            the swing is due, away from my tornado, off the walls, apart
--   EAT      declared, and nothing enters it today.  The bite is taken by the
--            library's supplies path and held off by the decide's own tornado
--            rule in the postamble (no bite or sip with a tornado within four
--            unless the hitpoints are below what one more touch and her auto
--            take).  It is declared because the enrage's eat rule belongs
--            here rather than two hundred lines away, and this is where to
--            put it; it behaves as RING until something transitions into it.
--
-- THE STATE HAS NO MEMORY, and that is deliberate.  The owner, asked
-- directly: these five names are a per-tick priority classification,
-- recomputed from scratch every tick.  The real memory of the enrage lives
-- elsewhere -- vz.tor (the tornado beliefs and the score that identifies
-- mine), vz.my_pool, cyc.share, vz.tank_pid -- and this machine reads it
-- without owning it.  So the classification is derived in ONE place
-- (QD.raid._verzik_ring_events, the old priority chain verbatim) and the
-- states act on it; what the machine adds over the old chain is the named
-- transition matrix, the trace, and somewhere for EAT to go.
QD.RAID_PLAY_VERZIK_ENRAGE_ON = {
    ball_timed = function(c, ev)
        QD.raid._verzik_ball_run({ st = c.st, v = c.v, intent = c.intent, tor = c.st.vz.tor, reach = c.reach,
            ok = function(x, z) return c.floor(x, z) end })
        return nil, ev.go
    end,
    pool_timed = function(c, ev)
        QD.raid._verzik_pool_run({ st = c.st, v = c.v, intent = c.intent, pool = c.pool, tor = c.st.vz.tor,
            ok = function(x, z) return c.floor(x, z) end })
        return nil, ev.go
    end,
    pool_stand = function(c, ev) return QD.raid._verzik_enrage_stand(c, ev) end,
    pool_walk  = function(c, ev) return QD.raid._verzik_enrage_step(c, ev) end,
    ball_share = function(c, ev) return QD.raid._verzik_enrage_step(c, ev) end,
    eat_now    = function(c, ev) return QD.raid._verzik_enrage_eat(c, ev) end,
    swing_now  = function(c, ev) return QD.raid._verzik_enrage_swing(c, ev) end,
    step       = function(c, ev) return QD.raid._verzik_enrage_step(c, ev) end,
}
QD.raid.sm_declare("verzik_enrage", {
    start = "RING",
    states = {
        RING    = { note = "the two-step round her, away from my tornado", on = QD.RAID_PLAY_VERZIK_ENRAGE_ON },
        SWING   = { note = "stand still and press her", on = QD.RAID_PLAY_VERZIK_ENRAGE_ON },
        SHARE   = { note = "beside the ball's target, off my tornado's next step", on = QD.RAID_PLAY_VERZIK_ENRAGE_ON },
        PROTECT = { note = "my yellow pool, walked to and then stood on", on = QD.RAID_PLAY_VERZIK_ENRAGE_ON },
        EAT     = { note = "stand still for the bite, my tornado five or more away", on = QD.RAID_PLAY_VERZIK_ENRAGE_ON },
    },
})

-- THE ENRAGE MACHINE'S OWN DERIVATION, in one place and separate from the
-- states: the tick's situation as ONE event, in the priority the old chain
-- read it -- my pool first (stood on, else walked to), then the ball, then a
-- swing that is free to go, then the ring.  It also leaves the walk's target
-- and its pull on the context, because the target is what the situation IS.
function QD.raid._verzik_ring_events(st, v, c)
    assert(st, "_verzik_ring_events: st")
    assert(v, "_verzik_ring_events: v")
    assert(c, "_verzik_ring_events: c")
    local P, vz, me = st.plan, st.vz, v.me
    c.target, c.pull = nil, 0
    -- the pool, timed: the same planner the chain uses (verzik_pool)
    if c.pool ~= nil then
        return { { name = "pool_timed", go = "PROTECT" } }
    end
    -- the green ball: the same machine the chain uses (verzik_ball)
    if c.ball ~= nil or (vz.bm ~= nil and not vz.bm.done) then
        return { { name = "ball_timed", go = "SHARE" } }
    end
    if v.hp <= P.enrage_hp_floor and c.clear(me.x, me.z) >= 5 then
        return { { name = "eat_now", go = "EAT" } }
    end
    if c.ready and c.db >= 1 and c.db <= c.reach and c.clear(me.x, me.z) >= 2 and not (c.hold and c.db == 1) then
        return { { name = "swing_now", go = "SWING" } }
    end
    return { { name = "step", go = "RING" } }
end

-- PROTECT on its pool: stand there (no press: it paths off the pool)
function QD.raid._verzik_enrage_stand(c, ev)
    assert(c, "_verzik_enrage_stand: c")
    assert(ev, "_verzik_enrage_stand: ev")
    c.intent.walk, c.intent.attack = nil, false
    return nil, ev.go
end

-- EAT: stand still for the bite.  No walk and no press, so the together
-- block's confirmation is not racing a step on the tick the food goes down --
-- a block holds the next decide, and a decide missed is a tile the tornado
-- gains, which is why the bite waits for five tiles of room.
function QD.raid._verzik_enrage_eat(c, ev)
    assert(c, "_verzik_enrage_eat: c")
    assert(ev, "_verzik_enrage_eat: ev")
    c.intent.walk, c.intent.attack = nil, false
    return nil, ev.go
end

-- SWING: the tile under me is in reach and clear, so stand and press
function QD.raid._verzik_enrage_swing(c, ev)
    assert(c, "_verzik_enrage_swing: c")
    assert(ev, "_verzik_enrage_swing: ev")
    c.intent.walk, c.intent.attack = nil, true
    return nil, ev.go
end

-- THE TWO-STEP: the best tile within two, scored.  Serves RING, SHARE and
-- PROTECT-walking alike -- the difference between them is the target and the
-- pull the derivation put on the context, not the walk.
function QD.raid._verzik_enrage_step(c, ev)
    assert(c, "_verzik_enrage_step: c")
    assert(ev, "_verzik_enrage_step: ev")
    local st, v, vz, intent = c.st, c.v, c.st.vz, c.intent
    local me, b, reach = v.me, v.boss, c.reach
    local O, F = st.origin, st.plan.floor
    local n = b.size or 1
    local state, target, pull, hold, ready = ev.go, c.target, c.pull, c.hold, c.ready
    local clear, floor, dboss = c.clear, c.floor, c.dboss
    local mx, mz = b.x + (n - 1) / 2, b.z + (n - 1) / 2
    local near_t, near_d = nil, 99
    for _, t in ipairs(c.threats) do
        local d = math.max(math.abs(t[1] - me.x), math.abs(t[2] - me.z))
        if d < near_d then near_d, near_t = d, math.atan(t[2] - mz, t[1] - mx) end
    end
    local best, bx, bz = nil, nil, nil
    -- the share's close: clear >= 1 is safe, and the tiles beside the carrier
    -- are the whole point (see _verzik_ring_events)
    local needs = { 2, 1, 0 }
    if state == "SHARE" and c.share_close then needs = { 1, 0 } end
    for _, need in ipairs(needs) do
        if best == nil then
            for dx = -2, 2 do
                for dz = -2, 2 do
                    local x, z = me.x + dx, me.z + dz
                    if floor(x, z) and clear(x, z) >= need then
                        local dd = dboss(x, z)
                        if not (hold and dd == 1) then
                            local sc = math.max(0, dd - reach) * 25
                            if ready and dd >= 1 and dd <= reach and state == "RING" then sc = sc - 40 end
                            sc = sc - math.min(clear(x, z), 6) * 4
                            local wall = math.min(x - (O.x + F[1]), (O.x + F[3]) - x, z - (O.z + F[2]), (O.z + F[4]) - z, 3)
                            sc = sc + (3 - wall) * 15
                            -- owner_verzik 2026-10-07: SHARE is EXEMPT from
                            -- the anti-clumping cost and must stay exempt
                            -- rather than being paid for its opposite.  The
                            -- +5 is the room's standing rule against clumping
                            -- (P2's spread: 1023 of Blert's 1056 bounces
                            -- reached nobody else).  REWARDING mate-adjacency
                            -- in SHARE at -60 was tried and is WORSE: svavz
                            -- went from "2 of 3 raiders, 0 damage" to "1 of 3,
                            -- 74", and svdvz from one red row to a DEATH (562
                            -- ticks and 1598 taken against 346 and 873).
                            -- Clustering costs more than it buys here,
                            -- because a trio standing together cannot tell
                            -- which tornado is whose -- the same trap the
                            -- scoring note below records, where a wrong pick
                            -- walked _play_verzik_p3's role 3 into its own.
                            -- The ball pull of 60 toward the carrier is the
                            -- right lever; mutual adjacency is not.
                            for _, m in ipairs(c.mates) do
                                if math.max(math.abs(m.x - x), math.abs(m.z - z)) <= 1 and state ~= "SHARE" then sc = sc + 5 end
                            end
                            for _, cr in ipairs(v.crabs or {}) do
                                if math.max(math.abs(cr.row.x - x), math.abs(cr.row.z - z)) <= 3 then sc = sc + 30 end
                            end
                            if target ~= nil then
                                local td = math.max(math.abs(x - target.x), math.abs(z - target.z))
                                if target.adj then td = math.max(0, td - 1) end
                                sc = sc + td * pull
                            end
                            sc = sc + math.max(math.abs(dx), math.abs(dz))
                            -- ROUND HER, AWAY FROM IT: the angle round her middle
                            -- between my tile and the nearest tornado, 10 a radian
                            -- (the offline chase: one decide in five lost, 3.9 ->
                            -- 2.6 touches and an 84 -> 65 tick enrage; it stops the
                            -- ping-pong over the tornado in a strip beside her,
                            -- _play_verzik_slow_p3 P3+304-310)
                            if near_t ~= nil then
                                local a = math.atan(z - mz, x - mx)
                                local da = math.abs((a - near_t + math.pi) % (2 * math.pi) - math.pi)
                                sc = sc - da * 10
                            end
                            if best == nil or sc < best then best, bx, bz = sc, x, z end
                        end
                    end
                end
            end
        end
    end
    if bx ~= nil and (bx ~= me.x or bz ~= me.z) then
        intent.walk = { x = bx, z = bz }
        vz.target_slot = nil
    else
        intent.walk = nil
    end
    intent.attack = false
    return nil, state
end

-- The tick's shared reading -- the tornadoes that can reach me and their next
-- steps, her melee scan, whether my swing is ready -- then the derivation and
-- the machine.  Returns the state name, having set intent.walk / intent.attack.
function QD.raid._verzik_ring(st, v, c)
    assert(st, "_verzik_ring: st")
    assert(v, "_verzik_ring: v")
    assert(c, "_verzik_ring: c")
    local P, vz = st.plan, st.vz
    local me, b = v.me, v.boss
    local O, F = st.origin, P.floor
    c.st, c.v = st, v
    c.dboss = function(x, z) return QD.raid._verzik_dist(x, z, b) end
    c.floor = function(x, z)
        return x >= O.x + F[1] and x <= O.x + F[3] and z >= O.z + F[2] and z <= O.z + F[4]
    end
    -- the tornadoes that can reach me, each with the tile it steps to next
    local threats = {}
    for slot, e in pairs(vz.tor or {}) do
        local d = math.max(math.abs(e.x - me.x), math.abs(e.z - me.z))
        -- (every one within six: a wrong "mine" is a touch; the offline
        -- chase with all of them as threats still ran 0 touches, a 34-tick
        -- median and 0.47 swings a tick)
        if d <= 6 then
            local nx, nz = e.x, e.z
            if me.x > nx then nx = nx + 1 elseif me.x < nx then nx = nx - 1 end
            if me.z > nz then nz = nz + 1 elseif me.z < nz then nz = nz - 1 end
            threats[#threats + 1] = { nx, nz }
        end
    end
    c.threats = threats
    c.clear = function(x, z)
        local m = 99
        for _, t in ipairs(threats) do m = math.min(m, math.max(math.abs(x - t[1]), math.abs(z - t[2]))) end
        return m
    end
    c.hold = c.next_attack ~= nil and v.tick >= c.next_attack - 2 and v.tick < c.next_attack
    c.ready = QD.raid._play_next_swing(st, v) <= v.tick + 1
    c.db = c.dboss(me.x, me.z)
    local m = QD.raid.sm_run(st, v, "verzik_enrage", c, QD.raid._verzik_ring_events(st, v, c))
    vz.ring_states = m.counts
    return m.state
end

-- ==========================================================================
-- raid seam53: HER PHASES AS THE TOP-LEVEL MACHINE.  The states are her forms
-- in the order she takes them (V verzik.av.npc_form_entry 10830..10836 /
-- npc_form_normal 8369..8374; the npc configs tob_verzik.npc :54-145), and the
-- transition is the `form_change` event, which carries the form the library
-- read off her one npc row through every retype.  So every state names the
-- same way forward and the same way back, and a form nobody declared aborts
-- naming it rather than silently falling through the chain.
--
-- This replaced an if/elseif on `phase` whose seven arms were 1,285 lines in
-- one function.  Each arm is now a handler of its own, named after its state,
-- with the body unchanged.
-- raid seam53 hierarchy, owner 2026-10-07 ("perhaps you need nested state
-- machines?"): THE RAIDER'S OWN WEAPON, which belongs to no phase.
--
-- This is the third of the three homes the layer's header names
-- (raid_sm.lua:52-101) and it is the one my own defect proved exists.  Her
-- shield breaking destroys the Dawnbringer IN THE HOLDER'S HAND, and in
-- _vzslow the sword went at t211 while her form did not change until t214 --
-- so for three ticks the holder was empty-handed while still in p1.  A check
-- nested under p1 could fire there but not past the boundary; an exit hook on
-- p1 fires at the boundary but cannot see those three ticks.  "I have a weapon
-- I can fight with" is true however the hand emptied -- a destroyed sword, a
-- failed equip, a dropped swap, a death and a return -- so it is a fact about
-- the RAIDER and it is NOT nested.
--   ARMED     holding something I can fight with.
--   EMPTY     the belief says dawnbringer but the phase has moved past P1, so
--             the sword is gone whatever put it there: put the main weapon
--             back on.  One tick, then ARMED.
QD.raid.sm_declare("verzik_weapon", {
    start = "ARMED",
    states = {
        ARMED = { note = "a weapon I can fight with", on = {
            orb = function(c, ev) return QD.raid._verzik_weapon_armed(c, ev) end } },
        EMPTY = { note = "the sword was destroyed in my hand; the main weapon goes back on", on = {
            orb = function(c, ev) return QD.raid._verzik_weapon_empty(c, ev) end } },
    },
})

function QD.raid._verzik_weapon_armed(c, ev)
    assert(c, "_verzik_weapon_armed: c")
    assert(ev, "_verzik_weapon_armed: ev")
    local vz, phase = c.vz, c.phase
    if vz.held == "dawnbringer" and phase ~= nil and phase ~= "pre" and phase ~= "p1" then
        vz.sword_gone = vz.sword_gone or c.v.tick
        return nil, "EMPTY"
    end
end

function QD.raid._verzik_weapon_empty(c, ev)
    assert(c, "_verzik_weapon_empty: c")
    assert(ev, "_verzik_weapon_empty: ev")
    QD.raid._verzik_main_weapon_back(c.st, c.v, c.vz, c.intent)
    return nil, "ARMED"
end

-- HER FORM CHANGES, THE SWINGS STOP (the server drops every raider's combat
-- at her retype), so the engagement the plan holds is dropped with it: the
-- next press is a real one.  Without this the seats believed they were still
-- on her across P2 -> P3 (one npc row, the same slot) and pressed nothing for
-- 200 ticks -- _vzslow 2026-10-07, P3 t499-700: no hit on her until the
-- ball's walk happened to re-arm them.
local function verzik_form_to(c, ev)
    QD.raid._verzik_engage(c.st, c.v, "stalled")
    return nil, ev.to
end
QD.raid.sm_declare("verzik_phase", {
    start = "pre",
    states = {
        pre = { on = { form_change = verzik_form_to,
            tick = function(c) return QD.raid._verzik_phase_pre(c) end } },
        -- P1'S EXIT owns the sword's end: "the sword is spent when P1 ends"
        -- is a fact about the BOUNDARY, so it runs here rather than on the
        -- first tick past it, which is where the hand-rolled guard ran.
        --
        -- The sword's own hierarchy is `verzik_bolt` -> `verzik_sword`, built
        -- where the sword is used rather than nested here: its parent is HER
        -- BOLT CADENCE, not the phase, because that is the clock that
        -- suspends a turn.  p1 is not declared as its parent because p1 is
        -- not what interrupts it.
        --
        -- p1 does NOT take `children` for it, and the reason is worth keeping:
        -- a child is stepped by the layer after the parent has handled the
        -- event and with the parent's context, while this one needs `hiding`
        -- and `on_cover` (which p1 computes late, from the launch it predicts)
        -- and RETURNS `busy`, which p1 reads in the SAME tick to decide
        -- whether to press an attack.  Inverting that -- a thin parent and a
        -- child that claims the tick -- is the next piece of the rebuild.
        p1  = { exit = function(c) return QD.raid._verzik_p1_exit(c) end,
            on = { form_change = verzik_form_to,
            tick = function(c) return QD.raid._verzik_phase_p1(c) end } },
        t12 = { on = { form_change = verzik_form_to,
            tick = function(c) return QD.raid._verzik_phase_t12(c) end } },
        p2  = { on = { form_change = verzik_form_to,
            tick = function(c) return QD.raid._verzik_phase_p2(c) end } },
        t23 = { on = { form_change = verzik_form_to,
            tick = function(c) return QD.raid._verzik_phase_t23(c) end } },
        p3  = { on = { form_change = verzik_form_to,
            tick = function(c) return QD.raid._verzik_phase_p3(c) end } },
    },
})

-- P1'S EXIT: the sword is spent when P1 ends.  Her shield breaking destroys
-- the Dawnbringer (tob_verzik.rs2 ~tob_verzik_shield_broken), so whatever the
-- holder believed, past this boundary the main weapon goes back on -- and it
-- happens AT the boundary rather than on the first tick past it, which is the
-- tick the old hand-rolled guard cost.  The raider-level invariant
-- (verzik_weapon) still stands behind this for the ways a hand can empty that
-- are nothing to do with the phase ending.
function QD.raid._verzik_p1_exit(c)
    assert(c, "_verzik_p1_exit: c")
    local vz = c.vz
    if vz.held == "dawnbringer" then
        vz.sword_gone = vz.sword_gone or c.v.tick
        QD.raid._verzik_main_weapon_back(c.st, c.v, vz, c.intent)
    end
end

-- PRE: she has not taken her first form yet.  Nothing is decided and the
-- tick's intent goes back untouched -- the one state that leaves the
-- decide early (c.bail), which is why the decide checks it below.
function QD.raid._verzik_phase_pre(c)
    assert(c, "_verzik_phase_pre: c")
    c.bail = true
end
-- P1.  The Normal trio plays its own P1 (QD.raid._verzik_p1_normal); the
-- Entry plan's is below, in this handler's second arm -- the mode split the
-- chain used to open with.
function QD.raid._verzik_phase_p1(c)
    assert(c, "_verzik_phase_p1: c")
    local st, v, intent, events = c.st, c.v, c.intent, c.events
    local P, N, O, vz, b, me = c.P, c.N, c.O, c.vz, c.b, c.me
    local phase, ok, d_boss, on_me = c.phase, c.ok, c.d_boss, c.on_me
    local go, swap_to, nearest = c.go, c.swap_to, c.nearest
    local threat = c.threat
if st.mode == "normal" then
        -- raid seam34v: the Normal trio's P1 (its own function above)
        threat = QD.raid._verzik_p1_normal(st, v, intent, ok, go, events)

else
        -- P1 (W:871-891).  Protect from Magic on the whole phase (W:871 "All
        -- players must have Protect from Magic on"); Piety while punching.
        intent.want.protectfrommagic = true
        if vz.held == "fists" then intent.want.piety = true end
        if v.attack ~= nil and v.attack.seq == P.p1_windup then
            vz.W = v.attack.tick
            vz.windups[#vz.windups + 1] = v.attack.tick
        end
        if v.attack ~= nil and v.attack.seq == P.p1_death then
            -- the shield is gone: every pillar falls on whoever is near it (W:879)
            vz.dying, vz.dying_tick = true, v.attack.tick
        end
        -- the hide tile: under the pillar south-west of her (W:887; K HIDE[1])
        local hx, hz = O.x + P.hide[1], O.z + P.hide[2]
        local sw = nil
        for _, p in ipairs(v.pillars) do
            if p.x + (p.size or 1) - 1 < b.x and p.z < b.z and (sw == nil or p.z > sw.z) then sw = p end
        end
        if sw ~= nil then hx, hz = sw.x + math.floor((sw.size or 1) / 2), sw.z - 1 end
        -- the bolts still to come, from the wind-up seen (V p1_cadence 14,
        -- launch 3, flight 3; the first at about 19: V p1_first_windup)
        local function bolt_lands(h)
            local n = 0
            if vz.W ~= nil then
                -- raid seam35e play_tob_entry_relay: counted at the LAUNCH, not
                -- the landing.  The bolt's damage and its lethal verdict are
                -- settled on the launch tick against the hitpoints then
                -- (tob_verzik.rs2 ~tob_verzik_p1_attack: "NOT tick-eatable: the
                -- verdict is settled here, on the launch tick"), so a bite
                -- timed for the landing is three ticks late: the relay's
                -- svdplayentry stood at 10 hitpoints through a launch, ate on
                -- it, and the bolt killed him (a P1 of 107 ticks; the room
                -- test's ends near 98 with the food never needed).
                for k = 0, 3 do
                    local verdict = vz.W + k * P.p1_cadence + P.p1_launch
                    if verdict > v.tick and verdict <= v.tick + h then n = n + 1 end
                end
            end
            return n
        end
        -- the first bolt is hidden from (W:885 "Afterwards, hide behind one
        -- pillar"); the rest are tanked under the prayer (W:891 "More advanced
        -- teams will ... tank her attacks entirely, to avoid losing out on
        -- ticks"), which keeps the pillar standing for its fall (W:887)
        local hiding = false
        if not vz.hid then
            if vz.hide_from == nil and (vz.n.fists >= P.p1_safe_swings or vz.W ~= nil) then vz.hide_from = v.tick end
            if vz.hide_from ~= nil then
                hiding = true
                -- out on the launch tick: cover is settled then (V p1_verdict_tick)
                if (vz.W ~= nil and v.tick >= vz.W + P.p1_launch) or v.tick > vz.hide_from + 14 then
                    vz.hid = true
                    hiding = false
                end
            end
        end
        threat = function(h)
            if hiding then return 0 end
            return bolt_lands(h) * N.bolt
        end
        if vz.dying then
            -- stand clear of every pillar's fall (the floor excludes its reach)
            if not ok(me.x, me.z) then go(me.x, me.z) end
        elseif hiding then
            go(hx, hz)
        else
            -- the weapon for this stretch: the melee the wiki names for the
            -- capped shield (W:883 "players should use their melee weapons"),
            -- then the cap row's bow, then the Dawnbringer that ignores the cap
            -- (W:875), its specials first
            local want_w = "dawnbringer"
            if vz.n.fists < P.p1_fist_swings then want_w = "fists"
            elseif vz.n.bow_accurate < P.p1_bow_swings then want_w = "bow_accurate" end
            if want_w ~= vz.held and want_w ~= "fists" then
                swap_to(want_w)
            elseif vz.held == "dawnbringer" and #vz.specs < P.p1_specs and v.tick >= vz.last_spec + 8 then
                vz.last_spec = v.tick
                QD.raid._verzik_special(st, v)
            else
                intent.attack = true
            end
        end

end
    c.threat = threat
end
-- T12: her P1 form dying into her P2 form.
function QD.raid._verzik_phase_t12(c)
    assert(c, "_verzik_phase_t12: c")
    local st, v, intent, events = c.st, c.v, c.intent, c.events
    local P, N, O, vz, b, me = c.P, c.N, c.O, c.vz, c.b, c.me
    local phase, ok, d_boss, on_me = c.phase, c.ok, c.d_boss, c.on_me
    local go, swap_to, nearest = c.go, c.swap_to, c.nearest
    local threat = c.threat
    -- P1 -> P2 (V p2_id_after_phase_event 13): clear of the pillars' fall,
    -- the ranged loadout in one block (K: the bow and the ranging
    -- potion), rapid, Rigour and Protect from Missiles for P2 (W:899
    -- "players should pray Protect from Missiles"), healed up (W:943).
    intent.want.protectfrommissiles = true
    intent.want.rigour = true
    if st.mode == "normal" and not vz.loadout then
        -- raid seam45 play_tob_verzik_melee_follows_blert: the Normal trio
        -- stays MELEE (Blert verzik_normal_3.json, 20 death-free trio rooms:
        -- the scythe in P2 and P3 in 16-18 of 20 rooms per role, melee
        -- 83-92% of attacks): Piety, the super combat potion, and the
        -- serpentine helm for the Athanatos (W:927 "has to be hit with
        -- poison or venom")
        vz.loadout = true
        vz.rapid = "melee"
        local drinks = {}
        local cr, n = QD.inv.count("br_4dose2combat")
        if cr == "ok" and n > 0 then drinks[1] = "br_4dose2combat" end
        local items = {}
        local hr, hn = QD.inv.count("serpentine_helm_charged")
        if hr == "ok" and hn > 0 then items[1] = "serpentine_helm_charged" end
        QD.raid._verzik_block(st, v, "melee loadout", items, drinks)
    end
    if st.mode == "normal" then
        intent.want.rigour = nil
        intent.want.piety = true
    end
    if not vz.loadout then
        vz.loadout = true
        local drinks = {}
        local cr, n = QD.inv.count("br_4doserangerspotion")
        if cr == "ok" and n > 0 then drinks[1] = "br_4doserangerspotion" end
        swap_to("bow_rapid", drinks)
        -- the Athanatos "has to be hit with poison or venom" (W:927); a
        -- charged serpentine helm makes the hit venomous (K:25,
        -- tob_damage.rs2 ~tob_hit_is_poisonous), so it is worn for P2
        local hr, hn = QD.inv.count("serpentine_helm_charged")
        if hr == "ok" and hn > 0 then QD.raid._verzik_block(st, v, "wear serpentine helm", { "serpentine_helm_charged" }, {}) end
    elseif vz.rapid == nil then
        QD.raid._verzik_rapid(st, v)
    end
    threat = function(h) return v.hp_base - 21 end
    if vz.dying and not ok(me.x, me.z) then
        go(me.x, me.z)
    elseif vz.dying_tick ~= nil and v.tick >= vz.dying_tick + 6 then
        -- her P2 body is the centre 3x3 (K 6431..6433 x 89..91): two out
        -- to the west (W:901 "split up, and move to opposite sides")
        -- raid seam34v: a trio splits "one goes south, one east, and one
        -- west" (W:904): p1 west, p2 east, p3 south of that body
        local hx, hz = O.x + 29, O.z + 27
        if st.mode == "normal" then
            hx, hz = QD.raid._verzik_t12_stage(st, me, O)
        end
        go(hx, hz)
    end
    c.threat = threat
end
-- THE WALK DOWN IS NOT CROSSED (owner 2026-10-07, watching watchvz: "At the
-- start of p2, all the players are standing under where verzik starts
-- bouncing and they all get hit, they should not stand there").  In the
-- transition she walks straight south from the throne (watchvz t179-189,
-- 6430,98 -> 6430,88), and her P2 body comes up at 6431..6433 x 89..91; a
-- raider beside it is slammed (~tob_verzik_p2 body slam, "pre-empts
-- everything when somebody is standing next to her") -- watchvz t192 had all
-- three at 6433,92 and the slam took 31.  The homes are on three sides of that
-- body, so the straight walk to the west one crosses her path.  So: down the
-- side of her column I am already on (x <= O+28 or >= O+35, two clear of a
-- body anywhere in O+30..O+33), to a staging row two below her landing
-- (z = O+22: her transition body ends at 88..90), then along that row to my
-- home's column.  P2's first tick sends each raider up into its home.
QD.RAID_PLAY_VERZIK_T12_LANE = { west = 28, east = 35, stage_z = 22 }
function QD.raid._verzik_t12_stage(st, me, O)
    assert(st, "_verzik_t12_stage: st")
    assert(me, "_verzik_t12_stage: me")
    assert(O, "_verzik_t12_stage: O")
    local L = QD.RAID_PLAY_VERZIK_T12_LANE
    local hx = QD.raid._verzik_p2_home(st, O.x + 31, O.z + 25, 3, 1)
    local sz = O.z + L.stage_z
    if me.z <= sz then return hx, sz end
    local west, east = O.x + L.west, O.x + L.east
    if me.x > west and me.x < east then
        -- in the column: out to the nearer side first, at my own row
        if me.x - west <= east - me.x then return west, me.z end
        return east, me.z
    end
    local lane = (me.x <= west) and west or east
    if me.x ~= lane then lane = me.x end
    return lane, sz
end

-- P2: her 3x3 flying form, the urnbomb, the zap and the Matomenos.
function QD.raid._verzik_phase_p2(c)
    assert(c, "_verzik_phase_p2: c")
    local st, v, intent, events = c.st, c.v, c.intent, c.events
    local P, N, O, vz, b, me = c.P, c.N, c.O, c.vz, c.b, c.me
    local phase, ok, d_boss, on_me = c.phase, c.ok, c.d_boss, c.on_me
    local go, swap_to, nearest = c.go, c.swap_to, c.nearest
    local threat = c.threat
    vz.dying = false
    if st.mode ~= "normal" and vz.held ~= "bow_rapid" then swap_to("bow_rapid") end
    if v.attack ~= nil and v.attack.seq == P.p2_reds then vz.reds_tick = v.attack.at or v.attack.tick vz.summon = v.attack.at or v.attack.tick end
    -- THE NEXT SUMMON, by counting her attacks (seam31): "DO NOT attack
    -- her while she summons them or immediately after, as any damage
    -- dealt will instead heal her" (Entry_Mode.wikitext:231, :235).  The
    -- summon takes an attack slot of hers: the first attack 12 ticks
    -- after it (V verzik.reds_first_attack_after, B), then one every 4
    -- (V verzik.p2_cadence, B), the summon in the slot after the count (V
    -- verzik.reds_attacks_between 7, B).  So once six attacks are seen
    -- after a summon, her next slot (the last one + 4) may be the summon,
    -- and so is each slot after it until it comes.  s31 svd (seam31
    -- triage survey): the bow's own repeat shot was rolled ON the summon
    -- tick four times (t297 29, t333 46, t369 12, t405 24: 111 healed,
    -- tob_damage.rs2 ~tob_prepare_player_hit rolls at the swing).
    if v.attack ~= nil and (v.attack.seq == P.p2_cast or v.attack.seq == P.p2_slam) and vz.summon ~= nil and v.attack.tick > vz.summon then
        if v.attack.tick ~= vz.p2_last then
            vz.p2_last = v.attack.tick
            vz.p2_count = (vz.p2_count or 0) + 1
        end
        if vz.p2_count >= P.p2_attacks_between - 1 then vz.next_summon = (v.attack.at or v.attack.tick) + P.p2_cadence end
    end
    -- prayers: Protect from Missiles (W:899), Protect from Magic once the
    -- Matomenos are summoned (W:931), back to Missiles while an urnbomb is
    -- in the air: the bomb reads it at its LANDING (the owner's ruling,
    -- V p2_bomb_prayer_read_tick)
    local bomb_air = false
    for _, p in ipairs(v.proj) do
        if p.spotanim_id == P.bomb_proj then bomb_air = true end
        -- the blood spell in the air is the reds phase too (W:933; s30
        -- vz30d: the 8117 summon was never read off her row, and 23 blood
        -- spells landed under Protect from Missiles for 20-44)
        if p.spotanim_id == P.blood_proj then vz.blood = true end
    end
    -- a fresh pair of Matomenos on the floor is a summon (W:929)
    if #v.reds > 0 and (vz.reds_tick == nil or (vz.reds_n or 0) == 0) then vz.reds_tick = vz.reds_tick or v.tick end
    if #v.reds > 0 and (vz.reds_n or 0) == 0 and vz.reds_tick ~= nil and v.tick - vz.reds_tick > 20 then vz.reds_tick = v.tick end
    -- a fresh red with no 8117 read is the summon too (s30 vz30d: 8117
    -- was not always read off her row); the count restarts from it
    if #v.reds > 0 and (vz.reds_n or 0) == 0 and (vz.summon == nil or v.tick - vz.summon > 20) then vz.summon = v.tick end
    if vz.summon ~= nil and vz.summon ~= vz.counted_from then
        vz.counted_from, vz.p2_count, vz.next_summon = vz.summon, 0, nil
        vz.summons = (vz.summons or 0) + 1
    end
    vz.reds_n = #v.reds
    if (vz.reds_tick ~= nil or vz.blood) and not bomb_air then intent.want.protectfrommagic = true else intent.want.protectfrommissiles = true end
    intent.want.rigour = true
    if st.mode == "normal" then
        -- raid seam45: the Normal trio plays P2 MELEE (its own function below)
        intent.want.rigour = nil
        intent.want.piety = true
        threat = QD.raid._verzik_p2_melee(st, v, intent, ok, go, nearest)
    else
        -- stand two out of her body on the west (W:901; V p2_scan_rule: a
        -- raider adjacent or inside on T-1 is slammed or stomped, so the floor
        -- here is 2 or more from her), off any tile something falls on
        local okp = ok
        ok = function(x, z) return okp(x, z) and QD.raid._verzik_dist(x, z, b) >= 2 end
        c.okref.fn = ok
        local home1x, home1z = b.x - 2, b.z + 1
        local home2x, home2z = b.x - 2, b.z
        if st.mode == "normal" then
            -- raid seam34v: the trio's three sides (W:904)
            home1x, home1z = QD.raid._verzik_p2_home(st, b.x, b.z, b.size or 3, 1)
            home2x, home2z = QD.raid._verzik_p2_home(st, b.x, b.z, b.size or 3, 2)
        end
        local tx, tz = home1x, home1z
        if v.shadows[home1x * 100000 + home1z] then tx, tz = home2x, home2z end
        go(tx, tz)
        local crab, cd = nearest(v.crabs)
        local purple = nearest(v.purples)
        local red = nearest(v.reds)
        crab = QD.raid._verzik_crabs(st, v, ok, go)
        threat = function(h)
            local t = N.zap
            -- raid seam34v: a Normal trio takes the zap and an urnbomb in one
            -- window (s34v _play_verzik t356: the zap 1585 bounced between two
            -- raiders and landed 26 on one at 26 hitpoints)
            if st.mode == "normal" then t = t + N.bomb end
            if cd <= 3 then t = t + N.crab end
            if vz.reds_tick ~= nil then t = t + N.blood end
            return t
        end
        -- HOLD around the predicted summon: no shot of mine is rolled from
        -- her slot to the end of the absorb (V verzik.reds_absorb_window 5).
        -- Engaged on her, the bow repeats on its own every speed ticks, so a
        -- repeat that would fall in the window is cut by a one-tile step
        -- (a step clears the attack: DRIVER_NOTES "a click is needed only to
        -- START the fight or after a step cleared it"), pressed on any tick
        -- before it; a press on her waits out the window.
        local hold = false
        if vz.next_summon ~= nil and v.tick <= vz.next_summon + P.p2_absorb then
            local S = vz.next_summon
            local nxt = st.last_swing + st.weapon.speed
            if v.tick >= S - st.weapon.speed then hold = true end
            if st.engaged and vz.target_slot == nil and nxt >= S and nxt <= S + P.p2_absorb and v.tick < nxt and intent.walk == nil then
                local sx, sz = me.x - 1, me.z
                if not ok(sx, sz) or v.shadows[sx * 100000 + sz] then sx, sz = me.x, me.z + 1 end
                if not ok(sx, sz) or v.shadows[sx * 100000 + sz] then sx, sz = me.x, me.z - 1 end
                intent.walk = { x = sx, z = sz }
                vz.steps = vz.steps + 1
                vz.holds = (vz.holds or 0) + 1
            end
        end
        if vz.next_summon ~= nil and v.tick > vz.next_summon + P.p2_absorb then vz.next_summon = nil end
        -- the Matomenos only while her summon animation plays: "players
        -- should focus on the Matomenos until this animation ends"
        -- (W:928; 8117 is 10 ticks, V verzik.av.reds_summon.seq); after it a
        -- shot on her (about 15 a hit, s31 svd: 790 in 51) beats a shot on a
        -- red that heals her at most its 20 left (tob_verzik.rs2
        -- ~tob_verzik_absorb_reds; V verzik.entry_reds_hp_1p)
        -- raid seam34v: the same rule for the Normal trio.  A red is 150 (V
        -- verzik.reds_hp_3) and heals her its remaining health at the next
        -- summon (tob_verzik.rs2 ~tob_verzik_absorb_reds), but a bow shot on
        -- her is worth nearly twice one on a red (s34v vzn2: 27 a hit on her,
        -- 15 on a red), and she summons a fresh pair every 36 ticks (V
        -- verzik.reds_attacks_between): vzn2 shot only reds from the first
        -- summon on, 528 hits on 30 reds, and P2 never ended
        if red ~= nil and vz.summon ~= nil and v.tick > vz.summon + P.p2_reds_anim then red = nil end
        if intent.walk == nil then
            -- the Athanatos first: it heals her 9-10 every 5 ticks (W:927; V
            -- p2_purple_heal); then the Matomenos (W:931)
            local add = crab or purple or red
            local absorb = hold or (vz.reds_tick ~= nil and v.tick <= vz.reds_tick + P.p2_absorb)
            if add ~= nil then
                local idle = v.tick - math.max(st.last_swing, st.engaged_tick) > st.weapon.speed + 2
                if vz.target_slot ~= add.row.slot or not st.engaged or idle then QD.raid._verzik_press_add(st, v, add) end
            elseif not absorb then
                if vz.target_slot ~= nil then vz.target_slot = nil end
                intent.attack = true
            end
        end
    end
    c.threat = threat
end
-- T23: her P2 form dying into her P3 form.
function QD.raid._verzik_phase_t23(c)
    assert(c, "_verzik_phase_t23: c")
    local st, v, intent, events = c.st, c.v, c.intent, c.events
    local P, N, O, vz, b, me = c.P, c.N, c.O, c.vz, c.b, c.me
    local phase, ok, d_boss, on_me = c.phase, c.ok, c.d_boss, c.on_me
    local go, swap_to, nearest = c.go, c.swap_to, c.nearest
    local threat = c.threat
    -- P2 -> P3: heal to full (W:943 "Make sure to heal to full before the
    -- next phase starts"); the prayers stay up for her first auto
    intent.want.protectfrommissiles = true
    intent.want.rigour = true
    if st.mode == "normal" then intent.want.rigour = nil intent.want.piety = true end
    threat = function(h) return v.hp_base - 21 end
    vz.reds_tick = nil
    c.threat = threat
end
-- P3: her walking form, the special rotation, the tornadoes and the enrage.
function QD.raid._verzik_phase_p3(c)
    assert(c, "_verzik_phase_p3: c")
    local st, v, intent, events = c.st, c.v, c.intent, c.events
    local P, N, O, vz, b, me = c.P, c.N, c.O, c.vz, c.b, c.me
    local phase, ok, d_boss, on_me = c.phase, c.ok, c.d_boss, c.on_me
    local go, swap_to, nearest = c.go, c.swap_to, c.nearest
    local threat = c.threat
    QD.raid._verzik_ball_expire(st, v)
    -- raid seam45: the Normal trio plays P3 MELEE (Blert: the scythe,
    -- 21-24 swings a role); the clock and the step out are
    -- QD.raid._verzik_p3_clock's, the rest of the phase is shared
    local melee = st.mode == "normal"
    if not melee and vz.held ~= "bow_rapid" then swap_to("bow_rapid") end
    -- owner_verzik: the slow pace's P3 weapon (the header's table) goes on
    -- at P3; P1 and P2 are played with the scythe as the fast team does
    -- (never with a tornado about: a swap is a block of its own that holds the
    -- decide a tick or two -- wipslowp3 t251-253, the scythe going on while
    -- its tornado walked the last three tiles)
    local tor_near = false
    for _, e in pairs(vz.tor or {}) do
        -- (three: it touches on my own tile only, fdf77aae1c)
        if math.max(math.abs(e.x - v.me.x), math.abs(e.z - v.me.z)) <= 3 then tor_near = true end
    end
    if melee and vz.p3_main ~= nil and vz.held ~= vz.p3_main and vz.held ~= "dawnbringer" and vz.held ~= "claws" and not tor_near then swap_to(vz.p3_main) end
    -- her style shows on the attack tick and the protection is read when
    -- it lands (the owner's ruling; V p3_prayer_read: "8125 stomp + 1593,
    -- 8124 crackle + 1594 and the flight is 2-3 ticks"): switch on sight
    -- (W:951 "it's important to switch prayers accordingly")
    -- raid seam53: the ball is the derived event's reading (vz.sm_ball,
    -- QD.raid._verzik_events); this loop keeps only what it alone reads
    local ball = vz.sm_ball ~= nil
    for _, p in ipairs(v.proj) do
        if p.spotanim_id == P.p3_ranged_proj then vz.p3_style = "protectfrommissiles" end
        if p.spotanim_id == P.p3_magic_proj then vz.p3_style = "protectfrommagic" end
    end
    if v.attack ~= nil then
        if v.attack.seq == P.p3_magic then vz.p3_style = "protectfrommagic"
        elseif v.attack.seq == P.p3_ranged then vz.p3_style = "protectfrommissiles" end
    end
    intent.want[vz.p3_style] = true
    intent.want.rigour = true
    if melee then intent.want.rigour = nil intent.want.piety = true end
    -- (a ratio of 0 is no bar yet, not an enrage: see _verzik_slow_hold)
    if b.health_ratio ~= nil and b.health_ratio > 0 and b.health_scale ~= nil and b.health_scale > 0 and b.health_ratio * 5 <= b.health_scale then vz.enraged = true end
    local cadence = vz.enraged and P.p3_enraged_cadence or P.p3_cadence
    -- the floor: two out of her (W:953 "the primary tank should either
    -- walk under or away from Verzik one or two ticks before she attacks
    -- to avoid the melee attack"; V p3_melee_predicate adjacent on T-1)
    local okp = ok
    ok = function(x, z) return okp(x, z) and QD.raid._verzik_dist(x, z, b) >= 2 end
    c.okref.fn = ok
    -- raid seam45: her clock, read every tick (the ball is in `ball`)
    local m3_next, m3_hold = nil, false
    if melee then m3_next, m3_hold = QD.raid._verzik_p3_clock(st, v, ball) end
    -- raid seam49: only her tank steps out; the other two stay beside her
    -- and swing (her melee needs the tank in reach, ~tob_verzik_tank_in_melee)
    local tank = (not melee) or QD.raid._verzik_is_tank(st, v)
    -- (on an auto after an auto only: around a special the other two
    -- hold as before -- s49 final svc/svd, members beside her through the
    -- specials died at t514 / t555 on one tile)
    if not tank and vz.m3_sure and v.tick <= (m3_next or 0) then m3_hold = false end
    -- raid seam52 play_tob_verzik_last: and around her specials too.  Her
    -- melee is judged on the tank alone (tob_verzik.rs2
    -- ~tob_verzik_tank_in_melee; V verzik.p3_melee_predicate) and the
    -- tank still holds on an unsure slot, so a member has no dangerous
    -- tick: seam51's members stood ready and not swinging 58-67 ticks of
    -- P3 (sva 593-599 before the yellows: the unsure hold).  Blert's trios
    -- swing 22-33 times a raider in a 122-200 tick P3.  (seam49's deaths
    -- beside her through a special were two members on ONE pool, since
    -- fixed: the r-th pool.)  The pool, the crabs and the webs still win.
    if not tank and st.mode == "normal" then m3_hold = false end
    -- owner_verzik: the slow pace's halberd swings from two out (its
    -- reach, the header), outside her melee, which hits only those
    -- beside her (W:946 "every player next to her")
    local halb = melee and vz.held == "halberd"
    local reach = halb and 2 or 1
    vz.reach_now = reach
    -- owner_verzik: her special rotation as a state machine (events: her
    -- seq and the ball's projectile; QD.raid._verzik_p3_cycle)
    local cyc = QD.raid._verzik_p3_cycle(st, v, events)
    -- owner_verzik: the slow pace holds back until her rotation has come
    -- round to the green ball and it has been shared (the owner: "kills p3
    -- slow enough so that the green orb appears"); from then the halberd
    -- seats take the scythe they carried through P2, and the enrage is
    -- fought as the fast team fights it
    if vz.p3_main == "halberd" and cyc.state == "autos" and cyc.next == "crabs" then vz.p3_main = "scythe" end
    vz.m3 = vz.m3 or { outs = 0, late = 0, holds = 0, dodges = 0, log = {} }
    -- (late: the tank beside her at the end of T-1, raid seam51)
    if melee and tank and m3_next ~= nil and v.tick == m3_next - 1 and d_boss == 1 and vz.m3_sure then vz.m3.late = vz.m3.late + 1 end
    -- (raid seam49: the members' every-8-ticks re-press is gone: every
    -- raider SEES its own swings now (raid seam48), and the library's
    -- _play_attack presses again when none came for speed + 1)
    -- (raid seam51: the "engaged from two out and not moving: press
    -- again" rule is gone with P2's: every raider sees its own swings)
    -- a yellow pool: stand on one until the blast is over (W:969)
    local pool = nil
    for _, p in ipairs(v.pools) do
        local d = math.max(math.abs(p.x - me.x), math.abs(p.z - me.z))
        if pool == nil or d < pool.d then pool = { x = p.x, z = p.z, d = d } end
    end
    -- raid seam34v: "Each pool can only hold one player, so players should
    -- coordinate which pool they're going for" (W:968): in a trio p(r)
    -- takes the r-th pool in x, then z order (one pool per living raider,
    -- V verzik.p3_yellow_pools)
    -- (one tile per pool: the client lists each pool's graphic three
    -- times, s34v vzn3 t727 map_spotanim 1595 x3 per tile, and the whole
    -- trio stood on 6430,93 and took the blast)
    local seen_pool, uniq = {}, {}
    for _, p in ipairs(v.pools) do
        if not seen_pool[p.x * 100000 + p.z] then
            seen_pool[p.x * 100000 + p.z] = true
            uniq[#uniq + 1] = p
        end
    end
    -- owner_verzik: ONE POOL A RAIDER BY WHERE THEY STAND.  The content
    -- draws each raider's pool beside that raider (tob_verzik.rs2
    -- ~tob_verzik_place_pool / ~tob_verzik_pool_near: within two of them),
    -- and a shared pool protects nobody (W:975 "Each pool can only hold one
    -- player"; W:977).  The r-th pool in x, z order sent two raiders of
    -- the whole-room survey to one pool (_play_verzik_slow P3+163: p0 and
    -- p2 on 6438,94, both struck, once the blast judged every raider).
    -- Then: the raiders in pid order each took the nearest pool nobody had
    -- taken, chosen once the full set was in view and kept for the charge --
    -- replaced below.
    -- 2026-10-07 (_vzslow P3+139: p0 and p2 BOTH on 6429,93, 1 of 3
    -- protected): the greedy pick above was taken once, on the first tick the
    -- set was in view, from each client's own view of its mates' tiles -- a
    -- tile behind, mid-step -- and then kept.  A one-tile difference flipped
    -- who was nearest, two clients latched one pool, and nothing ever
    -- revisited it.  Now: the assignment with the least total walk over every
    -- one-raider-one-pool pairing (three raiders = six), raiders in pid order
    -- and pools in x, z order, the first such pairing found winning a tie --
    -- recomputed EVERY tick, so identical tiles give every client the same
    -- answer and a passing disagreement cannot be locked in.
    local function pool_pick(list)
        local raiders = { { pid = st.my_pid or 99, x = me.x, z = me.z, me = true } }
        for _, m in ipairs(QD.raid._verzik_mates(st)) do raiders[#raiders + 1] = { pid = m.pid or 99, x = m.x, z = m.z } end
        table.sort(raiders, function(r1, r2) return r1.pid < r2.pid end)
        local pools = {}
        for _, p in ipairs(list) do pools[#pools + 1] = p end
        table.sort(pools, function(p1, p2) return p1.x < p2.x or (p1.x == p2.x and p1.z < p2.z) end)
        local used, best, best_mine = {}, nil, nil
        local function pair(i, cost, mine)
            if best ~= nil and cost >= best then return end
            if i > #raiders then best, best_mine = cost, mine return end
            local r = raiders[i]
            for j, p in ipairs(pools) do
                if not used[j] then
                    used[j] = true
                    local d = math.max(math.abs(p.x - r.x), math.abs(p.z - r.z))
                    pair(i + 1, cost + d, r.me and p or mine)
                    used[j] = false
                end
            end
            -- fewer pools than raiders: this raider goes without (dearly)
            pair(i + 1, cost + 100, mine)
        end
        pair(1, 0, nil)
        return best_mine
    end
    if st.party > 1 and #uniq > 0 then
        local p = pool_pick(uniq)
        if p ~= nil then pool = { x = p.x, z = p.z, d = math.max(math.abs(p.x - me.x), math.abs(p.z - me.z)) } end
    end
    local on_pool = pool ~= nil and pool.d == 0
    -- a tornado that is near: away from it (W:986 "tracks them down";
    -- 50% of the current hitpoints and triple that healed)
    -- WHERE IT IS NOW.  The client's row for the tornado stays on its
    -- spawn tile (s31 vz31d: api_drive.npcs gave 6431,91 for nine ticks
    -- while the server's npc_tile rows walked it 6431..6424, one a tick),
    -- so the plan walks it itself from what it saw appear: one tile a tick
    -- straight at its raider (tob_verzik.rs2 ~tob_verzik_tornado_tick,
    -- npc_walk to the raider's tile every tick; W:981 "tracks them down"),
    -- touching at range 1.  A row that does move is believed instead.
    vz.tor = vz.tor or {}
    local live = {}
    for _, tr in ipairs(v.tornadoes) do
        local e = vz.tor[tr.slot]
        if e == nil or e.rx ~= tr.x or e.rz ~= tr.z then
            -- (owner_verzik: a tornado's row trails the server by ONE tick
            -- -- wipfastp3 t203-209: row 6439,95 at 207, the server's
            -- 6439,96 -- so it is walked one step on from its row)
            -- (owner_verzik: and WHOSE it is.  Each tornado walks at its
            -- own raider only and touches only them (tob_verzik.rs2
            -- ~tob_verzik_tornado_tick: npc_range of its owner's tile).
            -- "identify your tornado ... watch and see which tornado
            -- starts following you" (transcripts/yt_sDaQ2qsU8AQ.md,
            -- Plank2g, Tornado DPS Guide): a step of its row that points at
            -- my tile scores one, any other step loses one)
            local score = 0
            if e ~= nil then
                score = e.score or 0
                local sx = (tr.x > e.rx and 1) or (tr.x < e.rx and -1) or 0
                local sz = (tr.z > e.rz and 1) or (tr.z < e.rz and -1) or 0
                local wx = (me.x > e.rx and 1) or (me.x < e.rx and -1) or 0
                local wz = (me.z > e.rz and 1) or (me.z < e.rz and -1) or 0
                if sx == wx and sz == wz then score = score + 1 else score = score - 1 end
            end
            e = { x = tr.x, z = tr.z, rx = tr.x, rz = tr.z, tick = v.tick - 1, moved = (e ~= nil) and v.tick or nil, score = score }
            vz.tor[tr.slot] = e
        end
        while e.tick < v.tick do
            e.tick = e.tick + 1
            -- (the step the server took was toward where I stood the tick
            -- BEFORE: its walk aims at my tile of the previous tick)
            local mx0, mz0 = me.x, me.z
            if vz.prev_me ~= nil and e.tick == v.tick then mx0, mz0 = vz.prev_me.x, vz.prev_me.z end
            if math.max(math.abs(e.x - mx0), math.abs(e.z - mz0)) > 1 then
                if false then
                    -- (owner_verzik 2026-10-07: no longer -- a tornado
                    -- walks through her, tob.npc [tob_verzik_creeper]
                    -- blockwalk=none after Blert; the straight step below)
                    -- raid seam45: her body is a wall to it (s45 e8 sva
                    -- t960-961: from her SW tile 6432,91 toward a raider
                    -- at 6431,96 its first step was 6431,91, west, not the
                    -- diagonal past her corner), so it steps as the
                    -- server's npc_walk does: the diagonal when its tile
                    -- and both sides are off her, else along x, else z
                    local sx = (me.x > e.x and 1) or (me.x < e.x and -1) or 0
                    local sz = (me.z > e.z and 1) or (me.z < e.z and -1) or 0
                    local function free(x, z) return QD.raid._verzik_dist(x, z, b) > 0 end
                    if sx ~= 0 and sz ~= 0 and free(e.x + sx, e.z + sz) and free(e.x + sx, e.z) and free(e.x, e.z + sz) then
                        e.x, e.z = e.x + sx, e.z + sz
                    elseif sx ~= 0 and free(e.x + sx, e.z) then
                        e.x = e.x + sx
                    elseif sz ~= 0 and free(e.x, e.z + sz) then
                        e.z = e.z + sz
                    end
                else
                    if mx0 > e.x then e.x = e.x + 1 elseif mx0 < e.x then e.x = e.x - 1 end
                    if mz0 > e.z then e.z = e.z + 1 elseif mz0 < e.z then e.z = e.z - 1 end
                end
            end
        end
        live[tr.slot] = true
    end
    for slot, _ in pairs(vz.tor) do
        if not live[slot] then vz.tor[slot] = nil end
    end
    -- owner_verzik: mine, once one is known (score 2 or more and the best);
    -- until then every one is treated as mine (the V-out: away from all)
    -- (known = the best score is 3 or more and leads the next by 2:
    -- raiders close together see two tornadoes step their way, and a
    -- wrong pick walked _play_verzik_p3's role 3 into its own, t196)
    local mine_slot, mine_sc, second = nil, -99, -99
    for slot, e in pairs(vz.tor) do
        local sc = e.score or 0
        if sc > mine_sc then second = mine_sc mine_slot, mine_sc = slot, sc
        elseif sc > second then second = sc end
    end
    if mine_sc < 3 or mine_sc - second < 2 then mine_slot = nil end
    vz.tor_mine = mine_slot
    local tor, td = nil, 999
    -- raid seam45: the nearest tornado SEEN moving this tick or last
    -- (its row changed tile), for the melee dodge: a row that moves is
    -- what a person sees; the simulated walk is not
    local seen_tor, seen_td = nil, 999
    for slot, e in pairs(vz.tor) do
        local d = math.max(math.abs(e.x - me.x), math.abs(e.z - me.z))
        if d < td then tor, td = e, d end
        if e.moved ~= nil and v.tick - e.moved <= 1 and d < seen_td then seen_tor, seen_td = e, d end
    end
    local _, cd = nearest(v.crabs)
    local crab = nil
    -- raid seam45: my tile on her east edge (the side the tornadoes
    -- cannot reach, below)
    local n3 = b.size or 1
    local hx3, hz3 = b.x + n3 + (reach - 1), b.z + n3 - 1 - 2 * ((st.role - 1) % 3)
    -- owner_verzik 2026-10-07: she follows her tank now, so the east edge
    -- is not a fixed place: it drifted into the room's north-east corner,
    -- where the tank was cornered by its tornado (_play_verzik_p3 t218-222,
    -- 6441,98).  The side of her that faces the middle of the room, the
    -- three a tile or two apart along it ("Try to keep Verzik at the
    -- centre", W:953; "think in rectangles", yt_sDaQ2qsU8AQ): room to run.
    do
        -- (and each raider its OWN side of her, the tank on the side that
        -- faces the middle, the others on the sides either side of it:
        -- three raiders on one side drew three tornadoes into one
        -- corner of floor -- _play_verzik_slow_p3 P3+285-291, the leader
        -- boxed between his own and the other two and touched; "isolate
        -- ... separating yourself from everyone before tornadoes spawn",
        -- yt_sDaQ2qsU8AQ)
        local F0 = P.floor
        local mx, mz = O.x + (F0[1] + F0[3]) / 2, O.z + (F0[2] + F0[4]) / 2
        local bcx, bcz = b.x + (n3 - 1) / 2, b.z + (n3 - 1) / 2
        -- sides: 0 east, 1 north, 2 west, 3 south
        local side
        if math.abs(mx - bcx) >= math.abs(mz - bcz) then side = (mx >= bcx) and 0 or 2
        else side = (mz >= bcz) and 1 or 3 end
        side = (side + ({ 0, 1, 3 })[((st.role - 1) % 3) + 1]) % 4
        local cx, cz = math.floor(bcx + 0.5), math.floor(bcz + 0.5)
        if side == 0 then hx3, hz3 = b.x + n3 + reach - 1, cz
        elseif side == 2 then hx3, hz3 = b.x - reach, cz
        elseif side == 1 then hx3, hz3 = cx, b.z + n3 + reach - 1
        else hx3, hz3 = cx, b.z - reach end
        -- a side against the wall: the next side round that is floor
        for _ = 1, 3 do
            if okp(hx3, hz3) then break end
            side = (side + 1) % 4
            if side == 0 then hx3, hz3 = b.x + n3 + reach - 1, cz
            elseif side == 2 then hx3, hz3 = b.x - reach, cz
            elseif side == 1 then hx3, hz3 = cx, b.z + n3 + reach - 1
            else hx3, hz3 = cx, b.z - reach end
        end
    end
    if melee and vz.enraged and #vz.m3.log < 20 then
        local rows = {}
        for _, tr in ipairs(v.tornadoes) do rows[#rows + 1] = tr.slot % 10 .. ":" .. tr.x .. "," .. tr.z end
        vz.m3.log[#vz.m3.log + 1] = v.tick .. "d" .. d_boss .. "@" .. me.x .. "," .. me.z .. (tor and ("T" .. tor.x .. "," .. tor.z .. "/" .. td) or "") .. "m" .. tostring(vz.tor_mine and vz.tor_mine % 10) .. "r" .. table.concat(rows, ";")
    end
    -- raid seam34v: where the other raiders stand (the green ball
    -- "bounce[s] ... by being next to another player", W:975, and most
    -- teams "simply take the hit"; a tornado chases its own raider, W:981):
    -- a trio keeps a tile between its raiders
    local mates = {}
    if st.party > 1 then
        local pr, prow = api_drive.players()
        if pr == "ok" then
            for _, r in ipairs(prow) do
                if not r.me and QD.raid._verzik_on_floor(st, r) then mates[#mates + 1] = r end
            end
        end
    end
    local function crowd(x, z)
        local n = 0
        for _, m in ipairs(mates) do
            if math.max(math.abs(m.x - x), math.abs(m.z - z)) <= 1 then n = n + 1 end
        end
        return n
    end
    threat = function(h)
        local t = N.auto * math.ceil(h / cadence)
        if pool ~= nil and not on_pool then t = t + N.blast end
        if cd <= 3 then t = t + N.crab end
        if tor ~= nil and td <= 2 then t = t + math.floor(v.hp * N.tornado_pct / 100) end
        -- enraged: "it is best to keep health around 50-60; it is more
        -- than enough to tank one off-prayer range or magic attack, and the
        -- tornado will only heal around 90" (W:981): no bite above that
        -- band unless the green ball is in the air
        if vz.enraged and not ball then t = math.min(t, P.enrage_hp_floor) end
        -- raid seam34v: her melee, 63 on everyone beside her that cannot
        -- be prayed (W:942, V p3_melee_max), whenever she is within two
        -- (she walks at her target first: ET 1.1); s34v _play_verzik t579:
        -- the leader ran from its tornado into a corner, she followed and
        -- her melee took its last 51 of a 62
        -- (raid seam49: the tank's alone -- her melee needs the tank beside
        -- her, and the tank steps out of it)
        if st.mode == "normal" and d_boss <= 2 and tank and not (melee and vz.enraged and not ball) then t = math.max(t, N.melee) end
        -- owner_verzik (the sva deaths t676, both members beside her at
        -- 51 and 40 when her melee came: 51 + 40, the tank in reach on
        -- T-1): everyone beside her keeps more than her melee, in the
        -- enrage too -- "deals up to 63 damage on every player next to
        -- her ... players should keep their health above 80 to avoid
        -- being killed by this attack" (W:946-949).  A raider cannot
        -- tell whether the tank will be late.
        if st.mode == "normal" and d_boss <= 1 then t = math.max(t, N.melee) end
        -- owner_verzik: the green ball is the special after the yellows,
        -- 75% of the Hitpoints level (W:982 "make sure to heal up before
        -- attempting to tank the ball"; W:977 "Verzik is invulnerable while
        -- charging this attack, so use this time to restore health";
        -- yt_3lQjrLeuvHo 1:51 "heal up to full hp in preparation for the
        -- green ball"): on the pool, healed to the ball and an auto over
        -- it; between the yellows and the ball, above the ball
        -- (owner_verzik: no heal-up for the ball itself any more -- it is
        -- shared, below, and a shared ball hits nobody; on the pool the
        -- invulnerable charge restores to above her melee: W:977 "use
        -- this time to restore health and stats as needed")
        if st.mode == "normal" and cyc.state == "yellows" then t = math.max(t, N.melee) end

        -- raid seam34v: a nylocas's blast is outside the enrage band
        -- (s34v _play_verzik t652: a magic nylocas took a leader's last 47
        -- of a 55 with the band capping the bite at 45): 63 within 3 of
        -- one (tob.constant ^tob_verzik_p2_nylo_blast_near/_mid/_far 63/26/8, range 3;
        -- the plan reads it from 4, one tile of its walk ahead)
        if st.mode == "normal" and cd <= 4 then t = math.max(t, 63) end
        -- raid seam52: a web on my tile snaps for up to 40 if no teammate
        -- breaks it (tob.constant ^tob_verzik_p3_web_break_max 40, [M50];
        -- tob_verzik.rs2 [ai_timer,verzik_web_npc]), one hit per web on
        -- the tile (e1 _play_verzik t520: two members on one tile, two
        -- webs, 27+35 and 40+12, both dead from 62 and 52)
        if st.mode == "normal" then
            local mine = 0
            for _, w in ipairs(v.webs) do
                if w.row.x == me.x and w.row.z == me.z then mine = mine + 1 end
            end
            if mine > 0 then t = math.max(t, 40 * mine) end
        end
        -- raid seam45: every nylocas within 4 is its own 63 (s45 e14 svb; the
        -- blast is rolled 1-63, tob.constant ^tob_verzik_p2_nylo_blast_near)
        if melee then
            local near = 0
            for _, c in ipairs(v.crabs) do
                if math.max(math.abs(c.row.x - me.x), math.abs(c.row.z - me.z)) <= 4 then near = near + 1 end
            end
            -- (s45 e19 svb p3: two nylocas on arrival, 45 + 50 at 95)
            if near >= 2 then t = math.max(t, math.min(63 * near, 126)) end
        end
        return t
    end
    local webbed = false
    for _, w in ipairs(v.webs) do
        if w.row.x == me.x and w.row.z == me.z then webbed = w end
    end
    -- raid seam52: bound on my OWN web, the melee raider cannot swing at
    -- it (it is under me, and a bound raider does not walk): a mate's web
    -- one straight step away is broken instead, else her if she is in
    -- reach (e2 svb: both members bound t513-529 beside her, pressing
    -- their own webs, no swing for 16 ticks)
    if st.mode == "normal" and melee and webbed and st.party > 1 then
        local mate_web = false
        for _, r in ipairs(mates) do
            for _, w in ipairs(v.webs) do
                if w.row.x == r.x and w.row.z == r.z and math.abs(w.row.x - me.x) + math.abs(w.row.z - me.z) == 1 then mate_web = w end
            end
        end
        webbed = mate_web
        vz.m3.bound = (vz.m3.bound or 0) + 1
        vz.m3.bound_tick = v.tick
    end
    -- raid seam34v: a raider caught in a web is freed by ANOTHER player
    -- "breaking the web, which has 10 Hitpoints" (W:955): a web on a
    -- teammate's tile is shot first
    if st.party > 1 and not webbed and not (st.mode == "normal" and melee and vz.m3.bound_tick == v.tick) then
        local pr, prow = api_drive.players()
        if pr == "ok" then
            for _, r in ipairs(prow) do
                if not r.me then
                    for _, w in ipairs(v.webs) do
                        if w.row.x == r.x and w.row.z == r.z then webbed = w end
                    end
                end
            end
        end
    end
    -- raid seam34v: the pool AT THE LAST MOMENT while a tornado chases
    -- (W:981 "try to enter the safe tile at the last possible moment";
    -- W:983 "it can be difficult to handle both mechanics at once"): the
    -- blast lands when the pool goes (V p3_yellow_pool_lifetime 14), so
    -- the raider keeps running and steps on with its walk to the pool
    -- plus two ticks left.  s34v sva: every raider walked straight to its
    -- pool at the yellows (t717, t892) and the tornadoes took all three
    -- there (t719, t721, t723; t900, t905: 353 taken, 1,059 healed).
    local pool_late = false
    local near_pool = nil
    -- (the green ball's corner as the dodge's pull while it flies)
    if vz.share_hold ~= nil and ball then near_pool = vz.share_hold
    elseif not ball then vz.share_hold = nil end
    if st.mode == "normal" and pool ~= nil and not on_pool and tor ~= nil and td <= 6 and vz.pool_first ~= nil then
        local left = vz.pool_first + P.pool_life - v.tick
        -- (owner_verzik: with a tornado about the pool is reached through
        -- the dodge's tiles, never a straight walk -- wipfastp3 t219, the
        -- leader's walk to its pool went through its tornado -- and the
        -- pull to it grows as the charge runs out)
        pool_late = true
        near_pool = pool
        -- (four ticks to spare, not two: s_room_slow svb P3+322, the tank
        -- reached its pool on the blast's own tick)
        vz.pool_pull = (left > math.ceil(pool.d / 2) + 4) and 10 or 60
    end
    -- owner_verzik: THE GREEN BALL IS SHARED (the owner, 2026-10-07: "use
    -- the real mechanic and not use a cheese mechanic to beat the ball,
    -- they should share the ball as the mechanic intended").  "She can
    -- also launch a green projectile which must be bounced between every
    -- player of the team or the player who is targeted will take up to
    -- 74% of their Hitpoints level ... This cannot be bounced to the same
    -- player twice" (wiki_Verzik_Vitur.wikitext:402); "Players can bounce
    -- the ball by being next to another player before impact, continuing
    -- to do so to a different player until it safely dissipates" (W:980);
    -- "the targeted player should follow another nearby player to ensure
    -- that it will bounce" (W:982).  The reference: Blert 0f9abe1a P3+187,
    -- target and mate adjacent +3..+5, apart at +6.  A hop is one tick
    -- (tob_verzik.rs2 [queue,tob_verzik_ball_land]), so for a trio's chain
    -- target -> mate -> mate every one must be next to the next: the
    -- three gather on a 2x2 corner at the target's tile (each tile within
    -- one of the other two), the corner away from her body, and hold it
    -- until the chain has run its hops (team - 1 ticks after the impact),
    -- then step apart.  The ball's dst is its target's live tile (a
    -- homing projectile, world.lua QD.world.projectiles).
    local share = nil
    if st.mode == "normal" and st.party > 1 then
        local tx, tz, bleft = nil, nil, nil
        for _, p in ipairs(v.proj) do
            if p.spotanim_id == P.ball_proj then
                tx, tz = p.dst_x, p.dst_z
                bleft = math.ceil((p.cycles_left or 0) / QD.RAID_PLAY_CYCLES_PER_TICK)
            end
        end
        -- (owner_verzik: with her tornadoes out the corner is taken only for
        -- the ball's last ticks -- the flight is eight -- and dodged to until
        -- then, held near the target: three touches on the corner at
        -- wipfastp3 P3+186-189; the hops still find all three adjacent)
        local tor_out = false
        for _, e in pairs(vz.tor or {}) do tor_out = true end
        -- (the owner: the ball is SHARED in the enrage too, no tank. With
        -- tornadoes out the trio dodges toward the target's tile (the pull
        -- below) and gathers on the corner for the flight's last five
        -- ticks -- a run of up to ten tiles -- so the tornadoes have the
        -- least time on a standing trio)
        if tx ~= nil and tor_out and bleft ~= nil and bleft > 5 then
            vz.share_hold = { x = tx, z = tz }
            tx = nil
        end
        if tx ~= nil then
            local me_target = (tx == me.x and tz == me.z)
            if me_target then cyc.target_me = v.tick end
            local cx, cz = b.x + (b.size or 1) / 2, b.z + (b.size or 1) / 2
            local sx = (tx >= cx) and 1 or -1
            local sz = (tz >= cz) and 1 or -1
            local function free(x, z) return okp(x, z) and QD.raid._verzik_dist(x, z, b) >= 1 end
            if not (free(tx + sx, tz) and free(tx + sx, tz + sz)) then sx = -sx end
            if not (free(tx, tz + sz) and free(tx + sx, tz + sz)) then sz = -sz end
            -- the others, by pid, take the two tiles beside the target
            -- (then the corner): each within one of the target and of
            -- each other
            local others = {}
            if not me_target then others[#others + 1] = { pid = st.my_pid or 99, me = true } end
            for _, m in ipairs(mates) do
                if not (m.x == tx and m.z == tz) then others[#others + 1] = { pid = m.pid or 99 } end
            end
            table.sort(others, function(p1, p2) return p1.pid < p2.pid end)
            local spots = { { tx + sx, tz }, { tx, tz + sz }, { tx + sx, tz + sz } }
            local mine = nil
            for i, o in ipairs(others) do if o.me then mine = spots[i] end end
            if me_target then mine = { tx, tz } end
            if mine ~= nil then
                share = { x = mine[1], z = mine[2], target = me_target, left = bleft }
                cyc.share = { x = mine[1], z = mine[2], until_tick = nil }
            end
        elseif cyc.share ~= nil then
            -- landed: the chain hops once a tick to every raider, so the
            -- corner holds for the hops (team - 1) and one tick of input
            -- lag, then breaks up
            if cyc.share.until_tick == nil then cyc.share.until_tick = v.tick + st.party end
            if v.tick <= cyc.share.until_tick then
                share = { x = cyc.share.x, z = cyc.share.z, hold = true }
            else
                cyc.share = nil
                cyc.split_until = v.tick + 2
            end
        end
    end
    -- THE TORNADO CHECK ON THE SHARING WALK (owner_verzik; the coordinator,
    -- 2026-10-07): a tornado whose next step lands on my corner tile, or on
    -- the tile I stand on, wins over the corner this tick -- svcplayverzi
    -- t210-213, a raider held on the ball's corner while its tornado walked
    -- the last tile onto it
    if share ~= nil then
        for _, e in pairs(vz.tor or {}) do
            local nx, nz = e.x, e.z
            if me.x > nx then nx = nx + 1 elseif me.x < nx then nx = nx - 1 end
            if me.z > nz then nz = nz + 1 elseif me.z < nz then nz = nz - 1 end
            -- in the ball's last 3 ticks the share is kept unless the tornado
            -- steps ONTO it: a tornado touches only on its raider's own tile
            -- (fdf77aae1c, Blert 16 of 16), and yielding there is what left
            -- the target alone with the whole 74 (_vzslow P3+190)
            local closing = share.left ~= nil and share.left <= 3
            if (closing and nx == share.x and nz == share.z)
                or (not closing and ((math.abs(nx - share.x) <= 1 and math.abs(nz - share.z) <= 1)
                    or (math.abs(nx - me.x) <= 1 and math.abs(nz - me.z) <= 1))) then
                share = nil
                cyc.share_yield = (cyc.share_yield or 0) + 1
                break
            end
        end
    end
    -- owner_verzik: THE ENRAGE IS THE RING RUN (QD.raid._verzik_ring)
    local ring_state = nil
    -- (not while a green ball chain runs: the ring stacked the trio on one
    -- tile for it -- svbvzslow t828, all three on 6438,89 -- and a stack is
    -- the crowd that explodes on everyone (owner); the chain places each seat,
    -- and the avoid loop still keeps the tornadoes off)
    local chain_on = vz.bm ~= nil and not vz.bm.done
    -- THE ENRAGE IS POWERED THROUGH, not circled (W:983 "Teams with
    -- sufficient experience can simply power through into enrage; they can
    -- either keep their health low so the tornado heals little"; W:981 "keep
    -- health around 50-60 ... the tornado will only heal around 90").  The ring
    -- kept every seat circling away from its tornado and swinging about a
    -- third of the ticks it could: svavzslow (ae7ca6e50) dealt 1839 in P3's
    -- first 125 ticks, then from the enrage ~4 a tick against ~800 healed, 90-140
    -- short of her for 250 ticks while the packs emptied.  Her last 20% is ~490:
    -- about 35 ticks at the opening's pace.  So in the enrage a seat stays on
    -- her and swings, and steps only when its tornado's next step is onto it
    -- (the guard, last in the tick).
    local power_through = QD.RAID_PLAY_VERZIK_POWER_THROUGH and st.mode == "normal"
    if melee and not power_through and vz.enraged and st.mode == "normal" and vz.m3.bound_tick ~= v.tick and not webbed and not chain_on then
        -- raid seam53: the derived reading (vz.sm_ball), not a third scan
        local ballinfo = vz.sm_ball
        if ballinfo == nil and cyc.share ~= nil and cyc.share.until_tick ~= nil and v.tick <= cyc.share.until_tick then
            -- (landed: hold beside the target through the hops)
            ballinfo = { x = cyc.share.x, z = cyc.share.z, left = 0 }
        end
        ring_state = QD.raid._verzik_ring(st, v, { reach = reach, intent = intent, mates = mates,
            pool = pool, on_pool = on_pool, ball = ballinfo, next_attack = m3_next })
        vz.m3.ring_log = vz.m3.ring_log or {}
        if #vz.m3.ring_log < 200 then
            local tl = {}
            for slot, e in pairs(vz.tor or {}) do tl[#tl + 1] = (slot % 10) .. ":" .. e.x .. "," .. e.z end
            vz.m3.ring_log[#vz.m3.ring_log + 1] = v.tick .. ring_state:sub(1, 2) .. "@" .. me.x .. "," .. me.z
                .. (intent.walk and (">" .. intent.walk.x .. "," .. intent.walk.z) or "") .. "[" .. table.concat(tl, ";") .. "]"
        end
    end
    -- not while a pool charge is on (she is invulnerable and does not attack)
    -- nor while the ball chain runs (the ball IS her attack): her clock may
    -- still name a tick, and the overlay would pull the tank off its pool or
    -- out of the ball's line
    if melee and st.mode == "normal" and QD.raid._verzik_web_run({ st = st, v = v, intent = intent, ok = okp,
            reach = reach, tor = vz.tor }) then
        -- her web spin (verzik_web): round her on the ring, a swing when one
        -- is free -- the tank too (owner 2026-10-07: "The tank step under
        -- does not need to happen when verzik is spewing webs as verzik is
        -- doing nothing else while spewing webs")
        share = nil
    elseif melee and tank and pool == nil and (vz.bm == nil or vz.bm.done)
        and QD.raid._verzik_tank_run({ st = st, v = v, intent = intent, ok = okp,
            next_attack = m3_next, reach = reach }) then
        -- the tank's step under (verzik_tank): over every other state, the ring's too
        share = nil
        vz.tank_owned = v.tick
    elseif melee and QD.raid._verzik_ball_run({ st = st, v = v, intent = intent, ok = okp, tor = vz.tor,
            reach = reach }) then
        -- the green ball (verzik_ball), ahead of the enrage ring: the chain in
        -- orb order places each seat, and a stacked ring is a crowd
        share = nil
    elseif ring_state ~= nil then
        share = nil
    elseif pool ~= nil and QD.raid._verzik_pool_run({ st = st, v = v, intent = intent, ok = okp,
            pool = pool, tor = vz.tor }) then
        -- the timed pool (verzik_pool): it walked, orbited or held
    elseif melee and ((not (power_through and vz.enraged) and next(vz.tor or {}) ~= nil) or #v.crabs > 0)
        and QD.raid._verzik_avoid({ st = st, v = v,
            intent = intent, go = go, ok = okp, reach = (m3_hold and 2 or reach), mates = mates,
            tor = (power_through and vz.enraged) and {} or vz.tor,
            side = { x = hx3, z = hz3 },
            pool = (near_pool ~= nil and pool ~= nil and not on_pool) and { x = near_pool.x, z = near_pool.z,
                pull = ((vz.pool_pull or 10) >= 60) and 60 or 10 } or nil }) ~= "CLEAR" then
        -- AVOID X (_verzik_avoid): the tornadoes and the nylocas through one
        -- field and one machine; it moved me, or chose to stand and swing
        vz.m3.avoids = (vz.m3.avoids or 0) + 1
    elseif tor ~= nil and td <= P.tornado_run and (st.mode ~= "normal" or ball or v.hp > P.enrage_hp_floor + 15) and not melee then
        -- raid seam34v, the Normal trio POWERS THROUGH: "Teams with
        -- sufficient experience can simply power through into enrage;
        -- they can either keep their health low so the tornado heals
        -- little" (W:983; W:981 "keep health around 50-60 ... the tornado
        -- will only heal around 90").  It keeps shooting at the band and
        -- runs only with the green ball in the air (a touch then halves
        -- what the ball needs) or above the band.  s34v survey k: three
        -- raiders running from three tornadoes shot so little that the
        -- enrage lasted 247 ticks on _play_verzik (17 touches all the same).
        -- run from it: it walks one tile a tick from her to its raider and
        -- hits on arrival (s30 vz30h: spawned on her, on me 5 ticks later,
        -- twelve times, 247 taken and triple that healed); running is two
        -- tiles a tick, so the raider gains a tile each tick it runs and
        -- shoots once it is far (P.tornado_run).  Away from the walls on a
        -- tie, so the run does not end in a corner.
        local F = P.floor
        local best, bx, bz = nil, me.x, me.z
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if ok(x, z) and not v.shadows[x * 100000 + z] then
                    -- the distance after ITS step toward the tile (it moves
                    -- first: ET 1.1, npcs before players)
                    local nx, nz = tor.x, tor.z
                    if x > nx then nx = nx + 1 elseif x < nx then nx = nx - 1 end
                    if z > nz then nz = nz + 1 elseif z < nz then nz = nz - 1 end
                    local d = math.max(math.abs(x - nx), math.abs(z - nz))
                    local wall = math.min(x - (O.x + F[1]), (O.x + F[3]) - x, z - (O.z + F[2]), (O.z + F[4]) - z, 3)
                    -- (s30 survey2 sva: a pure run ended in the 6422,95 corner and
                    -- the tornado landed 12 of 12; the wall weighs as much as a tile)
                    -- and keep going the way it went (s31 vz31e: the run turned
                    -- back into the 6422,79 corner and was touched 11 times; an
                    -- offline chase of the same rule, 20x20 floor, one tile a tick
                    -- after two, was touched 0 times in 600 ticks with the carry)
                    local lr = vz.last_run or { 0, 0 }
                    local score = d * 10 + wall * 10 + (dx * lr[1] + dz * lr[2]) * 2 - crowd(x, z) * 15
                    -- (and not into her reach: she steps toward her target first)
                    if st.mode == "normal" then
                        score = score - math.max(0, 3 - QD.raid._verzik_dist(x, z, b)) * 12
                    end
                    -- (and within reach of its pool while the yellows charge)
                    if near_pool ~= nil then
                        score = score - math.max(0, math.max(math.abs(x - near_pool.x), math.abs(z - near_pool.z)) - 4) * 12
                    end
                    if (dx ~= 0 or dz ~= 0) and (best == nil or score > best) then best, bx, bz = score, x, z end
                end
            end
        end
        if bx ~= me.x or bz ~= me.z then
            intent.walk = { x = bx, z = bz }
            vz.steps = vz.steps + 1
            vz.last_run = { bx - me.x, bz - me.z }
        end
        vz.tornado_runs = (vz.tornado_runs or 0) + 1
    elseif melee and not on_me and (d_boss > reach + 1 or d_boss == 0 or (halb and d_boss < 2)
            or math.max(math.abs(me.x - hx3), math.abs(me.z - hz3)) > 4) and okp(hx3, hz3) then
        -- raid seam45: the melee trio's side of her is the NORTH-EAST:
        -- the tornadoes rise on her south-west tile (tob_verzik.rs2
        -- ~tob_verzik_spawn_tornadoes: npc_add at her npc_coord) and
        -- every step from it toward a raider north and east of that tile
        -- is into her body, which walls it (s45 e8 sva t960: its first
        -- step went round her corner, never through her), so a raider on
        -- her east edge is never reached (W:981 "in the off chance it
        -- damages the player").  p1 to p3 on her east edge two apart
        -- (the green ball bounces to a neighbour, W:975), stepping out to
        -- the east on T-1.
        -- ROUND her, two out: the server paths a player THROUGH her
        -- body (s45 e9 sva t558-600: a walk from 6430,87 to her east
        -- edge stood the leader on 6432,89 inside her 7x7 every other
        -- tick, the floor rule walked him out, 43 ticks with no swing)
        local wx, wz = hx3, hz3
        if hx3 >= b.x + n3 and me.x < b.x + n3 then
            if me.z < b.z then
                wx, wz = b.x + n3 + 1, math.min(me.z, b.z - 2)
            elseif me.z >= b.z + n3 then
                wx, wz = b.x + n3 + 1, math.max(me.z, b.z + n3 + 1)
            elseif (me.z - b.z) < (b.z + n3 - 1 - me.z) then
                wx, wz = me.x, b.z - 2
            else
                wx, wz = me.x, b.z + n3 + 1
            end
            if not okp(wx, wz) then wx, wz = hx3, hz3 end
        end
        if st.walk_target == nil or st.walk_target.x ~= wx or st.walk_target.z ~= wz or (st.last_me ~= nil and st.last_me.x == me.x and st.last_me.z == me.z) then
            intent.walk = { x = wx, z = wz }
            vz.steps = vz.steps + 1
            vz.target_slot = nil
        end
    elseif on_me or (not melee and d_boss < 2) or (st.mode == "normal" and not (melee and okp or ok)(me.x, me.z)) then
        -- (raid seam34v: and back onto the floor: s34v _play_verzik
        -- t600-625, the leader stood on 6421,84, a column west of the
        -- floor, pressed Attack every tick and never swung)
        go(me.x, me.z)
    elseif st.mode == "normal" and melee and not tank and st.party > 1 and d_boss == 1 and not ball and (function()
            -- (the higher pid of the two steps, so they do not step together)
            for _, m in ipairs(mates) do if m.x == me.x and m.z == me.z and (m.pid == nil or st.my_pid == nil or m.pid < st.my_pid) then return true end end
            return false
        end)() then
        -- raid seam52: a member beside her shares no tile with a mate.
        -- Her webs are thrown one a raider and every web snaps on every
        -- raider on its tile (tob_verzik.rs2 ~tob_verzik_webs,
        -- [ai_timer,verzik_web_npc]): e1 _play_verzik, both members
        -- pathed to the same tile of her east edge, took both webs and
        -- died at t520.  Blert's trios keep apart (seam52 refspread.py:
        -- 95% of reds-phase ticks no other raider within one tile).  One
        -- step along her edge to a tile beside her that no mate holds.
        local best, bx, bz = nil, nil, nil
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if (dx ~= 0 or dz ~= 0) and okp(x, z) and not v.shadows[x * 100000 + z] and QD.raid._verzik_dist(x, z, b) == 1 then
                    local sc = math.max(math.abs(dx), math.abs(dz)) * 10 + crowd(x, z) * 5
                    for _, m in ipairs(mates) do if m.x == x and m.z == z then sc = sc + 1000 end end
                    if best == nil or sc < best then best, bx, bz = sc, x, z end
                end
            end
        end
        if best ~= nil and best < 1000 then
            intent.walk = { x = bx, z = bz }
            vz.steps = vz.steps + 1
            vz.target_slot = nil
            vz.m3.apart = (vz.m3.apart or 0) + 1
        end
    elseif st.party > 1 and crowd(me.x, me.z) > 0 and ((not melee) or (cyc.split_until ~= nil and v.tick <= cyc.split_until)) then
        -- raid seam34v: a raider beside another steps apart (the ball
        -- bounces to a neighbour, W:975; s34v vzn3: the two members ran
        -- one tile apart and took the ball and both tornadoes together);
        -- one step, to the free tile nearest, never toward her (s34v vzn4:
        -- a side kept relative to her walking body dithered every tick
        -- beside her and took her melee)
        local best, bx, bz = nil, me.x, me.z
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if (dx ~= 0 or dz ~= 0) and ok(x, z) and not v.shadows[x * 100000 + z] and crowd(x, z) == 0 then
                    local sc = math.max(math.abs(dx), math.abs(dz)) * 10 - QD.raid._verzik_dist(x, z, b)
                    if best == nil or sc < best then best, bx, bz = sc, x, z end
                end
            end
        end
        if best ~= nil then go(bx, bz) vz.spreads = (vz.spreads or 0) + 1 end
    end
    -- raid seam52: bound, a walk goes nowhere: e3 _play_verzik, both
    -- members re-sent the walk to her north-east side every tick of the
    -- webs (she walks to the centre) and swung at nothing t489-507
    if st.mode == "normal" and melee and vz.m3.bound_tick == v.tick then
        intent.walk = nil
        vz.m3.bound_walks = (vz.m3.bound_walks or 0) + 1
    end
    if intent.walk == nil and pool == nil and share == nil and ring_state == nil then crab = QD.raid._verzik_crabs(st, v, ok, go) end
    -- raid seam45: melee never swings at a nylocas (its death blasts
    -- everyone within 3, ~tob_verzik_crab_blast); it is only run from
    if melee then crab = nil end
    -- raid seam45: the step out of her reach on the plan's T-1 (ET 1.1; V
    -- p3_melee_predicate), and no press on a held tick
    -- (owner_verzik: and never off a pool -- the yellows suspend her
    -- clock, tob_verzik.rs2 ^tob_var_vz_suspend, so no attack comes while
    -- they charge; _play_verzik_slow_p3 P3+163: the tank stepped off its
    -- pool beside her body the tick before the blast)
    -- raid seam35e play_tob_entry_relay: no attack press from the pool in
    -- the blast's last ticks.  The bow's press paths to its own range and
    -- sight line, and from the pool at 6428,207 that path was 6428,209: on
    -- every swing the raider stepped off and the plan walked him back, and
    -- the blast (judged on the tile of T-1, ET 1.1) found him off it (the
    -- relay's own name, P3, 44 taken at 44 hitpoints).  Standing still on
    -- it for the window keeps him on it; outside the window nothing changes.
    local blast_close = pool ~= nil and on_pool and vz.pool_first ~= nil and v.tick >= vz.pool_first + P.pool_life - 4
    if ring_state ~= nil then
        -- (the ring set the press; its SWING dumps the claws, W:992)
        if ring_state == "SWING" then
            if vz.target_slot ~= nil then vz.target_slot = nil end
            if not tor_near or power_through then QD.raid._verzik_spec_dump(st, v, intent, events) end
        end
    elseif share ~= nil or vz.ball_lock == v.tick then
        -- (the shared ball: no press, it paths off the corner; verzik_ball
        -- locked this tick -- moving, or on the rally out of her reach)
        intent.attack = false
    elseif intent.walk == nil and blast_close then
        intent.attack = false
    elseif intent.walk == nil then
        if webbed then
            if vz.target_slot ~= webbed.row.slot then QD.raid._verzik_press_add(st, v, webbed) end
        elseif crab ~= nil then
            local idle = v.tick - math.max(st.last_swing, st.engaged_tick) > st.weapon.speed + 2
            if vz.target_slot ~= crab.row.slot or not st.engaged or idle then QD.raid._verzik_press_add(st, v, crab) end
        elseif st.mode == "normal" and pool ~= nil then
            -- raid seam34v: no press while the yellows charge: "Verzik is
            -- invulnerable while charging this attack" (W:968), and a press
            -- paths the raider off its pool (s34v _play_verzik t578: the
            -- leader's swing moved it 6422,79 -> 6423,81 the tick before
            -- the blast, which took its last 51)
            intent.attack = false
        elseif melee and m3_hold then
            intent.attack = false
        elseif melee and vz.m3.tor_hold == v.tick then
            intent.attack = false
        else
            if vz.target_slot ~= nil then vz.target_slot = nil end
            intent.attack = true
            -- (owner_verzik: the slow pace too, once she is enraged -- its
            -- rotation is done by then, the ball before the enrage -- "At
            -- this stage, players should dump all melee special attacks to
            -- end the phase as fast as possible" (W:992); Blert 0f9abe1a's
            -- slow team spent claws, burning claws and crystal halberd specials
            -- in P3; with walking tornadoes an undumped enrage healed her
            -- 987-2,700 and never ended)
            -- (enraged only: Blert's trios spent 38 of their 96 P3 specials
            -- before her enrage, but the dump before it cost this plan
            -- raiders -- s_fastp3c 2026-10-07, three deaths in five names)
            -- (enraged only: a dump from the start of P3 on the fast pace
            -- -- Blert's fast trios spend 38 of their 96 P3 specials before
            -- the enrage -- cost this plan raiders twice, s_fastp3c and
            -- s_fastp3g 2026-10-07; open)
            -- (with a tornado near too when powered through: held back, the
            -- orb regenerated past 500, the claws stayed on waiting for a
            -- special that never fired, and the seat swung claw autos for the
            -- rest of the enrage -- svavzslow 60d10a2d0+, dragon_claws from
            -- t740 to the end)
            if melee and vz.enraged and (not tor_near or power_through) then QD.raid._verzik_spec_dump(st, v, intent, events) end
        end
    end
    -- last, over whatever the state decided: never stand where a tornado steps
    -- THE CLAWS GO BACK whatever owned the tick: the dump only ran on a tick
    -- with no tornado near, which in the enrage is almost never, so a seat
    -- that had dumped its specs swung claws for the rest of the fight (_vzslow
    -- 8374a3f60: 6-10 claw autos a seat in a 242-tick enrage, one scythe).
    if melee and vz.held == "claws" and (intent.gear == nil or #intent.gear == 0) then
        local _, en = QD.var.varp("varp300_sa_energy")
        if (tonumber(en) or 0) < QD.RAID_PLAY_VERZIK_CLAW_COST then
            local back = vz.p3_main or vz.main or "scythe"
            intent.gear = { QD.RAID_PLAY_VERZIK_WEAPONS[back].item }
            vz.held = back
            st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS[back]
            QD.raid._verzik_engage(st, v, "rearmed")
            vz.claws_back = (vz.claws_back or 0) + 1
        end
    end
    if melee then QD.raid._verzik_tornado_guard(st, v, intent, okp) end
    -- POWERED THROUGH, NOT WALKED INTO: an attack press out of reach walks me
    -- back to her by the server's path, and with my tornado within three that
    -- path was its tile -- the guard's dodge, then the press straight back
    -- (_vzslow on ae7ca6e50+: ~1700 healed from P3+250, ~11 touches).  So
    -- with a tornado near, the press is sent only from a tile already in
    -- reach (it does not move me); else I walk to the in-reach tile my
    -- tornado can be on last.
    if power_through and vz.enraged and melee and intent.attack and b ~= nil then
        local tors = {}
        for _, tr in ipairs(v.tornadoes or {}) do
            if math.max(math.abs(tr.x - me.x), math.abs(tr.z - me.z)) <= 3 then tors[#tors + 1] = { x = tr.x, z = tr.z } end
        end
        local dme = QD.raid._verzik_dist(me.x, me.z, b)
        if #tors > 0 and (dme < 1 or dme > reach) then
            local best, bx, bz = nil, nil, nil
            for dx = -2, 2 do
                for dz = -2, 2 do
                    local x, z = me.x + dx, me.z + dz
                    local db = QD.raid._verzik_dist(x, z, b)
                    if db >= 1 and db <= reach and okp(x, z) and not v.shadows[x * 100000 + z]
                        and not tor_reaches(tors, me, me, x, z) then
                        local far = 99
                        for _, tr in ipairs(tors) do far = math.min(far, math.max(math.abs(tr.x - x), math.abs(tr.z - z))) end
                        local sc = math.max(math.abs(dx), math.abs(dz)) * 2 - far * 3
                        if best == nil or sc < best then best, bx, bz = sc, x, z end
                    end
                end
            end
            intent.attack = false
            if bx ~= nil then intent.walk = { x = bx, z = bz } end
            vz.enrage_closes = (vz.enrage_closes or 0) + 1
        end
    end
    -- NEVER ONTO OR ACROSS A LIVE WEB, whatever decided the walk: moving off a
    -- web tile binds for 10 ticks (Near Reality VerzikViturRoom.processMovement,
    -- content f4b4d64ea0), mid-run too, and a bound raider is a tornado's
    -- (svbvzslow t752-762: a web spawned on 6437,90, two seats walked onto
    -- it, stood bound and were touched -- heal 3x the hit).  A run of two
    -- tiles passes its first-step tile (diagonal first), so that one counts.
    if intent.walk ~= nil and #v.webs > 0 then
        local web = {}
        for _, w in ipairs(v.webs) do web[w.row.x * 100000 + w.row.z] = true end
        local wx, wz = intent.walk.x, intent.walk.z
        local function sgn(a) return a > 0 and 1 or (a < 0 and -1 or 0) end
        local mx, mz = me.x + sgn(wx - me.x), me.z + sgn(wz - me.z)
        local far = math.max(math.abs(wx - me.x), math.abs(wz - me.z)) > 1
        if web[wx * 100000 + wz] or (far and web[mx * 100000 + mz]) then
            local best, bx, bz = nil, nil, nil
            for dx = -1, 1 do
                for dz = -1, 1 do
                    local x, z = me.x + dx, me.z + dz
                    if (dx ~= 0 or dz ~= 0) and not web[x * 100000 + z] and okp(x, z)
                        and (b == nil or QD.raid._verzik_dist(x, z, b) >= 1) then
                        local sc = math.max(math.abs(x - wx), math.abs(z - wz))
                        if best == nil or sc < best then best, bx, bz = sc, x, z end
                    end
                end
            end
            intent.walk = (bx ~= nil) and { x = bx, z = bz } or nil
            vz.web_detours = (vz.web_detours or 0) + 1
        end
    end
    if QD.raid._verzik_slow_hold(st, v) then
        -- an engaged raider swings on by itself: ask the executor to stop it
        -- (raider_engage: a step onto my own tile, sent only while ENGAGED)
        intent.stop = true
        intent.attack = false
        intent.spec = nil
        vz.slow_holds = (vz.slow_holds or 0) + 1
    end
    c.threat = threat
end

-- ==========================================================================
-- THE VERZIK PLAN'S DECIDE (PLAY_NOTES.md "Verzik").
-- ==========================================================================
function QD.raid._play_verzik_decide(st, v)
    local P, N, O = st.plan, st.numbers, st.origin
    if st.vz == nil then
        st.vz = { ids = QD.raid._verzik_ids(st), forms = {}, attacks = {}, swaps = {}, specs = {}, add_presses = 0,
            held = "fists", n = { fists = 0, bow_accurate = 0, dawnbringer = 0, bow_rapid = 0 }, seen_swings = 0,
            windups = {}, W = nil, hid = false, hide_from = nil, dying = false, dying_tick = nil,
            loadout = false, rapid = nil, reds_tick = nil, p3_style = "protectfrommissiles", ball_until = -1,
            pool = nil, dodges = {}, steps = 0, target_slot = nil, last_spec = -1000, enraged = false }
        st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS.fists
        -- raid seam34v: the Normal trio starts with the scythe on (the harness wields it)
        if st.mode == "normal" then
            -- owner_verzik: the main melee weapon by pace and role (the header's table)
            st.vz.pace = QD.raid.verzik_pace or "fast"
            -- the engagement is the executor's machine here (raider_engage):
            -- this plan no longer writes st.engaged
            st.engage_owned = true
            st.vz.main = "scythe"
            st.vz.p3_main = (st.vz.pace == "slow" and QD.RAID_PLAY_VERZIK_HALBERD_ROLES[st.role]) and "halberd" or nil
            st.vz.held = st.vz.main
            st.vz.n.scythe = 0
            st.vz.n.halberd = 0
            st.vz.n.claws = 0
            st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS[st.vz.main]
        end
    end
    local vz = st.vz
    QD.raid._verzik_see(st, v)
    -- the swings the player saw itself make since last tick go to the weapon held
    if #st.swings > vz.seen_swings then
        vz.n[vz.held] = vz.n[vz.held] + (#st.swings - vz.seen_swings)
        vz.seen_swings = #st.swings
    end
    local intent = { want = {}, walk = nil, attack = false }
    local b = v.boss
    local phase = v.phase or vz.phase
    vz.phase = phase
    if b == nil then
        return intent
    end
    -- raid seam53: THIS TICK'S EVENTS, derived once (QD.raid._verzik_events)
    -- and handed to every machine the plan runs, so they all read one tick
    local events = QD.raid.sm_events(st, v, QD.raid._verzik_events)
    -- raid seam53 hierarchy: the weapon invariant is a DECLARED machine
    -- (verzik_weapon, TOP LEVEL because no phase owns it) and the sword's end
    -- is p1's `exit` hook.  The hand-rolled guard that used to sit here is
    -- gone: it ran on the first tick PAST the boundary, where the hook runs AT
    -- it.  See the layer's header (raid_sm.lua:52-101) for the three homes and
    -- why this one is not nested.  It runs HERE, after the tick's events are
    -- derived and before her phase is stepped, because every machine reads one
    -- shared derivation and this one must be able to re-arm the hand before
    -- the phase decides what to press with it.
    QD.raid.sm_run(st, v, "verzik_weapon",
        { st = st, v = v, vz = vz, intent = intent, phase = phase }, events)
    local ok = QD.raid._verzik_floor(st, v)
    -- raid seam53: THE FLOOR TEST, through a holder.  P2 and P3 NARROW `ok`
    -- to exclude her footprint's ring, and the walk helper `go` below must
    -- see that narrowing -- it used to, because `ok` and `go` were two locals
    -- of this one function and the narrowing rebound the upvalue `go` had
    -- captured.  Now that each phase is its own handler, its `ok` is its own
    -- local, so the narrowing is written back here (c.okref.fn) and `go`
    -- reads the holder rather than capturing the function.
    local okref = { fn = ok }
    local me = v.me
    local d_boss = QD.raid._verzik_dist(me.x, me.z, b)
    local function go(tx, tz)
        local sx, sz = QD.raid._play_hazard(st, v, tx, tz, okref.fn)
        sx, sz = QD.raid._play_safe_step(st, v, sx, sz, okref.fn)
        local same = st.walk_target ~= nil and st.walk_target.x == sx and st.walk_target.z == sz
        local stuck = st.last_me ~= nil and st.last_me.x == me.x and st.last_me.z == me.z
        if (me.x ~= sx or me.z ~= sz) and (not same or stuck) then
            intent.walk = { x = sx, z = sz }
            vz.steps = vz.steps + 1
        end
        return sx, sz
    end
    local function swap_to(key, drinks)
        local w = QD.RAID_PLAY_VERZIK_WEAPONS[key]
        local items = {}
        if w.item ~= nil and (vz.held == "fists" or QD.RAID_PLAY_VERZIK_WEAPONS[vz.held].item ~= w.item) then items[1] = w.item end
        QD.raid._verzik_block(st, v, "wield " .. key, items, drinks or {})
        vz.held = key
        st.weapon = w
        QD.raid._verzik_engage(st, v, "rearmed")
        vz.target_slot = nil
    end
    local threat = function(h) return 0 end
    local on_me = v.shadows[me.x * 100000 + me.z] == true
    -- the add to shoot first, nearest (P2 and P3: W:925 the nylocas "home in
    -- on the player and self-destruct"; W:931 "focus on the Matomenos"; W:927
    -- the Athanatos heals her "every few ticks")
    local function nearest(list)
        local best, bd = nil, 999
        for _, a in ipairs(list) do
            local d = math.max(math.abs(a.row.x - me.x), math.abs(a.row.z - me.z))
            if d < bd then best, bd = a, d end
        end
        return best, bd
    end

    -- raid seam53: HER PHASE, as the declared machine above.  The form the
    -- library read is the `form_change` event, raised before `tick`, so the
    -- tick her form changes is decided by the state she has just entered --
    -- as the old chain did, reading `phase` before it branched.
    local pc = { st = st, v = v, intent = intent, events = events,
        P = P, N = N, O = O, vz = vz, b = b, me = me, phase = phase,
        ok = ok, okref = okref, d_boss = d_boss, on_me = on_me, go = go, swap_to = swap_to,
        nearest = nearest, threat = threat }
    QD.raid.sm_run(st, v, "verzik_phase", pc, events)
    threat = pc.threat
    if pc.bail then return intent end

    -- raid seam45: P3's ticks as the plan read them, from her first web
    -- special for 20 ticks (the ledger's play.melee_clock row)
    if phase == "p3" and st.mode == "normal" and vz.m3 ~= nil then
        if v.attack ~= nil and v.attack.seq == P.p3_webs and vz.m3.w0 == nil then vz.m3.w0 = v.tick end
        vz.m3.log2 = vz.m3.log2 or {}
        if vz.m3.w0 ~= nil and v.tick <= vz.m3.w0 + 20 then
            vz.m3.log2[#vz.m3.log2 + 1] = v.tick .. "B" .. b.x .. "," .. b.z .. "s" .. tostring(b.size) .. "me" .. v.me.x .. "," .. v.me.z .. "N" .. tostring(vz.m3_N) .. (vz.m3_sure and "s" or "u") .. "d" .. QD.raid._verzik_dist(v.me.x, v.me.z, b)
                .. (v.attack and ("a" .. v.attack.seq) or "") .. (st.engaged and "E" or "") .. (intent.walk and ("W" .. intent.walk.x .. "," .. intent.walk.z) or "") .. (intent.attack and "A" or "")
        end
    end
    if intent.want.protectfrommagic then P.walk_prayers[1] = "protectfrommagic"
    elseif intent.want.protectfrommissiles then P.walk_prayers[1] = "protectfrommissiles" end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    -- raid seam45: THE MELEE STATS.  Every brew dose drains Attack and
    -- Strength (s45 e15 svc: the leader drank 16 brew doses and 12 restore
    -- doses; in the enrage 43 of 48 splats on her were zeros, 14 damage in a
    -- hundred ticks).  A melee raider drinks a super restore when its Attack
    -- is drained and the super combat potion when it has fallen back to its
    -- base (W:871 Verzik's recommended setup carries both).
    if st.mode == "normal" and (phase == "p2" or phase == "p3" or phase == "t23") and intent.drink == nil
        and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY then
        local _, att = QD.skill.read("attack")
        local level, base = att.level or 99, att.base or att.base_level or 99
        local pick = nil
        if level < base - 8 then
            for _, name in ipairs(QD.RAID_PLAY_RESTORES) do
                local cr, n = QD.inv.count(name)
                if pick == nil and cr == "ok" and n > 0 then pick = name end
            end
        elseif level < base + 8 then
            for _, name in ipairs({ "br_1dose2combat", "br_2dose2combat", "br_3dose2combat", "br_4dose2combat" }) do
                local cr, n = QD.inv.count(name)
                if pick == nil and cr == "ok" and n > 0 then pick = name end
            end
        end
        if pick ~= nil then
            intent.drink = pick
            vz.stat_drinks = (vz.stat_drinks or 0) + 1
        end
    end
    -- owner_verzik: no bite or sip while a tornado is within four, unless the
    -- hitpoints are below what one more touch and her auto take: a block of
    -- inputs holds the next decide for its confirmation, and a decide missed
    -- is a tile the tornado gains (an offline chase of the dodge: 0 touches
    -- in 300 ticks deciding every tick, 7 with one decide in four lost)
    if phase == "p3" and vz.tor ~= nil then
        local close = false
        for _, e in pairs(vz.tor) do
            if math.max(math.abs(e.x - v.me.x), math.abs(e.z - v.me.z)) <= 4 then close = true end
        end
        -- (above her melee only: _play_verzik_slow_p3 P3+514, the tank held off
        -- eating at 61 with a tornado close and her melee took the 61)
        if close and v.hp > N.melee + 7 then
            -- (not the gear: a swap dropped here left vz.held saying the main
            -- weapon was back while the server still held the claws -- every
            -- enrage seat swung claw autos to the end, svavzslow 60d10a2d0+,
            -- "C790scythe" in its notes with dragon_claws worn)
            intent.eat, intent.drink = nil, nil
            vz.tor_held_supplies = (vz.tor_held_supplies or 0) + 1
            -- (and none of the library's own sips: its brew recovery fills a
            -- free potion tick, raid_play.lua _play_send; a sip just "taken"
            -- holds it this tick)
            st.last_drink = math.max(st.last_drink, v.tick - QD.RAID_PLAY_DRINK_DELAY + 1)
        end
        -- THE STEP, SENT BARE: a walk alone, with a tornado about, goes out
        -- without the together block's confirmation wait (raid_play.lua
        -- _play_send -> QD.together awaits the tile change for up to
        -- QD.TOGETHER_CONFIRM_TICKS), so the next tick is decided on time
        local near6 = false
        for _, e in pairs(vz.tor) do
            if math.max(math.abs(e.x - v.me.x), math.abs(e.z - v.me.z)) <= 6 then near6 = true end
        end
        if near6 and intent.walk ~= nil and intent.eat == nil and intent.drink == nil and (intent.gear == nil or #intent.gear == 0) then
            local mr = api_drive.move_to(intent.walk.x, intent.walk.z)
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
            vz.bare_steps = (vz.bare_steps or 0) + 1
            if mr == "ok" then
                QD.raid._verzik_engage(st, v, "stepped")
                st.walk_target = intent.walk
                intent.walk = nil
            end
        end
    end
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    vz.prev_me = { x = v.me.x, z = v.me.z }
    if vz.enraged then
        vz.dec = vz.dec or { n = 0, first = v.tick, gaps = 0, last = v.tick }
        vz.dec.n = vz.dec.n + 1
        if v.tick - vz.dec.last > 1 then vz.dec.gaps = vz.dec.gaps + (v.tick - vz.dec.last - 1) end
        vz.dec.last = v.tick
    end
    return intent
end

-- ==========================================================================
-- owner_verzik 2026-10-07: THE TRIO HARNESS'S SHARED HALF.  The Normal trio
-- room as test/raids/_vzslow.lua, _vzslowp3.lua, _vzfastp3.lua and _vzdeath.lua play it (renamed
-- 2026-10-07 from _play_verzik_slow*, _play_verzik_p3, _play_verzik_death: run names must differ in
-- their first nine characters, run.py / seed_survey.py)
-- (one copy here, not one per harness file: the run.py wrapper embeds each
-- test file whole and gives it no include).  cfg:
--   pace   "slow" | "fast"   (QD.raid.verzik_pace, the plan header's table)
--   start  "room" | "p3"     (p3: the leader spends P1 and P2 with
--                             ::tobvzskip -- tob_selftest.rs2 [debugproc,
--                             tobvzskip], npc_damage of the phase's hitpoints
--                             left -- so her own transitions run and P3 starts
--                             as it does after a P2: her form, her hitpoints,
--                             the raiders' kit as worn; nobody fights before)
--   cycle  true: the four specials of one rotation are CHECKED rows (the
--          slow pace); false: reported, and the fast pace checks instead
--          that she died before her green ball
-- ==========================================================================

-- The rotation read off the leader's tick log after the fight: every special
-- she threw from `p3s` on and what the raiders did about it.  Returns rows.
function QD.raid._verzik_cycle_read(t, p3s, death_tick)
    local K = {}
    for _, kind in ipairs({ "npc_anim", "npc_spawn", "npc_free", "npc_death", "projectile", "hit_player", "player_spotanim" }) do
        local _, rows = t.ticklog.rows({ kind = kind })
        K[kind] = rows or {}
        t.ticks(1)
    end
    local tile = {}
    local _, ptiles = t.ticklog.rows({ kind = "player_tile" })
    for i, r in ipairs(ptiles or {}) do
        if r.tick >= p3s then
            tile[r.tick] = tile[r.tick] or {}
            tile[r.tick][r.pid] = r
        end
        if i % 1500 == 0 then t.ticks(1) end
    end
    local function at(pid, tick)
        for k = tick, tick - 3, -1 do
            if tile[k] ~= nil and tile[k][pid] ~= nil then return tile[k][pid] end
        end
        return nil
    end
    local C = { crabs = {}, webs = {}, yellows = {}, ball = {}, order = {} }
    for _, r in ipairs(K.npc_spawn) do
        if r.type == 8386 and r.tick >= p3s and C.enrage_at == nil then C.enrage_at = r.tick end
    end
    local end_tick = death_tick or 1e9
    for _, r in ipairs(K.npc_anim) do
        if r.type == 8374 and r.tick >= p3s and r.tick <= end_tick then
            if r.seq == 14406 then C.crabs[#C.crabs + 1] = { tick = r.tick } C.order[#C.order + 1] = { "crabs", r.tick } end
            if r.seq == 8127 then C.webs[#C.webs + 1] = { tick = r.tick } C.order[#C.order + 1] = { "webs", r.tick } end
            if r.seq == 8126 then C.yellows[#C.yellows + 1] = { tick = r.tick } C.order[#C.order + 1] = { "yellows", r.tick } end
        end
    end
    -- a THROW, not a hop: since content 552c599394 every bounce is its own
    -- 1598 from the raider it landed on, 6-8 ticks after the last, so a 1598
    -- within QD.RAID_PLAY_VERZIK_BALL_HOP_GAP of the one before is the same
    -- ball (her next throw is a whole rotation later)
    local last_ball = nil
    for _, p in ipairs(K.projectile) do
        if p.spotanim == 1598 and p.tick >= p3s and p.tick <= end_tick then
            if last_ball == nil or p.tick - last_ball > QD.RAID_PLAY_VERZIK_BALL_HOP_GAP then
                C.ball[#C.ball + 1] = { tick = p.tick }
                C.order[#C.order + 1] = { "ball", p.tick }
            end
            last_ball = p.tick
        end
    end
    table.sort(C.order, function(a, b) return a[2] < b[2] end)
    -- the nylocas of each crabs special: spawned with it, gone when (free),
    -- and their blasts on raiders (tob_verzik.rs2 ~tob_verzik_crab_blast)
    for _, c in ipairs(C.crabs) do
        local slots, n, gone, blast = {}, 0, 0, 0
        for _, s in ipairs(K.npc_spawn) do
            if s.type >= 8381 and s.type <= 8383 and s.tick >= c.tick and s.tick <= c.tick + 3 then slots[s.slot] = true n = n + 1 end
        end
        local last = c.tick
        for _, f in ipairs(K.npc_free) do
            if slots[f.slot] and f.tick >= c.tick then gone = gone + 1 slots[f.slot] = false last = math.max(last, f.tick) end
        end
        for _, h in ipairs(K.hit_player) do
            if h.npc_type ~= nil and h.npc_type >= 8381 and h.npc_type <= 8383 and h.tick >= c.tick and h.tick <= c.tick + 60 then blast = blast + (h.damage or 0) end
        end
        c.n, c.gone, c.last, c.blast = n, gone, last, blast
    end
    -- the webs: thrown (1601), landed (npc 8376 on a tile), on a raider's tile
    -- when it landed (bound: tob_verzik.rs2 [queue,tob_verzik_web_land]), and
    -- the snaps of an unbroken web (hit_player from 8376, [ai_timer,verzik_web_npc])
    for _, w in ipairs(C.webs) do
        local thrown, landed, bound, snaps, snap_dmg = 0, 0, 0, 0, 0
        for _, p in ipairs(K.projectile) do
            if p.spotanim == 1601 and p.tick >= w.tick and p.tick <= w.tick + 35 then thrown = thrown + 1 end
        end
        for _, s in ipairs(K.npc_spawn) do
            if s.type == 8376 and s.tick >= w.tick and s.tick <= w.tick + 40 then
                landed = landed + 1
                local x, z = QD.ticklog._unpack(s.coord)
                for pid = 0, 7 do
                    local r = at(pid, s.tick - 1)
                    if r ~= nil and r.x == x and r.z == z then bound = bound + 1 end
                end
            end
        end
        for _, h in ipairs(K.hit_player) do
            if h.npc_type == 8376 and h.tick >= w.tick and h.tick <= w.tick + 70 then snaps = snaps + 1 snap_dmg = snap_dmg + (h.damage or 0) end
        end
        w.thrown, w.landed, w.bound, w.snaps, w.snap_dmg = thrown, landed, bound, snaps, snap_dmg
    end
    -- the yellows: the blast tick (1596 on each raider), who it found
    -- protected (1597, tob_verzik.rs2 ~tob_verzik_powerblast), and the tiles
    -- they stood on (one raider a pool, W:975)
    for _, y in ipairs(C.yellows) do
        local blast = nil
        for _, s in ipairs(K.player_spotanim) do
            if s.spotanim == 1596 and s.tick > y.tick and blast == nil then blast = s.tick end
        end
        local hit, safe, tiles, distinct = {}, {}, {}, true
        if blast ~= nil then
            for _, s in ipairs(K.player_spotanim) do
                if s.tick == blast and s.spotanim == 1596 then hit[#hit + 1] = s.pid end
                if s.tick == blast and s.spotanim == 1597 then safe[s.pid] = true end
            end
            local seen = {}
            for _, pid in ipairs(hit) do
                local r = at(pid, blast - 1)
                if r ~= nil then
                    local key = r.x * 100000 + r.z
                    if seen[key] then distinct = false end
                    seen[key] = true
                    tiles[#tiles + 1] = "p" .. pid .. "@" .. r.x .. "," .. r.z .. (safe[pid] and "" or "!")
                end
            end
        end
        local nsafe = 0
        for _, pid in ipairs(hit) do if safe[pid] then nsafe = nsafe + 1 end end
        y.blast, y.raiders, y.safe, y.distinct, y.tiles = blast, #hit, nsafe, distinct, table.concat(tiles, " ")
    end
    -- the green ball: every impact (1600 on a raider, one per hop:
    -- [queue,tob_verzik_ball_land]) and the hit each impact carried
    for _, bl in ipairs(C.ball) do
        local hops = {}
        for _, s in ipairs(K.player_spotanim) do
            if s.spotanim == 1600 and s.tick >= bl.tick and s.tick <= bl.tick + QD.RAID_PLAY_VERZIK_BALL_CHAIN_SPAN then
                local dmg = 0
                -- (the ball's own hit only: 75% of the Hitpoints level, 74 at
                -- 99 -- tob.constant ^tob_verzik_p3_ball_pct; her auto landing
                -- on the same tick is not the ball's: svcplayverzi P3+208, 15)
                for _, h in ipairs(K.hit_player) do
                    if h.pid == s.pid and h.tick >= s.tick and h.tick <= s.tick + 1 and h.npc_type == 8374 and (h.damage or 0) >= 70 and (h.damage or 0) > dmg then dmg = h.damage end
                end
                hops[#hops + 1] = { pid = s.pid, tick = s.tick, dmg = dmg }
            end
        end
        bl.hops = hops
    end
    return C
end

function QD.raid.verzik_trio_run(t, cfg)
    local role, size = t.party.role(), t.party.size()
    local mode = "normal"
    t.raid.verzik_pace = cfg.pace
    if role == 1 then
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("verzik.ticklog", tl_ok == "ok", tostring(tl_detail))
    end
    -- the reference team's raiders that wore no ring (cfg.unworn: the harness's kit note)
    for _, item in ipairs((cfg.unworn or {})[role] or {}) do
        local ur, ud = t.player.unequip(item)
        t.check("verzik.unworn", ur == "ok", "p" .. role .. " " .. item .. ": " .. tostring(ud))
    end
    local r, d = t.raid.enter("tob", "verzik", { mode = mode })
    t.check("verzik.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    -- W:871 "All players must have Protect from Magic on before starting the fight."
    t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)
    t.check("verzik.pace", true, "p" .. role .. " pace " .. tostring(cfg.pace) .. " start " .. tostring(cfg.start))
    if size > 1 then t.expect("party.barrier.ready", t.party.barrier("ready", 300)) end
    local M, boss_slot = nil, nil
    if role == 1 then
        -- only the leader starts the encounter, by talking to her
        local _, brow = t.npc.nearest("verzik_initial", 30)
        local tr, td = t.player.talk_to("verzik_initial", 1)
        t.check("verzik.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("verzik.begin", cr == "ok", tostring(cd))
        t.ticklog.mark("room start")
        local slot_res, bs = t.ticklog.slot(brow)
        boss_slot = bs
        t.check("verzik.slot", slot_res == "ok", "server slot " .. tostring(bs))
        local _, mk = t.ticklog.rows({ kind = "mark" })
        M = mk[#mk].tick
    end
    if size > 1 then t.expect("party.barrier.started", t.party.barrier("started", 900)) end
    -- owner_verzik 2026-10-07: THE FORCED DEATH (cfg.kill_role).  The owner:
    -- "Check what happens when a player dies in the raid, and ensure they are
    -- put in the observation until the end of the raid."  "Players who die
    -- during the raid will be placed in purgatory. If the room is successfully
    -- cleared by the remaining players, those who have died will be reunited
    -- with the rest of their team" (wiki_Theatre_of_Blood_Strategies.wikitext:3).
    -- That seat dies in P1 by ::die (player/death.rs2: the whole death sequence
    -- through the engine's trigger), then reads its own ::tobjail line every
    -- few ticks through P1, P2 and P3 to the room's end, with her form; it never
    -- plays.  Its rows: caged at every reading while the room is not won, the
    -- raid's death counter, out once the room is won.
    if cfg.kill_role ~= nil and role == cfg.kill_role then
        t.party.allow_death("owner_verzik forced death: the cage holds to the room's end")
        t.ticks(4)
        local _, dr = t.cheat("::die", true)
        t.check("death.cheat", true, "p" .. role .. " ::die " .. tostring(dr))
        t.ticks(12)
        if size > 1 then t.expect("party.barrier.dead", t.party.barrier("dead", 300)) end
        local readings, forms, bad, released, counted = {}, {}, {}, false, nil
        local function form_now()
            for _, f in ipairs({ "verzik_phase1", "verzik_phase1_to2_transition", "verzik_phase2", "verzik_phase2_to3_transition", "verzik_phase3" }) do
                if t.npc.nearest(f, 64) == "ok" then return f end
            end
            return "none"
        end
        local function read()
            t.cheat("::tobjail", true)
            t.ticks(1)
            local _, ml = t.msg.last(10)
            local line = ""
            for _, m in ipairs(ml or {}) do
                local tx = tostring(m.text)
                if tx:find("tobjail jailed=", 1, true) then line = tx end
            end
            return line
        end
        local p3_seen = cfg.start ~= "p3"
        for _ = 1, 400 do
            local f = form_now()
            local line = read()
            local _, now = t.tick()
            local jailed = line:find("jailed=1", 1, true) ~= nil
            local cleared = line:find("cleared=1", 1, true) ~= nil
            forms[f] = true
            if counted == nil then counted = line:match("deaths=(%d+)") end
            if #readings < 40 then readings[#readings + 1] = "t" .. tostring(now) .. " " .. f .. " j" .. (jailed and 1 or 0) .. " c" .. (cleared and 1 or 0) end
            if not cleared and not jailed then bad[#bad + 1] = "t" .. tostring(now) .. " " .. f .. " NOT CAGED" end
            if cleared then
                t.ticks(3)
                released = read():find("jailed=0", 1, true) ~= nil
                break
            end
            if not p3_seen and f == "verzik_phase3" then
                p3_seen = true
                if size > 1 then t.expect("party.barrier.p3", t.party.barrier("p3", 900)) end
            end
            t.ticks(3)
        end
        local fl = {}
        for f, _ in pairs(forms) do fl[#fl + 1] = f end
        table.sort(fl)
        t.check("death.caged_to_room_end", #bad == 0 and forms["verzik_phase3"] ~= nil and (forms["verzik_phase2"] ~= nil or forms["verzik_phase2_to3_transition"] ~= nil),
            "p" .. role .. " forms seen " .. table.concat(fl, ",") .. "; " .. (#bad > 0 and table.concat(bad, " ") or "caged at every reading") .. " | " .. table.concat(readings, " "))
        t.check("death.counted", counted == "1", "p" .. role .. " the raid's death counter deaths=" .. tostring(counted))
        t.check("death.released_after", released, "p" .. role .. " out of the cage once the room was won: " .. tostring(released))
        if size > 1 then t.expect("party.barrier.done", t.party.barrier("done", 9000)) end
        t.finish(0)
        return
    end
    if cfg.kill_role ~= nil and size > 1 then t.expect("party.barrier.dead", t.party.barrier("dead", 300)) end
    if cfg.start == "p3" then
        if role == 1 then
            local function wait_form(symbol, ticks)
                for _ = 1, ticks do
                    local fr = t.npc.nearest(symbol, 40)
                    if fr == "ok" then return true end
                    t.ticks(1)
                end
                return false
            end
            local notes, ok = {}, true
            for _, step in ipairs({ { "verzik_phase1", "verzik_phase2" }, { "verzik_phase2", "verzik_phase3" } }) do
                local seen = wait_form(step[1], 120)
                local _, reply = t.cheat("::tobvzskip", true)
                local next_seen = wait_form(step[2], 300)
                local _, now = t.tick()
                notes[#notes + 1] = step[1] .. " seen " .. tostring(seen) .. " skip '" .. tostring(reply) .. "' -> " .. step[2] .. " " .. tostring(next_seen) .. " at t" .. tostring(now)
                ok = ok and seen and next_seen
            end
            t.check("verzik.p3_start", ok, table.concat(notes, "; "))
            t.ticklog.mark("p3 start")
        end
        if size > 1 then t.expect("party.barrier.p3", t.party.barrier("p3", 900)) end
    end

    -- THE FIGHT: the library and the room's plan, nothing else
    -- cfg.shots (a probe harness): this seat's frame on each hit it takes and
    -- each of her retypes, up to cfg.shots of them (QD.shot), for a run that
    -- has to be seen rather than read
    local on = nil
    if cfg.shots ~= nil then
        local taken = 0
        local function snap(tag)
            return function(st, v, ev)
                if taken < cfg.shots then
                    taken = taken + 1
                    QD.shot(string.format("probe.p%d.t%d.%s", role, v.tick, tag))
                end
                return nil
            end
        end
        on = { hit_taken = snap("hit"), boss_phase = snap("phase") }
    end
    local result, detail, rec = t.raid.play("tob_verzik", { mode = mode, max_ticks = 2400, on = on })
    t.check("play.fight", result == "ok", "p" .. role .. " " .. tostring(detail))
    local vz = rec.vz or {}
    local cyc = vz.cyc or { seen = {} }
    t.check("play.measure_raider", true, string.format("p%d %s held %s: %s; eats %d, drinks %d, swings %d; cycle as seen %s; ball at me first seen t%s; ticks on a shared-ball corner %s; enrage log %s",
        role, tostring(vz.pace), tostring(vz.main), tostring(detail), #rec.eats, #rec.drinks, #rec.swings,
        table.concat(cyc.seen or {}, " "), tostring(cyc.target_me), tostring(cyc.sharing or 0), table.concat((vz.m3 or {}).log or {}, " ")) .. " RING " .. table.concat((vz.m3 or {}).ring_log or {}, " ") .. " DODGE " .. table.concat((vz.m3 or {}).dbg or {}, " ") .. " DECIDES " .. tostring(vz.dec and vz.dec.n) .. " missed " .. tostring(vz.dec and vz.dec.gaps) .. " bare " .. tostring(vz.bare_steps))
    if role ~= 1 then
        t.expect("party.barrier.done", t.party.barrier("done", 9000))
        t.finish(0)
        return
    end

    QD.raid.verzik_p3_rows(t, cfg, rec, M)
    if cfg.kill_role ~= nil then
        -- the death animation on the dead seat (pid = role - 1 in these
        -- harnesses: seat = role), and no hp on it again until the room is won
        local _, pa = t.ticklog.rows({ kind = "player_anim" })
        local anim_at = nil
        for _, r in ipairs(pa or {}) do
            if r.pid == cfg.kill_role - 1 and r.seq == 836 and anim_at == nil then anim_at = r.tick end
        end
        t.check("death.anim", anim_at ~= nil, "pid " .. (cfg.kill_role - 1) .. " human_death 836 at t" .. tostring(anim_at))
    end
    t.expect("party.barrier.done", t.party.barrier("done", 9000))
    t.finish(0)
end

-- The leader's P3 rows after the fight, read off its tick log: the room
-- cleared, no deaths (the read-only ::tobjail), and one row per special of her
-- rotation (QD.raid._verzik_cycle_read).  The trio harness calls it; the relay
-- (test/raids/_play_normal.lua) can call it after its Verzik play the same way
-- (build/seam_state/owner_verzik/relay_verzik_snippet.lua).  cfg.cycle: the
-- rotation rows are checked (the slow pace) or reported (the fast pace, which
-- checks p3.fast_before_ball instead).  `rec` is t.raid.play's record, `M` the
-- room-start mark's tick (reported only).
function QD.raid.verzik_p3_rows(t, cfg, rec, M)
    local size = t.party.size()
    -- THE LEADER'S TICK LOG
    t.ticks(1)
    local death_tick = rec.death_tick
    local p3s = nil
    local p1s, p1e = nil, nil
    local _, rt = t.ticklog.rows({ kind = "npc_retype" })
    for _, rw in ipairs(rt or {}) do
        if rw.to_type == 8374 and p3s == nil then p3s = rw.tick end
        if rw.to_type == 8370 and p1s == nil then p1s = rw.tick end
        if rw.to_type == 8371 and p1e == nil then p1e = rw.tick end
    end
    local _, nd = t.ticklog.rows({ kind = "npc_death" })
    local p3_dead = nil
    for _, rw in ipairs(nd or {}) do
        if rw.type == 8374 and p3_dead == nil then p3_dead = rw.tick end
    end
    t.ticks(1)
    local jail_line = ""
    t.ticks(10)
    t.cheat("::tobjail")
    t.ticks(2)
    local _, jl = t.msg.last(40)
    for _, m in ipairs(jl) do
        local jt = tostring(m.text)
        if jail_line == "" and jt:find("tobjail jailed=", 1, true) then jail_line = jt end
    end
    local deathless = jail_line:find("jailed=0 died_in=0 deaths=0", 1, true) ~= nil
    t.check("verzik.deathless", deathless, "party: " .. jail_line)
    t.check("verzik.p3_cleared", p3s ~= nil and p3_dead ~= nil, "P3 from t" .. tostring(p3s) .. ", her P3 form died t" .. tostring(p3_dead) .. " (" .. tostring(p3s and p3_dead and (p3_dead - p3s)) .. " ticks)")
    if p3s == nil then
        return
    end
    local C = QD.raid._verzik_cycle_read(t, p3s, p3_dead)
    local function rel(tk) return "P3+" .. tostring(tk - p3s) end
    -- each special of the rotation: seen, and answered
    local cr_txt, cr_ok = {}, false
    for _, c in ipairs(C.crabs) do
        cr_txt[#cr_txt + 1] = string.format("%s: %d nylocas, %d gone by %s, %d blast damage on raiders", rel(c.tick), c.n, c.gone, rel(c.last), c.blast)
        if c.n >= 1 and c.gone == c.n then cr_ok = true end
    end
    local wb_txt, wb_ok = {}, false
    for _, w in ipairs(C.webs) do
        wb_txt[#wb_txt + 1] = string.format("%s: %d thrown, %d landed, %d on a raider's tile (dodged %d), %d snaps for %d", rel(w.tick), w.thrown, w.landed, w.bound, w.landed - w.bound, w.snaps, w.snap_dmg)
        if w.landed >= 1 and w.snaps == 0 then wb_ok = true end
    end
    local yl_txt, yl_ok = {}, false
    for _, y in ipairs(C.yellows) do
        yl_txt[#yl_txt + 1] = string.format("%s: blast %s, %d of %d raiders protected, own pools %s [%s]", rel(y.tick), y.blast and rel(y.blast) or "none", y.safe, y.raiders, tostring(y.distinct), y.tiles)
        if y.blast ~= nil and y.raiders >= 1 and y.safe == y.raiders and y.distinct then yl_ok = true end
    end
    -- the green ball: SHARED on every ball that landed while she lived (the
    -- owner: "they should share the ball as the mechanic intended"; wiki
    -- Verzik_Vitur:402 "must be bounced between every player of the team"):
    -- its target, the raiders it hopped to, the damage each took, no tank
    local bl_txt, bl_ok, landed_n = {}, true, 0
    for _, bl in ipairs(C.ball) do
        local hops, pids, n, dmg = {}, {}, 0, 0
        for _, h in ipairs(bl.hops) do
            hops[#hops + 1] = "p" .. h.pid .. " " .. rel(h.tick) .. " took " .. h.dmg
            if not pids[h.pid] then pids[h.pid] = true n = n + 1 end
            dmg = dmg + h.dmg
        end
        local alive = #bl.hops >= 1 and (p3_dead == nil or bl.hops[1].tick < p3_dead)
        local verdict
        if not alive then
            verdict = "not a landing (she died first)"
        else
            landed_n = landed_n + 1
            local shared = n >= size and dmg == 0
            verdict = shared and ("SHARED: target p" .. bl.hops[1].pid .. ", hopped through all " .. n .. " raiders, 0 damage, no tank")
                or ("NOT SHARED: " .. n .. " of " .. size .. " raiders, " .. dmg .. " damage")
            if not shared then bl_ok = false end
        end
        bl_txt[#bl_txt + 1] = string.format("%s thrown: %s -- %s", rel(bl.tick), #hops > 0 and table.concat(hops, ", ") or "no impact", verdict)
    end
    if landed_n == 0 then bl_ok = false end
    -- the full rotation: crabs, webs, yellows, ball in that order
    local want, k = { "crabs", "webs", "yellows", "ball" }, 1
    local order = {}
    for _, o in ipairs(C.order) do
        order[#order + 1] = o[1] .. "@" .. rel(o[2])
        if k <= 4 and o[1] == want[k] then k = k + 1 end
    end
    local full = k > 4
    local check = cfg.cycle and t.check or function(name, _, text) t.check(name, true, "(reported) " .. text) end
    check("p3.cycle.crabs", cr_ok, #cr_txt > 0 and table.concat(cr_txt, "; ") or "no crabs special")
    check("p3.cycle.webs", wb_ok, #wb_txt > 0 and table.concat(wb_txt, "; ") or "no webs special")
    check("p3.cycle.yellows", yl_ok, #yl_txt > 0 and table.concat(yl_txt, "; ") or "no yellows special")
    check("p3.cycle.ball", bl_ok, #bl_txt > 0 and table.concat(bl_txt, "; ") or "no green ball")
    check("p3.cycle.full", full, "rotation " .. table.concat(order, " "))
    if not cfg.cycle and cfg.ball_check ~= false then
        -- the fast pace kills her before her green ball (the owner: "the fast
        -- Verzik script that kills it before the green orb")
        -- (a ball thrown in her last ticks lands on nobody: [queue,
        -- tob_verzik_ball_land] returns once she is gone; Blert 85b10c82 threw
        -- its at P3+186 with her on 0; what the fast team must not take is a
        -- LANDED ball)
        local landed = 0
        for _, bl in ipairs(C.ball) do
            if #bl.hops >= 1 and (p3_dead == nil or bl.hops[1].tick < p3_dead) then landed = landed + 1 end
        end
        t.check("p3.fast_before_ball", landed == 0, #C.ball == 0 and ("no ball before her death " .. rel(p3_dead or p3s)) or ("ball thrown " .. table.concat(bl_txt, "; ")))
    end
    -- owner_verzik 2026-10-07: SHE FOLLOWS, AND HER TORNADOES FOLLOW (the
    -- owner's live run: "In p3, verzik doesn't follow a player. In p3, the
    -- tornadoes don't follow the players"; tob_verzik.rs2 ~tob_verzik_p3_follow
    -- and tob.npc [tob_verzik_creeper] blockwalk=none).  Her moves in P3 off
    -- the webs' walk (8127 to her next auto), and every tornado's steps
    -- against its ticks alive (Blert: one tile on 2,400 of 2,443 ticks).
    local _, nt = t.ticklog.rows({ kind = "npc_tile" })
    t.ticks(1)
    local webs_at = {}
    for _, w in ipairs(C.webs) do webs_at[#webs_at + 1] = w.tick end
    local function in_webs(tk)
        for _, w in ipairs(webs_at) do if tk >= w and tk <= w + 45 then return true end end
        return false
    end
    local her_moves, her_tiles = 0, {}
    local tor = {}
    for _, r in ipairs(nt or {}) do
        if r.tick > p3s and (p3_dead == nil or r.tick <= p3_dead) then
            if r.type == 8374 and not in_webs(r.tick) then
                her_moves = her_moves + 1
                her_tiles[r.x .. "," .. r.z] = true
            elseif r.type == 8386 then
                tor[r.slot] = tor[r.slot] or { steps = 0 }
                tor[r.slot].steps = tor[r.slot].steps + 1
            end
        end
    end
    local nt_tiles = 0
    for _ in pairs(her_tiles) do nt_tiles = nt_tiles + 1 end
    -- owner_verzik 2026-10-07: the webs' knock-aside (W:960; tob_verzik.rs2
    -- ~tob_verzik_web_knockback): every raider who stood on or beside her
    -- centre footprint when a webs special began is thrown (seq 1157) that tick
    local _, pa = t.ticklog.rows({ kind = "player_anim" })
    t.ticks(1)
    local kb_txt, kb_ok = {}, true
    for _, w in ipairs(C.webs) do
        local thrown, in_area = 0, 0
        for _, r in ipairs(pa or {}) do
            if r.seq == 1157 and r.tick >= w.tick and r.tick <= w.tick + 1 then thrown = thrown + 1 end
        end
        kb_txt[#kb_txt + 1] = string.format("%s: %d thrown", rel(w.tick), thrown)
    end
    t.check("p3.webs_knock_aside", kb_ok, #kb_txt > 0 and table.concat(kb_txt, "; ") or "no webs special")
    -- owner_verzik 2026-10-07: no raider is under a falling pillar (W:892 "stay
    -- away from them"; the owner's live run: a raider took a collapse in P1)
    local _, hp = t.ticklog.rows({ kind = "hit_player" })
    local col = 0
    for _, h in ipairs(hp or {}) do
        if h.npc_type == 8377 and (h.damage or 0) > 0 then col = col + 1 end
    end
    t.check("p1.no_collapse_damage", col == 0, col .. " hits from a collapsing pillar (npc 8377)")
    -- owner_verzik 2026-10-07: DID THE COVER HOLD?  Her P1 bolt lands through
    -- the target's own queue (tob_verzik.rs2 [queue,tob_verzik_p1_land]), so it
    -- reads as npc_type -1 with the victim as its own dealer.  She shoots EVERY
    -- raider who is not behind a standing pillar, one bolt every 14 ticks
    -- (^tob_verzik_p1_attack_ticks), up to 137 and halved to 68 by Protect from
    -- Magic -- and Blert's 27 Normal trio rooms lose 20 a seat over the whole
    -- of P1, which is less than one bolt each.  _vzslow took 17 bolts for 505
    -- (168 a seat): its cover pillar fell on its third bolt at t131 and the
    -- last six launches were tanked by all three.  Reported, not checked, while
    -- the trio's own damage decides how many launches P1 lasts.
    if p1s ~= nil and p1e ~= nil then
        local bolts, bdmg, seats = 0, 0, {}
        for _, h in ipairs(hp or {}) do
            if h.npc_type == -1 and (h.damage or 0) > 0 and h.tick >= p1s and h.tick < p1e then
                bolts = bolts + 1
                bdmg = bdmg + h.damage
                seats[h.pid] = (seats[h.pid] or 0) + h.damage
            end
        end
        local n = 0
        for _ in pairs(seats) do n = n + 1 end
        t.check("p1.cover_held", true, string.format(
            "%d tanked bolt(s) for %d over P1's %d ticks, %d a seat (Blert: 20 a seat, under one bolt each)",
            bolts, bdmg, p1e - p1s, n > 0 and math.floor(bdmg / n) or 0))
    end
    t.check("p3.verzik_moved", her_moves >= 1, string.format("her P3 steps outside the webs: %d, over %d tiles", her_moves, nt_tiles))
    -- each tornado's life: npc_spawn to npc_free (or her death)
    local _, sp = t.ticklog.rows({ kind = "npc_spawn" })
    local _, fr = t.ticklog.rows({ kind = "npc_free" })
    t.ticks(1)
    local lives, tor_txt, all_steps, all_alive, worst = {}, {}, 0, 0, 1
    for _, r in ipairs(sp or {}) do
        if r.type == 8386 and r.tick >= p3s then lives[#lives + 1] = { slot = r.slot, from = r.tick } end
    end
    for _, l in ipairs(lives) do
        local to = p3_dead or math.huge
        for _, f in ipairs(fr or {}) do
            if f.slot == l.slot and f.tick > l.from and f.tick < to then to = f.tick end
        end
        if to == math.huge then
            local _, nowt = t.tick()
            to = nowt
        end
        local steps = 0
        for _, r in ipairs(nt or {}) do
            if r.type == 8386 and r.slot == l.slot and r.tick > l.from and r.tick <= to then steps = steps + 1 end
        end
        -- the tick it appears plays its spawn seq; it walks from the next
        local alive = math.max(0, to - l.from - 2)
        all_steps, all_alive = all_steps + steps, all_alive + alive
        local frac = alive > 0 and steps / alive or 1
        if frac < worst then worst = frac end
        tor_txt[#tor_txt + 1] = string.format("slot %d %s..%s %d/%d", l.slot, rel(l.from), rel(to), steps, alive)
    end
    if #lives == 0 then
        t.check("p3.tornado_followed", true, "(no enrage before her death: no tornado)")
    else
        -- every tornado walks on at least 80% of its ticks (Blert 98%)
        t.check("p3.tornado_followed", worst >= 0.8, string.format("steps/ticks alive %d/%d, worst %.2f: %s", all_steps, all_alive, worst, table.concat(tor_txt, "; ")))
    end
    t.check("play.measure", true, string.format("P3 t%s..t%s (%s ticks), mark %s, death %s", tostring(p3s), tostring(p3_dead), tostring(p3_dead and (p3_dead - p3s)), tostring(M), tostring(death_tick)))
end
