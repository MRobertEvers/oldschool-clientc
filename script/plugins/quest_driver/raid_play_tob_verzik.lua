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
}

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
function QD.raid._verzik_press_add(st, v, add)
    local ar = QD.player.attack(add.symbol, 2, 1, { quick = true, slot = add.row.slot })
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
    st.attack_presses = (st.attack_presses or 0) + 1
    st.vz.add_presses = st.vz.add_presses + 1
    if ar == "ok" then
        st.engaged = true
        st.engaged_tick = v.tick
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
        st.engaged = true
        st.engaged_tick = v.tick
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
QD.RAID_PLAY_VERZIK_PILLAR_HP = 185
QD.RAID_PLAY_VERZIK_PILLAR_HIT_MAX = 60
function QD.raid._verzik_cover(st, v, ok)
    local b, O = v.boss, st.origin
    local cx = b.x + math.floor((b.size or 1) / 2)
    local best = nil
    for _, p in ipairs(v.pillars) do
        -- the lowest bar seen on it: the bar shows only for a while after a
        -- hit (s34v svb/sva: a bar gone read as a whole pillar, the raiders
        -- hid at both near pillars' loose tiles and both fell on them at t119)
        st.vz.pillar_hp = st.vz.pillar_hp or {}
        local hp = st.vz.pillar_hp[p.slot] or QD.RAID_PLAY_VERZIK_PILLAR_HP
        if p.health_ratio ~= nil and p.health_scale ~= nil and p.health_scale > 0 then
            hp = math.min(hp, math.floor(QD.RAID_PLAY_VERZIK_PILLAR_HP * p.health_ratio / p.health_scale + 0.5))
        end
        st.vz.pillar_hp[p.slot] = hp
        local may_fall = hp <= QD.RAID_PLAY_VERZIK_PILLAR_HIT_MAX
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
            local fall = math.max(math.abs(x - (p.x + 1)), math.abs(z - (p.z + 1)))
            if (fall >= 3 or not may_fall) and ok(x, z) then
                local d = QD.raid._verzik_dist(x, z, b) + (may_fall and 1 or 0)
                -- ties: the west pillar first (W:885 "the pillar directly
                -- south-west of Verzik"), then the tile nearer her centre line,
                -- so all three raiders read the same tile from anywhere
                local side = math.abs(x - cx) + (west and 0 or 100)
                if best == nil or d < best.d or (d == best.d and side < best.side) then
                    best = { x = x, z = z, d = d, side = side, pillar = p, hp = hp, may_fall = may_fall }
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
QD.RAID_PLAY_VERZIK_DAWN_COST = 350
function QD.raid._verzik_dawn(st, v, intent, hiding, on_cover)
    local vz = st.vz
    local dw = vz.dawn
    local _, energy = QD.var.varp("varp300_sa_energy")
    energy = tonumber(energy) or 0
    if dw == nil then
        local cr, n = QD.inv.count("verzik_special_weapon")
        local _, oid = api_drive.symbol("obj", "verzik_special_weapon")
        dw = { state = (cr == "ok" and n > 0) and "held" or "none", specs = {}, appear = 0, on_floor = false,
            e_prev = energy, arm_tick = -1000, obj = oid, took = nil, dropped = nil, refused = {} }
        vz.dawn = dw
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
    if dw.state == "none" then
        -- (taken whenever it lies there on my turn: past the near row there
        -- is no hiding, s34v vzn7: p3 never took it once the trio tanked)
        if floor_now and dw.appear == st.role - 1 then
            local tr, td = QD.player.click_obj("verzik_special_weapon", 3)
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
            local cr, n = QD.inv.count("verzik_special_weapon")
            if cr == "ok" and n > 0 then
                dw.state, dw.took = "held", v.tick
                dw.e_prev = energy
            elseif #dw.refused < 4 then
                dw.refused[#dw.refused + 1] = "t" .. v.tick .. " take " .. tostring(tr) .. ": " .. string.sub(tostring(td), 1, 100)
            end
        end
        return false
    end
    if dw.state == "done" then
        return false
    end
    local spent = #dw.specs >= 2 or energy < QD.RAID_PLAY_VERZIK_DAWN_COST
    if dw.state == "held" then
        if not spent then
            if vz.held ~= "dawnbringer" then
                intent.gear = { "verzik_special_weapon" }
                vz.held = "dawnbringer"
                st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS.dawnbringer
                st.engaged = false
                return true
            end
            if not hiding and v.tick >= dw.arm_tick + 5 then
                dw.arm_tick = v.tick
                intent.spec = true
                intent.attack = true
                st.engaged = false
                return true
            end
            -- between specials: its autos ignore the cap too (tob_damage.rs2
            -- ~tob_verzik_p1_cap: the weapon, not the swing)
            return false
        end
        -- spent: the scythe back on, then the drop on the cover tile
        if vz.held == "dawnbringer" then
            intent.gear = { "scythe_of_vitur" }
            vz.held = "scythe"
            st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS.scythe
            st.engaged = false
            dw.unwield = v.tick
            return false
        end
        -- drop it where the others hide (the last holder keeps it: nobody
        -- is left to take it, and her shield breaking destroys it,
        -- tob_verzik.rs2 ~tob_verzik_shield_broken)
        if on_cover and st.role < st.party then
            local dr, dd = QD.player.drop("verzik_special_weapon")
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
            dw.drop_result = tostring(dr) .. ": " .. string.sub(tostring(dd), 1, 120)
            local cr, n = QD.inv.count("verzik_special_weapon")
            if cr == "ok" and n == 0 then
                dw.state, dw.dropped = "done", v.tick
            end
        elseif st.role >= st.party then
            dw.state = "done"
        end
    end
    return false
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
function QD.raid._verzik_p1_normal(st, v, intent, ok, go)
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
        -- (s34v: every raider was hit by the t60 bolt with 19)
        L = vz.p1_start + 16 + P.p1_launch
        while L < v.tick do L = L + P.p1_cadence end
    end
    local cover = QD.raid._verzik_cover(st, v, ok)
    -- a far shadow is no cover: past the near row (s34v vzn6 t183-236: the
    -- middle pillars' shadows are 10+ from her, the trio walked between
    -- 6424,83 and 6427,93 for 58 ticks and never reached her) the bolts are
    -- TANKED under Protect from Magic (W:887 "even tank her attacks
    -- entirely, to avoid losing out on ticks")
    if cover ~= nil and cover.d > 7 then
        if vz.tank_from == nil then vz.tank_from = v.tick end
        cover = nil
    end
    vz.cover = cover
    local on_cover = cover ~= nil and me.x == cover.x and me.z == cover.z
    local travel = 0
    if cover ~= nil then travel = math.ceil(math.max(math.abs(me.x - cover.x), math.abs(me.z - cover.z)) / 2) end
    -- hide from (L - 2 - travel) through L - 1 (a walk sent on tick T moves
    -- on T + 1, two tiles a tick; one tick more for the walk round the
    -- pillar: s34v _play_verzik t84-88, the leader's route from 6430,98 to
    -- 6426,93 took four ticks, not three, and the bolt found it); the launch
    -- tick itself is the way out
    local hiding = cover ~= nil and v.tick < L and v.tick >= L - 2 - travel
    if hiding and vz.hide_log ~= L then
        vz.hide_log = L
        vz.hides = vz.hides or {}
        vz.hides[#vz.hides + 1] = { L = L, from = v.tick, travel = travel }
    end
    local busy = QD.raid._verzik_dawn(st, v, intent, hiding, on_cover)
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
    local pr, prow = api_drive.players()
    if pr ~= "ok" then return true end
    local b, low = v.boss, nil
    for _, r in ipairs(prow) do
        if r.pid ~= nil and math.max(math.abs(r.x - b.x), math.abs(r.z - b.z)) <= 20 and (low == nil or r.pid < low) then low = r.pid end
    end
    return low == nil or low == st.my_pid
end

-- The other raiders' tiles (api_drive.players), for the step's tie-break.
function QD.raid._verzik_mates(st)
    local mates = {}
    if st.party > 1 then
        local pr, prow = api_drive.players()
        if pr == "ok" then
            for _, r in ipairs(prow) do
                if not r.me then mates[#mates + 1] = r end
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
    if cd <= 4 then
        QD.raid._verzik_crabs(st, v, ok2, go)
        if intent.walk ~= nil then
            M.kites = M.kites + 1
            st.engaged = false
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
            st.engaged = false
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
                st.engaged = false
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
            st.engaged = false
        end
        return threat
    end
    local purple = nearest(v.purples)
    local red = nearest(v.reds)
    -- (the Athanatos is left while a nylocas is within 4 of it: its press
    -- walks the raider into the blast)
    if purple ~= nil then
        for _, c in ipairs(v.crabs) do
            if math.max(math.abs(c.row.x - purple.row.x), math.abs(c.row.z - purple.row.z)) <= 4 then purple = nil break end
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
    if b.health_ratio ~= nil and b.health_scale ~= nil and b.health_scale > 0 then pct = math.max(0, (b.health_ratio * 100 / b.health_scale - 50) * 2) end
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
                st.engaged = false
            end
        end
    else
        if vz.target_slot ~= nil then st.engaged = false vz.target_slot = nil end
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
            st.vz.held = "scythe"
            st.vz.n.scythe = 0
            st.weapon = QD.RAID_PLAY_VERZIK_WEAPONS.scythe
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
    local ok = QD.raid._verzik_floor(st, v)
    local me = v.me
    local d_boss = QD.raid._verzik_dist(me.x, me.z, b)
    local function go(tx, tz)
        local sx, sz = QD.raid._play_hazard(st, v, tx, tz, ok)
        sx, sz = QD.raid._play_safe_step(st, v, sx, sz, ok)
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
        st.engaged = false
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

    if phase == "p1" and st.mode == "normal" then
        -- raid seam34v: the Normal trio's P1 (its own function above)
        threat = QD.raid._verzik_p1_normal(st, v, intent, ok, go)

    elseif phase == "p1" then
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

    elseif phase == "pre" then
        return intent

    elseif phase == "t12" then
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
            if st.mode == "normal" then hx, hz = QD.raid._verzik_p2_home(st, O.x + 31, O.z + 25, 3, 1) end
            go(hx, hz)
        end

    elseif phase == "p2" then
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
                    st.engaged = false
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
                    if vz.target_slot ~= nil then st.engaged = false vz.target_slot = nil end
                    intent.attack = true
                end
            end
        end

    elseif phase == "t23" then
        -- P2 -> P3: heal to full (W:943 "Make sure to heal to full before the
        -- next phase starts"); the prayers stay up for her first auto
        intent.want.protectfrommissiles = true
        intent.want.rigour = true
        if st.mode == "normal" then intent.want.rigour = nil intent.want.piety = true end
        threat = function(h) return v.hp_base - 21 end
        vz.reds_tick = nil

    elseif phase == "p3" then
        -- raid seam45: the Normal trio plays P3 MELEE (Blert: the scythe,
        -- 21-24 swings a role); the clock and the step out are
        -- QD.raid._verzik_p3_clock's, the rest of the phase is shared
        local melee = st.mode == "normal"
        if not melee and vz.held ~= "bow_rapid" then swap_to("bow_rapid") end
        -- her style shows on the attack tick and the protection is read when
        -- it lands (the owner's ruling; V p3_prayer_read: "8125 stomp + 1593,
        -- 8124 crackle + 1594 and the flight is 2-3 ticks"): switch on sight
        -- (W:951 "it's important to switch prayers accordingly")
        local ball = false
        for _, p in ipairs(v.proj) do
            if p.spotanim_id == P.ball_proj then ball = true end
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
        if b.health_ratio ~= nil and b.health_scale ~= nil and b.health_scale > 0 and b.health_ratio * 5 <= b.health_scale then vz.enraged = true end
        local cadence = vz.enraged and P.p3_enraged_cadence or P.p3_cadence
        -- the floor: two out of her (W:953 "the primary tank should either
        -- walk under or away from Verzik one or two ticks before she attacks
        -- to avoid the melee attack"; V p3_melee_predicate adjacent on T-1)
        local okp = ok
        ok = function(x, z) return okp(x, z) and QD.raid._verzik_dist(x, z, b) >= 2 end
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
        if st.party > 1 and #uniq >= st.party then
            local sorted = {}
            for _, p in ipairs(uniq) do sorted[#sorted + 1] = p end
            table.sort(sorted, function(a, c) if a.x ~= c.x then return a.x < c.x end return a.z < c.z end)
            local p = sorted[st.role] or sorted[1]
            -- kept for the whole charge once chosen from the full set (s34v
            -- vzn5: the leader's pick flipped between 6426,79 and 6426,81 as
            -- the client's list changed, it walked between them for 14 ticks
            -- and the blast took its last 53)
            if vz.my_pool == nil or vz.my_pool.first ~= vz.pool_first then
                vz.my_pool = { x = p.x, z = p.z, first = vz.pool_first }
            end
        end
        -- raid seam49: fewer pools in view than raiders: the r-th of those in
        -- view, never the nearest (s49 final svc/svd: the two members, both
        -- beside her east edge now that only the tank steps out, took the
        -- same nearest pool and the blast killed one, t514 / t555)
        if st.party > 1 and #uniq > 0 and #uniq < st.party and (vz.my_pool == nil or vz.my_pool.first ~= vz.pool_first) then
            local sorted = {}
            for _, p in ipairs(uniq) do sorted[#sorted + 1] = p end
            table.sort(sorted, function(a, c) if a.x ~= c.x then return a.x < c.x end return a.z < c.z end)
            local p = sorted[((st.role - 1) % #sorted) + 1]
            pool = { x = p.x, z = p.z, d = math.max(math.abs(p.x - me.x), math.abs(p.z - me.z)) }
        end
        if st.party > 1 and #uniq > 0 and vz.my_pool ~= nil and vz.my_pool.first == vz.pool_first then
            pool = { x = vz.my_pool.x, z = vz.my_pool.z, d = math.max(math.abs(vz.my_pool.x - me.x), math.abs(vz.my_pool.z - me.z)) }
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
                e = { x = tr.x, z = tr.z, rx = tr.x, rz = tr.z, tick = v.tick, moved = (e ~= nil) and v.tick or nil }
                vz.tor[tr.slot] = e
            end
            while e.tick < v.tick do
                e.tick = e.tick + 1
                if math.max(math.abs(e.x - me.x), math.abs(e.z - me.z)) > 1 then
                    if melee then
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
                        if me.x > e.x then e.x = e.x + 1 elseif me.x < e.x then e.x = e.x - 1 end
                        if me.z > e.z then e.z = e.z + 1 elseif me.z < e.z then e.z = e.z - 1 end
                    end
                end
            end
            live[tr.slot] = true
        end
        for slot, _ in pairs(vz.tor) do
            if not live[slot] then vz.tor[slot] = nil end
        end
        local tor, td = nil, 999
        -- raid seam45: the nearest tornado SEEN moving this tick or last
        -- (its row changed tile), for the melee dodge: a row that moves is
        -- what a person sees; the simulated walk is not
        local seen_tor, seen_td = nil, 999
        for _, e in pairs(vz.tor) do
            local d = math.max(math.abs(e.x - me.x), math.abs(e.z - me.z))
            if d < td then tor, td = e, d end
            if e.moved ~= nil and v.tick - e.moved <= 1 and d < seen_td then seen_tor, seen_td = e, d end
        end
        local _, cd = nearest(v.crabs)
        local crab = nil
        -- raid seam45: my tile on her east edge (the side the tornadoes
        -- cannot reach, below)
        local n3 = b.size or 1
        local hx3, hz3 = b.x + n3, b.z + n3 - 1 - 2 * ((st.role - 1) % 3)
        if melee and vz.enraged and #vz.m3.log < 20 then
            local rows = {}
            for _, tr in ipairs(v.tornadoes) do rows[#rows + 1] = tr.x .. "," .. tr.z end
            vz.m3.log[#vz.m3.log + 1] = v.tick .. "d" .. d_boss .. "@" .. me.x .. "," .. me.z .. (tor and ("T" .. tor.x .. "," .. tor.z .. "/" .. td) or "") .. "r" .. table.concat(rows, ";")
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
                    if not r.me then mates[#mates + 1] = r end
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
            if ball then t = t + N.ball end
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
        if st.mode == "normal" and pool ~= nil and not on_pool and tor ~= nil and td <= P.tornado_run and vz.pool_first ~= nil then
            local left = vz.pool_first + P.pool_life - v.tick
            if left > math.ceil(pool.d / 2) + 2 then
                pool_late = true
                near_pool = pool
            end
        end
        if pool ~= nil and not pool_late then
            if not on_pool then
                intent.walk = { x = pool.x, z = pool.z }
                vz.steps = vz.steps + 1
            elseif st.mode == "normal" and st.engaged then
                -- on it and still swinging: a click on the pool's own tile
                -- ends the bow's repeat, which would path off it (s34v
                -- _play_verzik t578)
                intent.walk = { x = me.x, z = me.z }
                st.engaged = false
                vz.target_slot = nil
            end
        elseif melee and seen_tor ~= nil and seen_td <= 2 then
            tor, td = seen_tor, seen_td
            -- raid seam45: the melee trio DODGES its tornado along her edge
            -- instead of running from it or tanking it.  In our room a touch
            -- comes back 16 ticks later (tob.constant
            -- ^tob_verzik_p3_tornado_respawn) and heals her triple (W:981),
            -- so three raiders standing still beside her heal her about as
            -- fast as they hit her (s45 e6 sva: 21 touches, 694 taken, an
            -- enrage of 230 ticks with every raider running); W:981 "in the
            -- off chance it damages the player" -- a touch is the exception.
            -- It walks one tile a tick and a run is two (s30 vz30h), so a
            -- step of up to two along her edge, to the tile furthest from its
            -- next tile, keeps it off while the scythe stays in reach; her
            -- body is a wall to it (s45 e6 sva t738-758: 330 tornado tiles,
            -- one inside her 7x7, its spawn).
            local best, bx, bz = nil, nil, nil
            for dx = -2, 2 do
                for dz = -2, 2 do
                    local x, z = me.x + dx, me.z + dz
                    if (dx ~= 0 or dz ~= 0) and okp(x, z) and not v.shadows[x * 100000 + z] then
                        local nx, nz = tor.x, tor.z
                        if x > nx then nx = nx + 1 elseif x < nx then nx = nx - 1 end
                        if z > nz then nz = nz + 1 elseif z < nz then nz = nz - 1 end
                        local d = math.max(math.abs(x - nx), math.abs(z - nz))
                        -- (on a hold tick, two out of her: her melee hits
                        -- everyone beside her, s45 e7 svb t759-761: a dodge
                        -- along her edge on T-1 took her 60 and the tornado's 60)
                        local dd = QD.raid._verzik_dist(x, z, b)
                        if d >= 2 and (not m3_hold or dd >= 2) then
                            local edge = (dd == 1) and 0 or 1
                            local sc = edge * 100 - math.min(d, 4) * 10 + math.max(math.abs(dx), math.abs(dz))
                            if best == nil or sc < best then best, bx, bz = sc, x, z end
                        end
                    end
                end
            end
            if bx ~= nil then
                intent.walk = { x = bx, z = bz }
                vz.steps = vz.steps + 1
                vz.m3.dodges = vz.m3.dodges + 1
                st.engaged = false
                vz.target_slot = nil
            end
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
        elseif melee and cd <= 3 and QD.raid._verzik_crabs(st, v, ok, go) == nil and intent.walk ~= nil then
            -- raid seam45: a nylocas within 3 is run from before anything
            -- else (W:925); its blast on arrival is 63 within 1 and two
            -- arrive together (s45 e14 svb t792-793: 14 + 63 + 18 on the
            -- leader at 60 beside her east edge)
            vz.m3.kites = (vz.m3.kites or 0) + 1
        elseif melee and not on_me and (me.x < b.x or me.z < b.z or d_boss > 2) and okp(hx3, hz3) then
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
            if me.x < b.x + n3 then
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
                st.engaged = false
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
                st.engaged = false
                vz.target_slot = nil
                vz.m3.apart = (vz.m3.apart or 0) + 1
            end
        elseif st.party > 1 and crowd(me.x, me.z) > 0 and (not melee or ball) then
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
        if intent.walk == nil and pool == nil then crab = QD.raid._verzik_crabs(st, v, ok, go) end
        -- raid seam45: melee never swings at a nylocas (its death blasts
        -- everyone within 3, ~tob_verzik_crab_blast); it is only run from
        if melee then crab = nil end
        -- raid seam45: the step out of her reach on the plan's T-1 (ET 1.1; V
        -- p3_melee_predicate), and no press on a held tick
        if melee and m3_hold and intent.walk == nil then
            vz.m3.holds = vz.m3.holds + 1
            if d_boss == 1 then
                local sx, sz = QD.raid._verzik_step_out(st, v, okp, mates)
                if sx ~= nil then
                    intent.walk = { x = sx, z = sz }
                    vz.steps = vz.steps + 1
                    vz.m3.outs = vz.m3.outs + 1
                    st.engaged = false
                    vz.target_slot = nil
                end
            elseif st.engaged then
                -- engaged from two out, the server would path the swing in:
                -- a click on my own tile clears it
                intent.walk = { x = me.x, z = me.z }
                st.engaged = false
                vz.target_slot = nil
            end
        end
        -- raid seam35e play_tob_entry_relay: no attack press from the pool in
        -- the blast's last ticks.  The bow's press paths to its own range and
        -- sight line, and from the pool at 6428,207 that path was 6428,209: on
        -- every swing the raider stepped off and the plan walked him back, and
        -- the blast (judged on the tile of T-1, ET 1.1) found him off it (the
        -- relay's own name, P3, 44 taken at 44 hitpoints).  Standing still on
        -- it for the window keeps him on it; outside the window nothing changes.
        local blast_close = pool ~= nil and on_pool and vz.pool_first ~= nil and v.tick >= vz.pool_first + P.pool_life - 4
        if intent.walk == nil and blast_close then
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
            else
                if vz.target_slot ~= nil then st.engaged = false vz.target_slot = nil end
                intent.attack = true
            end
        end
    end

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
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    return intent
end
