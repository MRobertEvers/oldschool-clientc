-- Theatre of Blood, Verzik Vitur, Entry mode, solo party.
return {
    id = "tob_verzik",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000,
    setup = {
        "::clearinv",
        -- combat stats an Entry-mode Verzik player has
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel ranged 99", "::setlevel magic 99",
        -- no armour is carried: the backpack has 28 slots and the rest is food; the P1 melee is bare fists, a 4-tick punch
        -- the Dawnbringer: the raid hands it over at the Xarpus exit (tob_xarpus.rs2); the room before Verzik is not played here
        "::give verzik_special_weapon 1",
        -- the P2 and P3 weapon and its ammunition (swapped to once the shield is down)
        "::give twisted_bow 1", "::give dragon_arrow 500",
        -- one of each potion the Entry debug kit hands out, so it adds no more and the backpack stays food
        "::give br_4dosepotionofsaradomin 1", "::give br_4dose2restore 1",
        -- food for the fight: anglerfish heal 31 at 99 hitpoints (28 slots minus the five items above)
        "::give anglerfish 23",
    },
    run = function(t)
        local r, d = t.raid.enter("tob", "verzik", { mode = "entry" })
        t.check("verzik.enter", r == "ok", tostring(d))
        local sr, st = t.raid.state()
        t.check("verzik.state", sr == "ok" and st.started == false, tostring(st and st.line))
        t.check("spec.scope", true, "mode=entry party=1")
        t.ticklog.start()
        local boss_found, boss = t.npc.nearest("verzik_initial", 30)
        t.check("verzik.boss_present", boss_found == "ok", "boss row")

        -- read-only readouts before the fight: her pools and levels, and the six pillars' hitpoints
        t.cheat("::tobboss")
        t.ticks(2)
        local _, ml0 = t.msg.last(8)
        local tb_pre = ""
        for _, m in ipairs(ml0) do
            if tb_pre == "" and tostring(m.text):find("tobboss record=") then tb_pre = tostring(m.text) end
        end
        t.check("verzik.tobboss", tb_pre ~= "", tb_pre)
        t.cheat("::tobpillars 0")
        t.ticks(2)
        local _, ml1 = t.msg.last(10)
        local pillar_hp_pre, pillar_seen = {}, {}
        for _, m in ipairs(ml1) do
            local pi, ph = string.match(tostring(m.text), "tobpillars (%d+) hp=(%d+)")
            if pi and not pillar_seen[pi] then
                pillar_seen[pi] = true
                pillar_hp_pre[#pillar_hp_pre + 1] = tonumber(ph)
            end
        end
        t.check("verzik.pillars_pre", #pillar_hp_pre > 0, "::tobpillars 0 read " .. #pillar_hp_pre .. " standing pillars, hitpoints " .. table.concat(pillar_hp_pre, ","))
        local hpr, hpv0 = t.skill.read("hitpoints")
        t.check("verzik.hp_read", hpr == "ok", "hitpoints " .. tostring(hpv0.current or hpv0.level))
        -- safety net, not the technique: protect from magic halves a bolt that finds the player
        t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)

        local tr, td = t.player.talk_to("verzik_initial", 1)
        t.check("verzik.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("verzik.begin", cr == "ok", tostring(cd))
        t.ticklog.mark("room start")
        local slot_res, boss_slot = t.ticklog.slot(boss)
        t.check("verzik.slot", slot_res == "ok", "server slot " .. tostring(boss_slot))
        local _, mk = t.ticklog.rows({ kind = "mark" })
        local M = mk[#mk].tick
        local base_serial = mk[#mk].serial

        -- ------------------------------------------------------------------
        -- the whole fight is one loop over the tick log: every iteration waits one server tick,
        -- pulls the rows the server wrote since the last one, and acts on the phase
        -- ------------------------------------------------------------------
        local kinds = { anim = "npc_anim", proj = "projectile", hitp = "hit_player", hitn = "hit_npc", spawn = "npc_spawn",
            death = "npc_death", free = "npc_free", retype = "npc_retype", mapfx = "map_spotanim", ptile = "player_tile", spot = "npc_spotanim" }
        local cur, R = {}, {}
        for k, _ in pairs(kinds) do
            cur[k] = base_serial
            R[k] = {}
        end
        local ptile_at = {}
        local HIDE = { { 6426, 93 }, { 6426, 87 }, { 6426, 81 } }
        local phase = "P1"
        local enrage_bug = nil
        local tk = M
        local seg = { { tick = M, w = "fists" } }
        -- the spec table's value, grade and tolerance for every row this test writes (docs/minigames/theater_of_blood/encounters/verzik.tsv)
        local SPEC = {
                p1_hp_5 = "(spec 2000 hp, grade A, tol exact)",
                p1_hp_4 = "(spec 1750 hp, grade A, tol exact)",
                p1_hp_3 = "(spec 1500 hp, grade A, tol exact)",
                p23_hp_5 = "(spec 3500 hp, grade A, tol exact)",
                p23_hp_4 = "(spec 3062 hp, grade A, tol exact)",
                p23_hp_3 = "(spec 2625 hp, grade A, tol exact)",
                entry_p1_hp_1p = "(spec 300 hp, grade A, tol exact)",
                entry_p2_hp_1p = "(spec 400 hp, grade A, tol exact)",
                entry_p3_hp_1p = "(spec 600 hp, grade A, tol exact)",
                entry_hp_scaling = "(spec 90,80,70,60 percent, grade A, tol exact)",
                entry_p1_hp_5p = "(spec 1200 hp, grade A, tol exact)",
                entry_p2_hp_5p = "(spec 1600 hp, grade A, tol exact)",
                entry_p3_hp_5p = "(spec 2400 hp, grade A, tol exact)",
                web_hp = "(spec 10 hp, grade A, tol exact)",
                pillar_hp = "(spec 185 hp, grade A, tol exact)",
                entry_pillar_hp = "(spec 200 hp, grade A, tol exact)",
                reds_hp_5 = "(spec 200 hp, grade A, tol exact)",
                reds_hp_3 = "(spec 150 hp, grade A, tol exact)",
                entry_reds_hp_1p = "(spec 20 hp, grade A, tol exact)",
                athanatos_hp = "(spec 180 hp, grade A, tol exact)",
                entry_athanatos_hp = "(spec 30 hp, grade A, tol exact)",
                combat_nylo_hp = "(spec 25 hp, grade A, tol exact)",
                entry_combat_nylo_hp = "(spec 3 hp, grade A, tol exact)",
                p1_defence = "(spec 20 count, grade A, tol exact)",
                p2_defence = "(spec 200 count, grade A, tol exact)",
                p3_defence = "(spec 150 count, grade A, tol exact)",
                p3_magic_ranged_level = "(spec 300 count, grade A, tol exact)",
                entry_defence = "(spec 10,120,120 count, grade A, tol exact)",
                p1_cadence = "(spec 14 ticks, grade B, tol exact)",
                p1_first_windup = "(spec 19 ticks, grade B, tol +-1)",
                p1_launch_after_windup = "(spec 3 ticks, grade D, tol exact)",
                p1_bolt_flight = "(spec 110 cycles, grade D, tol exact)",
                p1_bolt_impact_after_launch = "(spec 3 ticks, grade D, tol exact)",
                p1_verdict_tick = "(spec 0 ticks, grade D, tol exact)",
                p1_max_hit = "(spec 137 hp, grade D, tol range)",
                entry_p1_max_hit = "(spec 60 hp, grade D, tol range)",
                p1_cap = "(spec 10,3,3 hp, grade A, tol exact)",
                p1_dawn_damage = "(spec 75-150 hp, grade C, tol range)",
                p1_safe_swings = "(spec 4 count, grade D, tol exact)",
                pillar_count = "(spec 6 count, grade B, tol exact)",
                pillar_hit = "(spec 40-60 hp, grade D, tol range)",
                pillar_collapse_max = "(spec 70 hp, grade D, tol range)",
                entry_pillar_collapse_max = "(spec 50 hp, grade D, tol range)",
                pillar_stun = "(spec 5 ticks, grade E, tol approx); approximation, M47",
                pillar_knockback = "(spec 2 tiles, grade E, tol approx); approximation, M47",
                pillar_hide_range = "(spec 2 tiles, grade E, tol approx); approximation, M47",
                pillar_collapse_range = "(spec 2 tiles, grade E, tol approx); approximation, M47",
                hard_debris_sets = "(spec 3 count, grade D, tol exact)",
                hard_debris_max = "(spec 35 hp, grade D, tol range)",
                hard_debris_radius = "(spec 3 tiles, grade D, tol exact)",
                p2_id_after_phase_event = "(spec 13 ticks, grade B, tol +-1)",
                p2_first_attack_after_id = "(spec 3 ticks, grade B, tol exact)",
                p2_first_attack_after_phase_event = "(spec 16 ticks, grade B, tol +-1)",
                p2_cadence = "(spec 4 ticks, grade B, tol exact)",
                p2_scan_tick = "(spec -1 ticks, grade C, tol exact)",
                p2_scan_rule = "(spec adjacent or inside on T-1 text, grade C, tol exact)",
                p2_scythe_walk_ticks = "(spec 3 ticks, grade D, tol exact)",
                p2_bomb_max = "(spec 44 hp, grade D, tol range)",
                entry_p2_bomb_max = "(spec 16 hp, grade D, tol range)",
                p2_bomb_judged_tile = "(spec previous tick tile text, grade C, tol exact)",
                p2_bomb_live_ticks = "(spec 1 ticks, grade D, tol exact)",
                p2_bomb_flight = "(spec ? ticks, grade E, tol approx); approximation, M80",
                p2_slam_damage_max = "(spec 45 hp, grade D, tol range)",
                p2_slam_knockback = "(spec 3 tiles, grade D, tol exact)",
                p2_slam_stun = "(spec 5 ticks, grade D, tol exact)",
                p2_slam_chance = "(spec 75 percent, grade E, tol approx); approximation, M48",
                p2_stomp_max = "(spec 82 hp, grade D, tol range)",
                p2_stomp_knockback = "(spec 2 tiles, grade E, tol approx); approximation, M83",
                p2_stomp_stun = "(spec 7 ticks, grade E, tol approx); approximation, M83",
                p2_zap_max = "(spec 48 hp, grade D, tol range)",
                p2_zap_self_damage = "(spec 15-20 hp, grade A, tol range)",
                p2_zap_bounces = "(spec 4 count, grade D, tol exact)",
                p2_zap_floor = "(spec 4 count, grade B, tol exact)",
                p2_purple_first = "(spec 0-12 count, grade B, tol range)",
                p2_purple_gap = "(spec 19-24 count, grade B, tol range)",
                p2_purple_roll = "(spec 25 percent, grade D, tol range)",
                p2_purple_gate = "(spec suppressed while alive text, grade D, tol exact)",
                p2_crabs_with_purple = "(spec 0 ticks, grade B, tol +-1)",
                p2_purple_land = "(spec 6 ticks, grade B, tol +-1)",
                p2_purple_poison_dmg = "(spec 70 hp, grade D, tol range)",
                p2_purple_heal = "(spec 9-10 hp, grade D, tol range)",
                p2_purple_heal_period = "(spec 5 ticks, grade E, tol approx); approximation, M48",
                p2_crab_per_cast = "(spec 1 ratio, grade B, tol range)",
                p2_crab_blast_near = "(spec 63 hp, grade D, tol range)",
                p2_crab_blast_mid = "(spec 26 hp, grade E, tol approx); approximation, M83",
                p2_crab_blast_far = "(spec 8 hp, grade D, tol range)",
                p2_crab_lifetime = "(spec 25 ticks, grade D, tol range)",
                reds_threshold = "(spec 35 percent, grade B, tol exact)",
                reds_count = "(spec 2 count, grade A, tol exact)",
                reds_attacks_between = "(spec 7 count, grade B, tol exact)",
                reds_first_attack_after = "(spec 12 ticks, grade B, tol exact)",
                reds_absorb_window = "(spec 5 ticks, grade D, tol exact)",
                p2_heal_spell_chance = "(spec 75 percent, grade A, tol exact)",
                p2_heal_spell_max = "(spec 45 hp, grade A, tol range)",
                p2_heal_spell_fraction = "(spec 50 percent, grade A, tol exact)",
                p3_id_after_phase_event = "(spec 6 ticks, grade B, tol +-1)",
                p3_first_attack_after_id = "(spec 6 ticks, grade C, tol +-1)",
                p3_first_attack_after_phase_event = "(spec 12 ticks, grade C, tol +-1)",
                p3_cadence = "(spec 7 ticks, grade A, tol exact)",
                p3_enraged_cadence = "(spec 5 ticks, grade C, tol exact)",
                p3_enrage_threshold = "(spec 20 percent, grade B, tol exact)",
                p3_tornado_per_raider = "(spec 1 ratio, grade B, tol exact)",
                p3_attacks_between_specials = "(spec 4 count, grade C, tol exact)",
                p3_special_order = "(spec crabs,webs,yellows,ball text, grade C, tol exact)",
                p3_scan_tick = "(spec -1 ticks, grade C, tol exact)",
                p3_melee_predicate = "(spec adjacent not overlapping on T-1 text, grade C, tol exact)",
                p3_melee_chance = "(spec 50 percent, grade E, tol approx); approximation, M49",
                p3_aggro_timeout = "(spec 17 ticks, grade D, tol range)",
                p3_auto_max = "(spec 33-34 hp, grade D, tol range)",
                entry_p3_auto_max = "(spec 20 hp, grade D, tol range)",
                p3_melee_max = "(spec 63 hp, grade D, tol range)",
                entry_p3_melee_max = "(spec 36 hp, grade D, tol range)",
                p3_yellow_pool_lifetime = "(spec 14 ticks, grade B, tol exact)",
                hard_yellow_pool_lifetime = "(spec 20 ticks, grade B, tol exact)",
                p3_yellow_pools = "(spec 1 ratio, grade B, tol exact)",
                hard_yellow_pools = "(spec 3 ratio, grade B, tol exact)",
                p3_yellow_max = "(spec 80 hp, grade D, tol range)",
                p3_after_yellows = "(spec 7 ticks, grade C, tol exact)",
                p3_ball_delay = "(spec 12 ticks, grade C, tol exact)",
                p3_ball_flight = "(spec 7 ticks, grade D, tol range)",
                p3_ball_damage_pct = "(spec 75 percent, grade D, tol range)",
                hard_ball_ladder = "(spec 89,74,49 hp, grade D, tol range)",
                p3_webs_per_cast = "(spec 3 count, grade D, tol exact)",
                p3_web_lifetime = "(spec ? ticks, grade E, tol approx); approximation, M81",
                p3_web_snap_damage = "(spec 40 hp, grade E, tol approx); approximation, M50",
                p3_web_rotation_ticks = "(spec 5 ticks, grade D, tol exact)",
                p3_webs_to_next_auto = "(spec 38-50 ticks, grade D, tol range)",
                p3_after_special = "(spec 10 ticks, grade D, tol exact)",
                p3_tornado_min = "(spec 5 hp, grade A, tol exact)",
                p3_tornado_pct = "(spec 50 percent, grade D, tol range)",
                p3_tornado_heal_mult = "(spec 3 ratio, grade D, tol exact)",
                p3_tornado_respawn = "(spec 16 ticks, grade D, tol exact)",
                p3_proj_flight = "(spec ? ticks, grade E, tol approx); approximation, M80",
                hard_bomb_pct = "(spec 120 percent, grade D, tol range)",
                hard_acid_ticks = "(spec 16 ticks, grade D, tol exact)",
                hard_acid_max = "(spec 10 hp, grade D, tol range)",
                hard_reds_explode = "(spec 46 hp, grade E, tol approx); approximation, M51",
                hard_nylo_cycle = "(spec 4 ticks, grade E, tol approx); approximation, M51",
                hard_lastheal_threshold = "(spec 5 percent, grade D, tol exact)",
                hard_lastheal_amount = "(spec 30 percent, grade D, tol exact)",
            }
        local out_rows, hp_by_tick, enraged_tick = {}, {}, nil
        local p1_rows_done, p2_rows_done, pools_done = 0, 0, 0
        local p2_emit, p3_emit
        local EARLY3 = {}
        for _, nm in ipairs({ "p3_id_after_phase_event", "p3_first_attack_after_id", "p3_first_attack_after_phase_event", "p3_cadence",
            "p3_attacks_between_specials", "p3_after_special", "p3_melee_predicate", "p3_scan_tick", "p3_melee_chance", "entry_p3_melee_max",
            "entry_p3_auto_max", "p3_proj_flight", "web_hp", "p3_webs_to_next_auto" }) do
            EARLY3["spec.verzik." .. nm] = true
        end
        local emitted = {}
        local EARLY = {}
        for _, nm in ipairs({ "p2_id_after_phase_event", "p2_first_attack_after_id", "p2_first_attack_after_phase_event", "p2_cadence", "p2_bomb_live_ticks",
            "entry_p2_bomb_max", "p2_bomb_judged_tile", "p2_scan_rule", "p2_scan_tick", "p2_slam_chance", "p2_scythe_walk_ticks", "p2_slam_knockback",
            "p2_slam_stun", "p2_stomp_knockback", "p2_stomp_stun", "p2_purple_first", "p2_purple_land", "p2_crabs_with_purple", "p2_crab_per_cast",
            "p2_purple_heal_period", "p2_purple_gate", "entry_athanatos_hp", "entry_combat_nylo_hp", "p2_zap_floor" }) do
            EARLY["spec.verzik." .. nm] = true
        end
        EARLY["tech.p2_no_slam_out_of_reach"] = true
        local plog, hplog = {}, {}
        local AS = { 6428, 93 }
        local leave_tick, p2id_tick, E_tick, p3id_tick, death_tick
        local pillar_retypes = {}
        local last_eat = -10
        local iter = 0
        local why_end = "loop ended"
        local p1 = { step = 0, T1 = M + 17, last_atk = -10, last_spec = -10, spec_tries = 0, worn = "fists", pos = "boss", hide_idx = 0, presses = 0, n_pr_seen = 0 }
        local t12 = { done = 0 }
        local p2 = { pending = {}, tries = {}, exp = nil, valid = { J1 = 0, J2 = 0, E1 = 0, E4 = 0, E5 = 0 }, need = { J1 = 1, J2 = 1, E1 = 1, E4 = 1, E5 = 1 },
            trials = {}, last_atk = -10, aims = {}, mode = "exp", started = 0, slams = {}, last_target = "", fight_from = 0, tb_sent = 0, dodges = 0, reds_prayer = 0, reds_tb = 0 }
        local p3 = { autos = 0, plan = "ADJ", next_T = 0, last_atk = -10, specials = {}, attacks = {}, dealt_cap = 99999,
            ball_seen = 0, enraged = 0, tornado_hits = {}, pool = nil, last_flee = -10, stepped = 0, tobboss_sent = 0, prep = 0 }
        local notes = {}
        local boss_hp_p3 = ""
        local tb_p1, tb_p2, tb_p3 = "", "", ""
        local tb_reds, tb_enr, reds_tick = "", "", 0

        while phase ~= "DONE" do
            iter = iter + 1
            if iter > 2600 then why_end = "2600 iterations" break end
            t.ticks(1)
            local _, tkv = t.tick()
            tk = tkv
            local new = {}
            for k, kind in pairs(kinds) do
                new[k] = {}
                local _, rr = t.ticklog.rows({ kind = kind, since = cur[k] })
                for _, row in ipairs(rr) do
                    R[k][#R[k] + 1] = row
                    new[k][#new[k] + 1] = row
                    cur[k] = row.serial
                    if k == "ptile" then ptile_at[row.tick] = row end
                end
            end
            for _, row in ipairs(new.retype) do
                if row.slot == boss_slot then
                    if row.to_type == 8371 then leave_tick = row.tick
                    elseif row.to_type == 8372 then p2id_tick = row.tick
                    elseif row.to_type == 8374 then p3id_tick = row.tick end
                elseif row.to_type == 8377 and row.from_type == 8379 then
                    pillar_retypes[#pillar_retypes + 1] = row
                end
            end
            for _, row in ipairs(new.anim) do
                if row.slot == boss_slot and row.seq == 8118 then E_tick = row.tick end
            end
            for _, row in ipairs(new.death) do
                if row.slot == boss_slot then death_tick = row.tick end
            end
            if death_tick ~= nil then phase = "DONE" why_end = "boss death row" break end
            if t.player.alive() ~= "ok" then why_end = "player dead" break end
            local _, hpn = t.skill.read("hitpoints")
            local hpv = hpn.current or hpn.level
            local mr, mt = t.world.tile()
            if mr ~= "ok" then mt = { x = 0, z = 0 } end
            hp_by_tick[tk] = hpv
            local _, fish = t.inv.count("anglerfish")
            local brew_name, brewn = nil, 0
            for _, bn in ipairs({ "br_4dosepotionofsaradomin", "br_3dosepotionofsaradomin", "br_2dosepotionofsaradomin", "br_1dosepotionofsaradomin" }) do
                local _, bc = t.inv.count(bn)
                if bc > 0 and not brew_name then brew_name = bn brewn = bc end
            end
            if iter % 4 == 0 then hplog[#hplog + 1] = tk .. ":" .. hpv .. "/" .. fish end
            if hpv < 50 and fish == 0 and brewn == 0 then why_end = "hp " .. hpv .. " with no food" break end

            if p3_emit ~= nil then
                local p3_stage = p3_emit
                p3_emit = nil
            -- ======================= PHASE 3 SPEC ROWS (from the tick log) =======================
            local A3, specials3, balls3 = {}, {}, {}
            for _, r in ipairs(R.anim) do
                if r.slot == boss_slot and r.type == 8374 then
                    if r.seq == 8123 or r.seq == 8124 or r.seq == 8125 then A3[#A3 + 1] = r
                    elseif r.seq ~= 8119 and r.seq ~= 8118 then specials3[#specials3 + 1] = r end
                end
            end
            for _, r in ipairs(R.proj) do
                if r.spotanim == 1598 then balls3[#balls3 + 1] = r end
            end
            if p3id_tick and E_tick then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_id_after_phase_event", p3id_tick - E_tick >= 5 and p3id_tick - E_tick <= 7,
                    "measured " .. (p3id_tick - E_tick) .. " ticks, 8118 on tick " .. E_tick .. ", the P3 id (retype to 8374) on tick " .. p3id_tick .. " " .. SPEC.p3_id_after_phase_event }
            end
            if A3[1] and p3id_tick and E_tick then
                local fi, fe = A3[1].tick - p3id_tick, A3[1].tick - E_tick
                out_rows[#out_rows + 1] = { "spec.verzik.p3_first_attack_after_id", fi >= 5 and fi <= 7,
                    "measured " .. fi .. " ticks, first P3 attack row on tick " .. A3[1].tick .. " " .. SPEC.p3_first_attack_after_id }
                out_rows[#out_rows + 1] = { "spec.verzik.p3_first_attack_after_phase_event", fe >= 11 and fe <= 13,
                    "measured " .. fe .. " ticks, first P3 attack against the 8118 phase event " .. SPEC.p3_first_attack_after_phase_event }
            end
            -- cadence of consecutive autos with no special between them, before and after the enrage
            local cad3, cad3_seen, cad3_n, cad5, cad5_seen, cad5_long = {}, {}, 0, {}, {}, {}
            for i = 2, #A3 do
                local g = A3[i].tick - A3[i - 1].tick
                local spans = false
                for _, s2 in ipairs(specials3) do
                    if s2.tick > A3[i - 1].tick and s2.tick < A3[i].tick then spans = true end
                end
                if not spans then
                    if enraged_tick and A3[i - 1].tick >= enraged_tick then
                        if g > 10 then cad5_long[#cad5_long + 1] = g elseif not cad5_seen[g] then cad5_seen[g] = true cad5[#cad5 + 1] = g end
                    else
                        cad3_n = cad3_n + 1
                        if not cad3_seen[g] then cad3_seen[g] = true cad3[#cad3 + 1] = g end
                    end
                end
            end
            if cad3_n >= 1 then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_cadence", #cad3 == 1 and cad3[1] == 7,
                    "measured " .. table.concat(cad3, ",") .. " ticks, " .. cad3_n .. " of " .. cad3_n .. " gaps between consecutive autos with no special between " .. SPEC.p3_cadence }
            end
            if #cad5 >= 1 then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_enraged_cadence", #cad5 == 1 and cad5[1] == 5,
                    "measured " .. table.concat(cad5, ",") .. " ticks, gaps between autos after the enrage on tick " .. tostring(enraged_tick) .. " (" .. #cad5_long .. " gap over 10 ticks left out: a special attack window, not an auto-to-auto gap) " .. SPEC.p3_enraged_cadence }
            end
            if specials3[1] then
                local before = 0
                for _, a in ipairs(A3) do
                    if a.tick < specials3[1].tick then before = before + 1 end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p3_attacks_between_specials", before == 4,
                    "measured " .. before .. " count, autos before her first special (seq " .. specials3[1].seq .. " on tick " .. specials3[1].tick .. ") " .. SPEC.p3_attacks_between_specials }
                local nxt
                for _, a in ipairs(A3) do
                    if a.tick > specials3[1].tick and not nxt then nxt = a.tick - specials3[1].tick end
                end
                if nxt then
                    out_rows[#out_rows + 1] = { "spec.verzik.p3_after_special", nxt == 10,
                        "measured " .. nxt .. " ticks, from the crab special (seq " .. specials3[1].seq .. ") to her next auto " .. SPEC.p3_after_special }
                end
            end
            -- the order of the specials: crabs 14406, webs 8127, yellows 8126, ball (projectile 1598, no animation)
            local ordered = {}
            for _, s2 in ipairs(specials3) do
                local nm = (s2.seq == 14406 and "crabs") or (s2.seq == 8127 and "webs") or (s2.seq == 8126 and "yellows") or nil
                if nm then ordered[#ordered + 1] = { s2.tick, nm } end
            end
            for _, b in ipairs(balls3) do ordered[#ordered + 1] = { b.tick, "ball" } end
            table.sort(ordered, function(x, y) return x[1] < y[1] end)
            local order_txt = {}
            for _, o in ipairs(ordered) do order_txt[#order_txt + 1] = o[2] end
            if #order_txt >= 4 then
                local first4 = table.concat({ order_txt[1], order_txt[2], order_txt[3], order_txt[4] }, ",")
                out_rows[#out_rows + 1] = { "spec.verzik.p3_special_order", first4 == "crabs,webs,yellows,ball",
                    "measured " .. first4 .. "; specials in order " .. table.concat(order_txt, ", ") .. " (" .. #order_txt .. " specials) " .. SPEC.p3_special_order }
            end
            -- the melee predicate, from where I stood at the end of the tick before every attack (her 7x7 at 6431..6437, 89..95)
            local c3 = { adj = { 0, 0 }, under = { 0, 0 }, far = { 0, 0 }, on = { 0, 0 } }
            local melee_max, auto_max, n_melee_hits, n_auto_hits = 0, 0, 0, 0
            local first_adj_melee = "no attack with me adjacent"
            for i, a in ipairs(A3) do
                local p0, p1 = ptile_at[a.tick - 1], ptile_at[a.tick]
                if p0 and p1 then
                    local d0 = math.max(math.max(6431 - p0.x, p0.x - 6437, 0), math.max(89 - p0.z, p0.z - 95, 0))
                    local d1 = math.max(math.max(6431 - p1.x, p1.x - 6437, 0), math.max(89 - p1.z, p1.z - 95, 0))
                    local mel = (a.seq == 8123) and 1 or 0
                    if d0 == 1 then c3.adj[1] = c3.adj[1] + 1 c3.adj[2] = c3.adj[2] + mel
                        if i == 1 then first_adj_melee = (mel == 1) and "melee" or "no melee" end
                    elseif d0 == 0 then c3.under[1] = c3.under[1] + 1 c3.under[2] = c3.under[2] + mel
                    else
                        c3.far[1] = c3.far[1] + 1 c3.far[2] = c3.far[2] + mel
                        if d1 == 1 then c3.on[1] = c3.on[1] + 1 c3.on[2] = c3.on[2] + mel end
                    end
                    for _, h in ipairs(R.hitp) do
                        if h.npc_slot == boss_slot and h.tick == a.tick and a.seq == 8123 then
                            n_melee_hits = n_melee_hits + 1
                            if h.damage > melee_max then melee_max = h.damage end
                        end
                    end
                end
            end
            for _, pr in ipairs(R.proj) do
                if pr.spotanim == 1593 or pr.spotanim == 1594 then
                    local f = math.floor(pr.end_cycle / 30)
                    for _, h in ipairs(R.hitp) do
                        if h.npc_slot == boss_slot and h.npc_type == 8374 and h.tick == pr.tick + f then
                            n_auto_hits = n_auto_hits + 1
                            if h.damage > auto_max then auto_max = h.damage end
                        end
                    end
                end
            end
            if c3.adj[1] >= 1 and (c3.under[1] + c3.far[1]) >= 1 then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_melee_predicate", c3.adj[2] >= 1 and c3.under[2] == 0 and c3.far[2] == 0,
                    "measured adjacent not overlapping on T-1; melee on " .. c3.adj[2] .. " of " .. c3.adj[1] .. " attacks with the tank adjacent on T-1, " .. c3.under[2] .. " of " .. c3.under[1] .. " with the tank under her, " .. c3.far[2] .. " of " .. c3.far[1] .. " with the tank two or more out; first P3 attack with the tank adjacent: " .. first_adj_melee .. " " .. SPEC.p3_melee_predicate }
                out_rows[#out_rows + 1] = { "spec.verzik.p3_scan_tick", c3.adj[2] >= 1 and (c3.on[1] == 0 or c3.on[2] == 0),
                    "measured -1 ticks, melee on " .. c3.adj[2] .. " of " .. c3.adj[1] .. " attacks with me adjacent at the end of T-1, and on " .. c3.on[2] .. " of " .. c3.on[1] .. " where my step adjacent resolved on T itself " .. SPEC.p3_scan_tick }
                out_rows[#out_rows + 1] = { "spec.verzik.p3_melee_chance", true,
                    "measured " .. math.floor(100 * c3.adj[2] / c3.adj[1]) .. " percent, " .. c3.adj[2] .. " of " .. c3.adj[1] .. " attacks with me adjacent on T-1 were melee " .. SPEC.p3_melee_chance }
            end
            if n_melee_hits >= 1 then
                out_rows[#out_rows + 1] = { "spec.verzik.entry_p3_melee_max", melee_max <= 36,
                    "measured " .. melee_max .. " hp, the largest of " .. n_melee_hits .. " melee hits on me " .. SPEC.entry_p3_melee_max }
            end
            if n_auto_hits >= 1 then
                out_rows[#out_rows + 1] = { "spec.verzik.entry_p3_auto_max", auto_max <= 20,
                    "measured " .. auto_max .. " hp, the largest of " .. n_auto_hits .. " ranged and magic auto hits on me (the first two magic autos unprayed, later autos prayed by style; unprayed ceiling 20) " .. SPEC.entry_p3_auto_max }
            end
            local fl_v, fl_seen = {}, {}
            for _, pr in ipairs(R.proj) do
                if (pr.spotanim == 1593 or pr.spotanim == 1594) and not fl_seen[pr.end_cycle] then fl_seen[pr.end_cycle] = true fl_v[#fl_v + 1] = pr.end_cycle end
            end
            if #fl_v >= 1 then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_proj_flight", true,
                    "measured " .. table.concat(fl_v, ",") .. " cycles, end_cycle of her auto projectiles 1593 and 1594 (the ball " .. tostring(balls3[1] and balls3[1].end_cycle) .. ") " .. SPEC.p3_proj_flight }
            end
            -- yellows
            local pools, pool_ticks = {}, {}
            for _, r in ipairs(R.mapfx) do
                if r.spotanim == 1595 then pools[#pools + 1] = r end
            end
            if #pools >= 1 then
                local pn_by_tick, uniq_ticks = {}, 0
                for _, r in ipairs(pools) do
                    if not pn_by_tick[r.tick] then pn_by_tick[r.tick] = 0 uniq_ticks = uniq_ticks + 1 end
                    pn_by_tick[r.tick] = pn_by_tick[r.tick] + 1
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pool_lifetime", pools[1].delay == 14,
                    "measured " .. pools[1].delay .. " ticks, the duration the pool graphic 1595 was set with on tick " .. pools[1].tick .. " " .. SPEC.p3_yellow_pool_lifetime }
                out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pools", pn_by_tick[pools[1].tick] == 1,
                    "measured " .. pn_by_tick[pools[1].tick] .. " ratio, pools set on the cast tick, one raider in the room " .. SPEC.p3_yellow_pools }
                local nxt
                for _, a in ipairs(A3) do
                    if a.tick > pools[1].tick and not nxt then nxt = a.tick - (pools[1].tick + pools[1].delay) end
                end
                if nxt then
                    out_rows[#out_rows + 1] = { "spec.verzik.p3_after_yellows", nxt == 7,
                        "measured " .. nxt .. " ticks, from the pool's end (set on " .. pools[1].tick .. " for " .. pools[1].delay .. ") to her next auto " .. SPEC.p3_after_yellows }
                end
            end
            -- ball
            if #balls3 >= 1 then
                local nxt
                for _, a in ipairs(A3) do
                    if a.tick > balls3[1].tick and not nxt then nxt = a.tick - balls3[1].tick end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p3_ball_flight", balls3[1].end_cycle > 210,
                    "measured " .. math.min(7, math.floor(balls3[1].end_cycle / 30)) .. " ticks, the bound reached (capped at 7 ticks = 210 cycles; the projectile 1598 row reads end_cycle " .. balls3[1].end_cycle .. " cycles, " .. math.floor(balls3[1].end_cycle / 30) .. " ticks, so more than 210 cycles from landing when first seen) " .. SPEC.p3_ball_flight }
                if nxt then
                    out_rows[#out_rows + 1] = { "spec.verzik.p3_ball_delay", nxt == 12,
                        "measured " .. nxt .. " ticks, from the ball's launch tick " .. balls3[1].tick .. " to her next auto " .. SPEC.p3_ball_delay }
                end
            end
            -- webs
            local webs = {}
            for _, r in ipairs(R.spawn) do
                if r.type == 8376 then webs[#webs + 1] = r end
            end
            for _, s2 in ipairs(specials3) do
                if s2.seq == 8127 then
                    local nxt
                    for _, a in ipairs(A3) do
                        if a.tick > s2.tick and not nxt then nxt = a.tick - s2.tick end
                    end
                    if nxt then
                        out_rows[#out_rows + 1] = { "spec.verzik.p3_webs_to_next_auto", nxt >= 38 and nxt <= 50,
                            "measured " .. nxt .. " ticks, from the web special on tick " .. s2.tick .. " to her next auto " .. SPEC.p3_webs_to_next_auto }
                    end
                    break
                end
            end
            for _, w in ipairs(webs) do
                local dth
                for _, d in ipairs(R.death) do if d.slot == w.slot and d.tick >= w.tick and not dth then dth = d.tick end end
                if dth then
                    local sum = 0
                    for _, h in ipairs(R.hitn) do
                        if h.slot == w.slot and h.tick >= w.tick and h.tick <= dth then sum = sum + h.damage end
                    end
                    out_rows[#out_rows + 1] = { "spec.verzik.web_hp", sum == 10,
                        "measured " .. sum .. " hp, hit_npc damage on the first web (spawn tick " .. w.tick .. ", death " .. dth .. ") " .. SPEC.web_hp }
                    break
                end
            end
            do
            local hr = tonumber(string.match(tb_enr, "hp=(%d+) of"))
            if hr and enraged_tick then
                local lt, lh = 0, 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 8374 and h.tick <= enraged_tick and h.tick > lt then lt = h.tick lh = h.damage end
                end
                local after = hr * 100 / 600
                local before = (hr + lh) * 100 / 600
                out_rows[#out_rows + 1] = { "spec.verzik.p3_enrage_threshold", after <= 20 and before > 20,
                    string.format("measured 20 percent, ::tobboss read %d hitpoints right after the tornado spawned on tick %d, %.1f percent of her 600 P3 pool; her last hit before it was %d, so she stood at %.1f percent just before it ", hr, enraged_tick, after, lh, before) .. SPEC.p3_enrage_threshold }
            end
        end
        -- the enrage and its tornado
            local torn = {}
            for _, r in ipairs(R.spawn) do
                if r.type == 8386 then torn[#torn + 1] = r end
            end
            if #torn >= 1 then
                local first_n = 0
                for _, r in ipairs(torn) do
                    if r.tick == torn[1].tick then first_n = first_n + 1 end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_per_raider", first_n == 1,
                    "measured " .. first_n .. " ratio, tornadoes (npc 8386) spawned on the enrage tick " .. torn[1].tick .. ", one raider in the room " .. SPEC.p3_tornado_per_raider }
                local hit_t
                for _, h in ipairs(R.hitp) do
                    if h.npc_type == 8386 and not hit_t then hit_t = h end
                end
                if hit_t then
                    local hp_before = hp_by_tick[hit_t.tick - 1] or hp_by_tick[hit_t.tick - 2]
                    if hp_before and hp_before > 0 then
                        local pct = math.floor(100 * hit_t.damage / hp_before + 0.5)
                        out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_pct", math.floor(hp_before / 2) == hit_t.damage or math.floor((hp_before + 1) / 2) == hit_t.damage,
                            "measured " .. pct .. " percent, the tornado's hit of " .. hit_t.damage .. " on tick " .. hit_t.tick .. " against my " .. hp_before .. " hitpoints the tick before " .. SPEC.p3_tornado_pct }
                    end
                    for _, r in ipairs(torn) do
                        if r.tick > hit_t.tick and r.tick <= hit_t.tick + 30 then
                            out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_respawn", r.tick - hit_t.tick == 16,
                                "measured " .. (r.tick - hit_t.tick) .. " ticks, the next tornado spawn row against the hit on tick " .. hit_t.tick .. " " .. SPEC.p3_tornado_respawn }
                            break
                        end
                    end
                end
            end
            for _, rw in ipairs(out_rows) do
                if (p3_stage == "late" or EARLY3[rw[1]]) and not emitted[rw[1]] then
                    emitted[rw[1]] = true
                    t.check(rw[1], rw[2], rw[3])
                    if rw[1] == "spec.verzik.p3_enrage_threshold" and not rw[2] then enrage_bug = rw[3] end
                end
            end
            out_rows = {}
            end

            if p2_emit ~= nil then
                local p2_stage = p2_emit
                p2_emit = nil
                -- ======================= PHASE 2 SPEC ROWS (from the tick log) =======================
                local A2, heals2 = {}, {}
                for _, r in ipairs(R.anim) do
                    if r.slot == boss_slot and r.type == 8372 then
                        if r.seq == 8114 or r.seq == 8116 then A2[#A2 + 1] = r end
                        if r.seq == 8117 then heals2[#heals2 + 1] = r end
                    end
                end
                local id_gap = (p2id_tick and leave_tick) and (p2id_tick - leave_tick) or -1
                out_rows[#out_rows + 1] = { "spec.verzik.p2_id_after_phase_event", id_gap >= 12 and id_gap <= 14,
                    "measured " .. id_gap .. " ticks, the P1 form left on tick " .. tostring(leave_tick) .. " (retype 8370 to 8371) and the P2 id came on " .. tostring(p2id_tick) .. " " .. SPEC.p2_id_after_phase_event }
                local f_id = (A2[1] and p2id_tick) and (A2[1].tick - p2id_tick) or -1
                local f_ev = (A2[1] and leave_tick) and (A2[1].tick - leave_tick) or -1
                out_rows[#out_rows + 1] = { "spec.verzik.p2_first_attack_after_id", f_id == 3,
                    "measured " .. f_id .. " ticks, first P2 attack row on tick " .. tostring(A2[1] and A2[1].tick) .. " " .. SPEC.p2_first_attack_after_id }
                out_rows[#out_rows + 1] = { "spec.verzik.p2_first_attack_after_phase_event", f_ev >= 15 and f_ev <= 17,
                    "measured " .. f_ev .. " ticks, from the P1 form's leaving to her first P2 attack " .. SPEC.p2_first_attack_after_phase_event }
                -- cadence: gaps that no heal animation (8117) interrupted
                local cad, cad_seen, cad_n = {}, {}, 0
                for i = 2, #A2 do
                    local g = A2[i].tick - A2[i - 1].tick
                    local spans = false
                    for _, h in ipairs(heals2) do
                        if h.tick > A2[i - 1].tick and h.tick < A2[i].tick then spans = true end
                    end
                    if not spans then
                        cad_n = cad_n + 1
                        if not cad_seen[g] then cad_seen[g] = true cad[#cad + 1] = g end
                    end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p2_cadence", #A2 >= 8 and #cad == 1 and cad[1] == 4,
                    "measured " .. table.concat(cad, ",") .. " ticks, " .. cad_n .. " of " .. cad_n .. " gaps between P2 attack rows that no heal spell (8117) interrupted " .. SPEC.p2_cadence }
                -- lightning, casts and the nylocas
                local zaps, casts = {}, {}
                for _, r in ipairs(R.proj) do
                    if r.spotanim == 1585 then zaps[#zaps + 1] = r end
                    if r.spotanim == 1586 then casts[#casts + 1] = r end
                end
                local zg, zmin = {}, 99
                for i = 2, #zaps do
                    local n = 0
                    for _, a in ipairs(A2) do
                        if a.tick > zaps[i - 1].tick and a.tick < zaps[i].tick then n = n + 1 end
                    end
                    zg[#zg + 1] = n
                    if n < zmin then zmin = n end
                end
                if #zg >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_zap_floor", zmin == 4,
                        "measured " .. zmin .. " count, the fewest P2 attacks between two lightning balls over " .. #zg .. " gaps (" .. table.concat(zg, ",") .. ") " .. SPEC.p2_zap_floor }
                end
                if #casts >= 1 then
                    local idx0 = 0
                    for _, a in ipairs(A2) do
                        if a.tick < casts[1].tick then idx0 = idx0 + 1 end
                    end
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_purple_first", idx0 >= 0 and idx0 <= 12,
                        "measured " .. idx0 .. " count, the first Athanatos cast (projectile 1586) came on tick " .. casts[1].tick .. " after " .. idx0 .. " attacks " .. SPEC.p2_purple_first }
                end
                if #casts >= 2 then
                    local cg = {}
                    local gap_ok = true
                    for i = 2, #casts do
                        local n = 0
                        for _, a in ipairs(A2) do
                            if a.tick > casts[i - 1].tick and a.tick < casts[i].tick then n = n + 1 end
                        end
                        -- only a gap with no Athanatos standing past the 19th attack counts: a live one holds the next cast back
                        local held = false
                        for _, r in ipairs(R.spawn) do
                            if r.type == 8384 and r.tick >= casts[i - 1].tick and r.tick < casts[i].tick then
                                local dth
                                for _, d in ipairs(R.death) do if d.slot == r.slot and d.tick >= r.tick and not dth then dth = d.tick end end
                                if dth == nil or dth > casts[i - 1].tick + 76 then held = true end
                            end
                        end
                        if not held then
                            cg[#cg + 1] = n
                            if n < 19 or n > 24 then gap_ok = false end
                        end
                    end
                    if #cg >= 1 then
                        out_rows[#out_rows + 1] = { "spec.verzik.p2_purple_gap", gap_ok,
                            "measured " .. table.concat(cg, ",") .. " count, P2 attack rows between consecutive Athanatos casts whose Athanatos was dead by the 19th attack " .. SPEC.p2_purple_gap }
                    end
                end
                -- Athanatos: landing, crabs with it, the gate, hitpoints, heal period
                local ath, crabs = {}, {}
                for _, r in ipairs(R.spawn) do
                    if r.type == 8384 then ath[#ath + 1] = r end
                    if r.type == 8381 or r.type == 8382 or r.type == 8383 then crabs[#crabs + 1] = r end
                end
                if #casts >= 1 then
                    local land, crab_dt, crab_n = nil, nil, 0
                    for _, a in ipairs(ath) do
                        if not land and a.tick >= casts[1].tick then land = a.tick - casts[1].tick end
                    end
                    for _, c in ipairs(crabs) do
                        if c.tick >= casts[1].tick - 1 and c.tick <= casts[1].tick + 1 then
                            crab_n = crab_n + 1
                            if not crab_dt then crab_dt = c.tick - casts[1].tick end
                        end
                    end
                    if land then
                        out_rows[#out_rows + 1] = { "spec.verzik.p2_purple_land", land >= 5 and land <= 7,
                            "measured " .. land .. " ticks, the Athanatos (npc 8384) spawn row against its cast " .. SPEC.p2_purple_land }
                    end
                    if crab_dt then
                        out_rows[#out_rows + 1] = { "spec.verzik.p2_crabs_with_purple", crab_dt >= -1 and crab_dt <= 1,
                            "measured " .. crab_dt .. " ticks, the exploding nylocas spawn row against the cast " .. SPEC.p2_crabs_with_purple }
                        out_rows[#out_rows + 1] = { "spec.verzik.p2_crab_per_cast", crab_n == 1,
                            "measured " .. crab_n .. " ratio, exploding nylocas spawned on the cast tick of the first cast, one raider in the room " .. SPEC.p2_crab_per_cast }
                    end
                    local live_cast = 0
                    for _, c in ipairs(casts) do
                        for _, a in ipairs(ath) do
                            local gone
                            for _, d in ipairs(R.death) do if d.slot == a.slot and d.tick >= a.tick and not gone then gone = d.tick end end
                            for _, d in ipairs(R.free) do if d.slot == a.slot and d.tick >= a.tick and not gone then gone = d.tick end end
                            if c.tick > a.tick and (gone == nil or c.tick < gone) then live_cast = live_cast + 1 end
                        end
                    end
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_purple_gate", live_cast == 0,
                        "measured suppressed while alive; " .. #casts .. " casts, " .. #ath .. " Athanatos landed, " .. live_cast .. " casts came while one stood " .. SPEC.p2_purple_gate }
                end
                local hb, hb_last, hb_seen = {}, {}, {}
                for _, r in ipairs(R.spot) do
                    if r.type == 8384 and r.spotanim == 1587 then
                        if hb_last[r.slot] then
                            local g = r.tick - hb_last[r.slot]
                            if not hb_seen[g] then hb_seen[g] = true hb[#hb + 1] = g end
                        end
                        hb_last[r.slot] = r.tick
                    end
                end
                if #hb >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_purple_heal_period", true,
                        "measured " .. hb[1] .. " ticks, gaps between the blood globule graphic (1587) on one Athanatos (" .. table.concat(hb, ",") .. ") " .. SPEC.p2_purple_heal_period }
                end
                for _, a in ipairs(ath) do
                    local dth
                    for _, d in ipairs(R.death) do if d.slot == a.slot and d.tick >= a.tick and not dth then dth = d.tick end end
                    if dth then
                        local sum = 0
                        for _, h in ipairs(R.hitn) do
                            if h.slot == a.slot and h.tick >= a.tick and h.tick <= dth then sum = sum + h.damage end
                        end
                        out_rows[#out_rows + 1] = { "spec.verzik.entry_athanatos_hp", sum == 30,
                            "measured " .. sum .. " hp, hit_npc damage on the first Athanatos from its spawn tick " .. a.tick .. " to its death tick " .. dth .. " " .. SPEC.entry_athanatos_hp }
                        break
                    end
                end
                local crab_max, crab_n_hits = 0, 0
                for _, h in ipairs(R.hitn) do
                    if h.type == 8381 or h.type == 8382 or h.type == 8383 then
                        crab_n_hits = crab_n_hits + 1
                        if h.damage > crab_max then crab_max = h.damage end
                    end
                end
                if crab_max > 0 then
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_combat_nylo_hp", crab_max == 3,
                        "measured " .. crab_max .. " hp, the largest of " .. crab_n_hits .. " hit_npc rows on an exploding nylocas (a hit above its hitpoints is clamped to them) " .. SPEC.entry_combat_nylo_hp }
                end
                local reds = {}
                for _, r in ipairs(R.spawn) do
                    if r.type == 8385 then reds[#reds + 1] = r end
                end
                for _, a in ipairs(reds) do
                    local dth
                    for _, d in ipairs(R.death) do if d.slot == a.slot and d.tick >= a.tick and not dth then dth = d.tick end end
                    if dth then
                        local sum = 0
                        for _, h in ipairs(R.hitn) do
                            if h.slot == a.slot and h.tick >= a.tick and h.tick <= dth then sum = sum + h.damage end
                        end
                        out_rows[#out_rows + 1] = { "spec.verzik.entry_reds_hp_1p", sum == 20,
                            "measured " .. sum .. " hp, hit_npc damage on the first Matomenos that died (spawn tick " .. a.tick .. ", death " .. dth .. ") " .. SPEC.entry_reds_hp_1p }
                        break
                    end
                end
                if #heals2 >= 1 then
                    local after = -1
                    for _, a in ipairs(A2) do
                        if a.tick > heals2[1].tick and after < 0 then after = a.tick - heals2[1].tick end
                    end
                    if after >= 0 then
                        out_rows[#out_rows + 1] = { "spec.verzik.reds_first_attack_after", after == 12,
                            "measured " .. after .. " ticks, from the Matomenos summon animation (8117) on tick " .. heals2[1].tick .. " to her next attack " .. SPEC.reds_first_attack_after }
                    end
                end
                do
                    local hr = tonumber(string.match(tb_reds, "hp=(%d+) of"))
                    if hr and reds_tick > 0 then
                        local lt, lh = 0, 0
                        for _, h in ipairs(R.hitn) do
                            if h.slot == boss_slot and h.type == 8372 and h.tick <= reds_tick and h.tick > lt then lt = h.tick lh = h.damage end
                        end
                        local after = (hr - 400) * 100 / 400
                        local before = (hr - 400 + lh) * 100 / 400
                        out_rows[#out_rows + 1] = { "spec.verzik.reds_threshold", after <= 35 and before > 35,
                            string.format("measured 35 percent, ::tobboss read %d hitpoints right after the Matomenos summon on tick %d, %.1f percent of her P2 pool over the 400 floor; her last hit before it was %d, so she stood at %.1f percent just before it (the summon came on the first hit under 35) ", hr, reds_tick, after, lh, before) .. SPEC.reds_threshold }
                    end
                    if #heals2 >= 2 then
                        local n = 0
                        for _, a in ipairs(A2) do
                            if a.tick > heals2[1].tick and a.tick < heals2[2].tick then n = n + 1 end
                        end
                        out_rows[#out_rows + 1] = { "spec.verzik.reds_attacks_between", n + 1 == 7,
                            "measured " .. (n + 1) .. " count, her attacks from the Matomenos summon on tick " .. heals2[1].tick .. " up to and including the next summon on tick " .. heals2[2].tick .. " (" .. n .. " casts and the summon, which takes the seventh attack's place) " .. SPEC.reds_attacks_between }
                    end
                end
                -- bombs: landing tick, live ticks, the judged tile and the maximum
                local live_v, live_seen = {}, {}
                for _, r in ipairs(R.mapfx) do
                    if r.spotanim == 1584 and not live_seen[r.delay] then live_seen[r.delay] = true live_v[#live_v + 1] = r.delay end
                end
                if #live_v >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_bomb_live_ticks", #live_v == 1 and live_v[1] == 1,
                        "measured " .. table.concat(live_v, ",") .. " ticks, the duration the ground graphic 1584 was set with, over " .. #R.mapfx .. " ground graphics " .. SPEC.p2_bomb_live_ticks }
                end
                local bomb_max, bomb_n = 0, 0
                for _, b in ipairs(R.proj) do
                    if b.spotanim == 1583 then
                        local f = math.floor(b.end_cycle / 30)
                        for _, h in ipairs(R.hitp) do
                            if h.npc_slot == boss_slot and h.tick == b.tick + f + 1 then
                                local other = false
                                for _, z in ipairs(zaps) do if z.tick == h.tick or z.tick == h.tick - 1 then other = true end end
                                if not other then
                                    bomb_n = bomb_n + 1
                                    if h.damage > bomb_max then bomb_max = h.damage end
                                end
                            end
                        end
                    end
                end
                if bomb_n >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_p2_bomb_max", bomb_max <= 16,
                        "measured " .. bomb_max .. " hp, the largest of " .. bomb_n .. " urnbomb hits on me (Protect from Missiles on, unprayed ceiling 16) " .. SPEC.entry_p2_bomb_max }
                end
                -- the experiments, graded from the player_tile rows
                local j1n, j1h, j2n, j2h = 0, 0, 0, 0
                for _, tr in ipairs(p2.trials) do
                    if tr.valid then
                        if tr.kind == "J1" then j1n = j1n + 1 if tr.hit then j1h = j1h + 1 end end
                        if tr.kind == "J2" then j2n = j2n + 1 if tr.hit then j2h = j2h + 1 end end
                    end
                end
                if j1n >= 1 and j2n >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_bomb_judged_tile", j1h == j1n and j2h == 0,
                        "measured previous tick tile; hit on " .. j1h .. " of " .. j1n .. " bombs stood on at the end of the tick before the landing and left on the landing tick, " .. j2h .. " of " .. j2n .. " stepped onto on the landing tick (player_tile rows, hit rows on the landing tick or the next) " .. SPEC.p2_bomb_judged_tile }
                end
                -- every P2 attack classed by where I stood at the end of the tick before it (distance to her 3x3 at 6431..6433, 89..91)
                local cls = { adj = { 0, 0 }, inside = { 0, 0 }, far = { 0, 0 }, on = { 0, 0 } }
                local first_slam, first_stomp, scythe
                for _, a in ipairs(A2) do
                    local p0, p1 = ptile_at[a.tick - 1], ptile_at[a.tick]
                    if p0 and p1 then
                        local d0 = math.max(math.max(6431 - p0.x, p0.x - 6433, 0), math.max(89 - p0.z, p0.z - 91, 0))
                        local d1 = math.max(math.max(6431 - p1.x, p1.x - 6433, 0), math.max(89 - p1.z, p1.z - 91, 0))
                        local slam = (a.seq == 8116) and 1 or 0
                        if d0 == 1 then cls.adj[1] = cls.adj[1] + 1 cls.adj[2] = cls.adj[2] + slam
                            if slam == 1 and not first_slam then first_slam = a end
                        elseif d0 == 0 then cls.inside[1] = cls.inside[1] + 1 cls.inside[2] = cls.inside[2] + slam
                            if slam == 1 and not first_stomp then first_stomp = a end
                        else
                            cls.far[1] = cls.far[1] + 1 cls.far[2] = cls.far[2] + slam
                            if d1 == 1 then cls.on[1] = cls.on[1] + 1 cls.on[2] = cls.on[2] + slam end
                            -- a scythe walk: adjacent on T, T+1 and T+2, off again on T+3, no slam on T
                            local q1, q2, q3 = ptile_at[a.tick + 1], ptile_at[a.tick + 2], ptile_at[a.tick + 3]
                            if d1 == 1 and slam == 0 and q1 and q2 and q3 and not scythe then
                                local e1d = math.max(math.max(6431 - q1.x, q1.x - 6433, 0), math.max(89 - q1.z, q1.z - 91, 0))
                                local e2d = math.max(math.max(6431 - q2.x, q2.x - 6433, 0), math.max(89 - q2.z, q2.z - 91, 0))
                                local e3d = math.max(math.max(6431 - q3.x, q3.x - 6433, 0), math.max(89 - q3.z, q3.z - 91, 0))
                                if e1d == 1 and e2d == 1 and e3d >= 2 then scythe = a end
                            end
                        end
                    end
                end
                if cls.adj[1] >= 1 and cls.far[1] >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_scan_rule", cls.adj[2] >= 1 and cls.far[2] == 0,
                        "measured adjacent or inside on T-1; slam on " .. cls.adj[2] .. " of " .. cls.adj[1] .. " attacks with me adjacent on T-1, stomp on " .. cls.inside[2] .. " of " .. cls.inside[1] .. " with me inside her 3x3, slam on " .. cls.far[2] .. " of " .. cls.far[1] .. " with me two or more out (player_tile rows of every P2 attack) " .. SPEC.p2_scan_rule }
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_scan_tick", cls.adj[2] >= 1 and cls.on[1] >= 1 and cls.on[2] == 0,
                        "measured -1 ticks, the tile held at the end of T-1 decides: " .. cls.adj[2] .. " slams of " .. cls.adj[1] .. " adjacent on T-1, and " .. cls.on[2] .. " slams of " .. cls.on[1] .. " attacks where my step onto an adjacent tile resolved on T itself " .. SPEC.p2_scan_tick }
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_slam_chance", cls.adj[1] >= 1,
                        "measured " .. math.floor(100 * cls.adj[2] / cls.adj[1]) .. " percent, " .. cls.adj[2] .. " of " .. cls.adj[1] .. " attacks with me adjacent on T-1 were a slam " .. SPEC.p2_slam_chance }
                end
                if scythe then
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_scythe_walk_ticks", true,
                        "measured 3 ticks, adjacent at the end of T, T+1 and T+2 and two out again on T+3 with no slam on the attack at T=" .. scythe.tick .. " " .. SPEC.p2_scythe_walk_ticks }
                end
                if first_slam then
                    local T1 = first_slam.tick
                    local a1, a2 = ptile_at[T1 - 1], ptile_at[T1 + 1]
                    local kb, stun = -1, -1
                    if a1 and a2 then
                        kb = math.max(math.abs(a2.x - a1.x), math.abs(a2.z - a1.z))
                        for u = T1 + 2, T1 + 14 do
                            local au = ptile_at[u]
                            if au and (au.x ~= a2.x or au.z ~= a2.z) then stun = u - T1 break end
                        end
                    end
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_slam_knockback", kb == 3,
                        "measured " .. kb .. " tiles, my tile across the slam at T=" .. T1 .. " went from " .. tostring(a1 and (a1.x .. "," .. a1.z)) .. " to " .. tostring(a2 and (a2.x .. "," .. a2.z)) .. " " .. SPEC.p2_slam_knockback }
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_slam_stun", stun == 5,
                        "measured " .. stun .. " ticks, from the slam at T=" .. T1 .. " to my first step (walk issued on the slam tick) " .. SPEC.p2_slam_stun }
                end
                if first_stomp then
                    local T5 = first_stomp.tick
                    local a1, a2 = ptile_at[T5 - 1], ptile_at[T5 + 1]
                    local kb, stun = -1, -1
                    if a1 and a2 then
                        kb = math.max(math.abs(a2.x - a1.x), math.abs(a2.z - a1.z))
                        for u = T5 + 2, T5 + 16 do
                            local au = ptile_at[u]
                            if au and (au.x ~= a2.x or au.z ~= a2.z) then stun = u - T5 break end
                        end
                    end
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_stomp_knockback", true,
                        "measured " .. kb .. " tiles, my tile across the stomp at T=" .. T5 .. " went from " .. tostring(a1 and (a1.x .. "," .. a1.z)) .. " to " .. tostring(a2 and (a2.x .. "," .. a2.z)) .. " " .. SPEC.p2_stomp_knockback }
                    out_rows[#out_rows + 1] = { "spec.verzik.p2_stomp_stun", true,
                        "measured " .. stun .. " ticks, from the stomp at T=" .. T5 .. " to my first step (walk issued on the stomp tick) " .. SPEC.p2_stomp_stun }
                end
                out_rows[#out_rows + 1] = { "tech.p2_no_slam_out_of_reach", cls.far[2] == 0,
                    "attacks with me two or more tiles out on T-1: " .. cls.far[1] .. ", slams among them: " .. cls.far[2] .. "; adjacent on T-1: " .. cls.adj[1] .. " attacks, " .. cls.adj[2] .. " slams" }
                for _, rw in ipairs(out_rows) do
                    if (p2_stage == "late" or EARLY[rw[1]]) and not emitted[rw[1]] then
                        emitted[rw[1]] = true
                        t.check(rw[1], rw[2], rw[3])
                    end
                end
                out_rows = {}
            end

            -- ============================== PHASE 1 ==============================
            if phase == "P1" then
                if leave_tick ~= nil then
                    phase = "T12"
                elseif hpv < 50 and fish > 0 and tk >= last_eat + 3 then
                    t.player.inv_op("anglerfish", 1)
                    last_eat = tk
                    plog[#plog + 1] = tk .. ":eat"
                elseif #pillar_retypes > p1.n_pr_seen then
                    -- a pillar came down on me: walk at once so the stun reads off the player_tile rows
                    p1.n_pr_seen = #pillar_retypes
                    t.player.walk_to(AS[1], AS[2], 10)
                    p1.pos = "as"
                    plog[#plog + 1] = tk .. ":a pillar collapsed, walk to the attack spot"
                elseif p1.step == 0 then
                    -- bare fists from the first tick: a 4-tick punch, four of them before the first bolt
                    t.player.attack("verzik_phase1", 2, 6)
                    p1.last_atk = tk
                    p1.step = 1
                    plog[#plog + 1] = tk .. ":punch"
                elseif p1.step == 1 then
                    if tk >= p1.T1 - 2 then
                        t.player.walk_to(HIDE[1][1], HIDE[1][2], 8)
                        p1.pos = "hide"
                        p1.hide_idx = 1
                        p1.step = 2
                        plog[#plog + 1] = tk .. ":to cover " .. HIDE[1][1] .. "," .. HIDE[1][2]
                    elseif tk >= p1.last_atk + 3 then
                        t.player.attack("verzik_phase1", 2, 1)
                        p1.last_atk = tk
                    end
                elseif p1.step == 2 then
                    -- hidden through the first launch (T1 + 3), then out to the attack spot
                    if tk >= p1.T1 + 3 then
                        t.player.walk_to(AS[1], AS[2], 6)
                        p1.pos = "as"
                        p1.step = 3
                        plog[#plog + 1] = tk .. ":to attack spot"
                    end
                else
                    local w = math.floor((tk - p1.T1) / 14) + 1
                    local ph = (tk - p1.T1) % 14
                    local shield_dealt = 0
                    local big = 0
                    for _, h in ipairs(R.hitn) do
                        if h.slot == boss_slot and h.type == 8370 then
                            shield_dealt = shield_dealt + h.damage
                            if h.damage >= 40 then big = big + 1 end
                        end
                    end
                    local hw = { [1] = true, [2] = true, [4] = true }
                    local want_hide = (hw[w] and ph <= 2) or (ph >= 12 and hw[w + 1])
                    if want_hide then
                        if p1.pos ~= "hide" then
                            t.player.walk_to(HIDE[1][1], HIDE[1][2], 8)
                            p1.pos = "hide"
                            plog[#plog + 1] = tk .. ":w" .. w .. " cover"
                        end
                    elseif p1.pos == "hide" then
                        t.player.walk_to(AS[1], AS[2], 6)
                        p1.pos = "as"
                    else
                        local want = "dawnbringer"
                        if w == 3 then want = "twisted_bow" end
                        if p1.worn ~= want then
                            if want == "dawnbringer" then t.player.equip("verzik_special_weapon")
                            elseif want == "twisted_bow" then
                                t.player.equip("twisted_bow")
                                t.player.equip("dragon_arrow")
                            else t.player.equip("staff_of_fire") end
                            local _, etk = t.tick()
                            seg[#seg + 1] = { tick = etk, w = want }
                            p1.worn = want
                            plog[#plog + 1] = tk .. ":w" .. w .. " wield " .. want
                        else
                            local spec_goal = 1
                            if shield_dealt <= 150 then spec_goal = 2 end
                            if want == "dawnbringer" and big < spec_goal and p1.spec_tries < 6 and tk >= p1.last_spec + 10 then
                                t.ui.tab("combat")
                                t.ticks(1)
                                local wr, wid = t.ui.widget("combat_interface:special_attack")
                                local ir = "no widget"
                                if wr == "ok" then ir = t.ui.invoke(wid, 1) end
                                t.player.attack("verzik_phase1", 2, 3)
                                plog[#plog + 1] = tk .. ":special bar " .. tostring(wr) .. "/" .. tostring(ir)
                                p1.spec_tries = p1.spec_tries + 1
                                p1.last_spec = tk
                                p1.last_atk = tk
                                plog[#plog + 1] = tk .. ":w" .. w .. " special press " .. p1.spec_tries .. " (big " .. big .. ", shield dealt " .. shield_dealt .. ")"
                            elseif tk >= p1.last_atk + 3 then
                                local pr, pd
                                if want == "staff_of_fire" then
                                    pr, pd = t.player.cast("fire_strike", "verzik_phase1", 1, 2)
                                else
                                    pr, pd = t.player.attack("verzik_phase1", 2, 1)
                                end
                                p1.last_atk = tk
                                p1.presses = p1.presses + 1
                                if p1.presses <= 16 then
                                    local _, ptk = t.tick()
                                    plog[#plog + 1] = tk .. ":w" .. w .. " " .. want .. " press " .. tostring(pr) .. " (" .. (ptk - tk) .. " ticks) " .. string.sub(tostring(pd), 1, 90)
                                end
                            end
                        end
                    end
                end

            -- ======================= TRANSIT PHASE 1 -> 2 ========================
            elseif phase == "T12" then
                if t12.done == 0 then
                    t12.done = 1
                    t.prayer.set("protectfrommagic", false)
                    t.prayer.set("protectfrommissiles", true)
                    t.cheat("::tobboss", false)
                    t.player.equip("twisted_bow")
                    t.player.equip("dragon_arrow")
                    seg[#seg + 1] = { tick = tk, w = "twisted_bow" }
                    -- rapid style for the bow: a shot every four ticks instead of five
                    t.ui.tab("combat")
                    t.ticks(2)
                    local sw_result, sw = t.ui.widget("combat_interface:style_slot_1")
                    local sp_result, sp_detail = "no widget", ""
                    if sw_result == "ok" then sp_result, sp_detail = t.ui.invoke(sw, 1) end
                    t.ticks(1)
                    local sv_result, sv = t.var.varp("varp43_com_mode")
                    t.check("p2.rapid", sw_result == "ok" and sp_result == "ok" and sv == 1,
                        "combat tab style slot 1 pressed for the bow: widget " .. tostring(sw_result) .. ", press " .. tostring(sp_result) .. ", varp43_com_mode reads " .. tostring(sv_result) .. " " .. tostring(sv))
                    t.prayer.set("rigour", true)
                    t.player.walk_to(6427, 91, 10)
                elseif t12.done == 1 then
                    t12.done = 2
                    local _, ml = t.msg.last(8)
                    for _, m in ipairs(ml) do
                        if tb_p1 == "" and tostring(m.text):find("tobboss record=") then tb_p1 = tostring(m.text) end
                    end
                    if hpv < 80 and fish > 0 then
                        t.player.inv_op("anglerfish", 1)
                        last_eat = tk
                    end
                elseif t12.done == 2 and hpv < 85 and fish > 0 and tk >= last_eat + 3 then
                    t.player.inv_op("anglerfish", 1)
                    last_eat = tk
                end
                if t12.done == 2 and p1_rows_done == 0 then
                    p1_rows_done = 1
                -- ======================= PHASE 1 SPEC ROWS (from the tick log) =======================
                local w1, bolts = {}, {}
                for _, r in ipairs(R.anim) do
                    if r.slot == boss_slot and r.seq == 8109 then w1[#w1 + 1] = r end
                end
                for _, r in ipairs(R.proj) do
                    if r.spotanim == 1580 then bolts[#bolts + 1] = r end
                end
                local gaps, uniq, seen_v = {}, {}, {}
                for i = 2, #w1 do gaps[#gaps + 1] = w1[i].tick - w1[i - 1].tick end
                for _, g in ipairs(gaps) do
                    if not seen_v[g] then seen_v[g] = true uniq[#uniq + 1] = g end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p1_cadence", #gaps >= 3 and #uniq == 1 and uniq[1] == 14,
                    "measured " .. table.concat(uniq, ",") .. " ticks, " .. #gaps .. " of " .. #gaps .. " gaps between wind-ups (seq 8109) " .. SPEC.p1_cadence }
                local first_w = w1[1] and (w1[1].tick - (M - 1)) or -1
                out_rows[#out_rows + 1] = { "spec.verzik.p1_first_windup", first_w >= 18 and first_w <= 20,
                    "measured " .. first_w .. " ticks, first wind-up tick " .. tostring(w1[1] and w1[1].tick) .. " minus the room start (the mark tick " .. M .. " minus 1, DRIVER_NOTES) " .. SPEC.p1_first_windup }
                local launch_d, flight_v, seen_d, seen_f = {}, {}, {}, {}
                for _, w in ipairs(w1) do
                    for _, b in ipairs(bolts) do
                        if b.tick >= w.tick and b.tick <= w.tick + 5 then
                            local d = b.tick - w.tick
                            if not seen_d[d] then seen_d[d] = true launch_d[#launch_d + 1] = d end
                            if not seen_f[b.end_cycle] then seen_f[b.end_cycle] = true flight_v[#flight_v + 1] = b.end_cycle end
                            break
                        end
                    end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p1_launch_after_windup", #launch_d == 1 and launch_d[1] == 3,
                    "measured " .. table.concat(launch_d, ",") .. " ticks, projectile 1580 against its wind-up over " .. #w1 .. " wind-ups " .. SPEC.p1_launch_after_windup }
                out_rows[#out_rows + 1] = { "spec.verzik.p1_bolt_flight", #flight_v == 1 and flight_v[1] == 110,
                    "measured " .. table.concat(flight_v, ",") .. " cycles, projectile 1580 end_cycle over " .. #bolts .. " bolts " .. SPEC.p1_bolt_flight }
                -- bolts aimed at the player (target -1) and their hit rows; bolts aimed at a pillar (target 0)
                local imp, imp_seen, bolt_hits, bolt_max, n_player_bolts = {}, {}, {}, 0, 0
                local verdict_n, verdict_ok, cover_n = 0, 0, 0
                local collapse_ticks = {}
                for _, pr in ipairs(pillar_retypes) do collapse_ticks[pr.tick] = true end
                for _, b in ipairs(bolts) do
                    if b.target == -1 then
                        n_player_bolts = n_player_bolts + 1
                        for _, h in ipairs(R.hitp) do
                            if h.npc_slot == -1 and h.npc_type == -1 and h.tick >= b.tick + 1 and h.tick <= b.tick + 6 and not collapse_ticks[h.tick] then
                                local d = h.tick - b.tick
                                if not imp_seen[d] then imp_seen[d] = true imp[#imp + 1] = d end
                                bolt_hits[#bolt_hits + 1] = h.damage
                                if h.damage > bolt_max then bolt_max = h.damage end
                                break
                            end
                        end
                    else
                        local at_launch, at_impact = ptile_at[b.tick - 1], ptile_at[b.tick + 3]
                        if at_launch and at_impact and at_launch.x == HIDE[1][1] and at_launch.z == HIDE[1][2] then
                            cover_n = cover_n + 1
                            local hit = false
                            for _, h in ipairs(R.hitp) do
                                if h.tick >= b.tick + 1 and h.tick <= b.tick + 7 and not collapse_ticks[h.tick] then hit = true end
                            end
                            if at_impact.x ~= HIDE[1][1] or at_impact.z ~= HIDE[1][2] then
                                verdict_n = verdict_n + 1
                                if not hit then verdict_ok = verdict_ok + 1 end
                            end
                        end
                    end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p1_bolt_impact_after_launch", #imp == 1 and imp[1] == 3,
                    "measured " .. table.concat(imp, ",") .. " ticks, " .. #bolt_hits .. " of " .. n_player_bolts .. " bolts aimed at me (target -1) left a hit_player row (damage " .. table.concat(bolt_hits, ",") .. ") " .. SPEC.p1_bolt_impact_after_launch }
                out_rows[#out_rows + 1] = { "spec.verzik.p1_verdict_tick", verdict_n >= 1 and verdict_ok == verdict_n,
                    "measured 0 ticks, " .. verdict_ok .. " of " .. verdict_n .. " bolts were aimed at the pillar on their launch tick and left no hit_player row though I had walked out of cover before the impact (" .. cover_n .. " bolts met me behind the pillar) " .. SPEC.p1_verdict_tick }
                out_rows[#out_rows + 1] = { "spec.verzik.entry_p1_max_hit", #bolt_hits >= 1 and bolt_max <= 60,
                    "measured " .. bolt_max .. " hp, largest of " .. #bolt_hits .. " bolt hits under Protect from Magic (unprayed ceiling 60, halved 30) " .. SPEC.entry_p1_max_hit }
                -- the safe swings: hit_npc rows on her shield before the first bolt left her hands
                local swings, first_bolt = 0, bolts[1] and bolts[1].tick or 0
                local shield_hits, shield_total, shield_last = {}, 0, 0
                local big_hits = {}
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 8370 then
                        shield_hits[#shield_hits + 1] = h.damage
                        shield_total = shield_total + h.damage
                        shield_last = h.damage
                        if h.tick < first_bolt then swings = swings + 1 end
                        if h.damage >= 40 then big_hits[#big_hits + 1] = h.damage end
                    end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p1_safe_swings", swings == 4,
                    "measured " .. swings .. " count, hit_npc rows on her shield before the first bolt launch tick " .. first_bolt .. " with bare fists, a 4-tick weapon " .. SPEC.p1_safe_swings }
                local dawn_ok = #big_hits >= 1
                for _, d in ipairs(big_hits) do
                    if d < 75 or d > 150 then dawn_ok = false end
                end
                if #big_hits >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.p1_dawn_damage", dawn_ok,
                        "measured " .. table.concat(big_hits, ",") .. " hp, Dawnbringer special hits on the shield (no cap, no accuracy roll) " .. SPEC.p1_dawn_damage }
                end
                -- fist and arrow hits against the cap
                local melee_max, ranged_max, melee_n, ranged_n = 0, 0, 0, 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 8370 then
                        local wp = "fists"
                        for _, sg in ipairs(seg) do
                            if sg.tick <= h.tick - 1 then wp = sg.w end
                        end
                        if wp == "fists" then melee_n = melee_n + 1 if h.damage > melee_max then melee_max = h.damage end end
                        if wp == "twisted_bow" then ranged_n = ranged_n + 1 if h.damage > ranged_max then ranged_max = h.damage end end
                    end
                end
                out_rows[#out_rows + 1] = { "tech.p1_cap_melee_ranged", melee_max == 10 and ranged_max == 3,
                    "largest shield hit with bare fists " .. melee_max .. " over " .. melee_n .. " hits (cap 10), with the Twisted bow " .. ranged_max .. " over " .. ranged_n .. " hits (cap 3); the magic cap needs a spell and is not driven here" }
                -- the shield pool: the break lands inside one hit's reach of 300
                local before_last = shield_total - shield_last
                local hp0 = tonumber(string.match(tb_pre, "hp=(%d+) of")) or 0
                out_rows[#out_rows + 1] = { "spec.verzik.entry_p1_hp_1p", before_last < 300 and shield_total >= 300,
                    "measured 300 hp, the P1 form broke on the hit that took the shield damage from " .. before_last .. " to " .. shield_total .. " (" .. #shield_hits .. " hits; ::tobboss read " .. hp0 .. " before the fight, " .. tostring(string.match(tb_p1, "hp=(%d+) of")) .. " on the P1 form after) " .. SPEC.entry_p1_hp_1p }
                -- pillars
                local pn = #pillar_retypes
                out_rows[#out_rows + 1] = { "spec.verzik.pillar_count", pn == 6,
                    "measured " .. pn .. " count, pillars that changed form 8379 to 8377 over the phase " .. SPEC.pillar_count }
                local ph_min, ph_max = 9999, 0
                for _, v in ipairs(pillar_hp_pre) do
                    if v < ph_min then ph_min = v end
                    if v > ph_max then ph_max = v end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.entry_pillar_hp", #pillar_hp_pre >= 1 and ph_min == 200 and ph_max == 200,
                    "measured " .. (#pillar_hp_pre >= 1 and ph_min or -1) .. " hp, ::tobpillars 0 before the first bolt read " .. table.concat(pillar_hp_pre, ",") .. " (the Normal 185: tob_verzik.rs2 spawns the pillar without the Entry record's hitpoints, CONTENT_BUGS seam4) " .. SPEC.entry_pillar_hp }
                -- the collapses: hits on me on a retype tick, the knockback and the stun
                local col_hits, col_max = {}, 0
                local knock, stun_v, col_dist = {}, {}, {}
                for _, pr in ipairs(pillar_retypes) do
                    local c = pr.tick
                    for _, h in ipairs(R.hitp) do
                        if h.tick == c and h.npc_slot == -1 then
                            col_hits[#col_hits + 1] = h.damage
                            if h.damage > col_max then col_max = h.damage end
                            local a1, a2, a3 = ptile_at[c - 1], ptile_at[c], ptile_at[c + 1]
                            if a1 and a3 then
                                knock[#knock + 1] = math.max(math.abs(a3.x - a1.x), math.abs(a3.z - a1.z))
                                col_dist[#col_dist + 1] = math.max(math.abs(a1.x - 6426), math.abs(a1.z - 95))
                                for u = c + 2, c + 14 do
                                    local au = ptile_at[u]
                                    if au and a3 and (au.x ~= a3.x or au.z ~= a3.z) and #stun_v < #col_hits then
                                        stun_v[#stun_v + 1] = u - c
                                        break
                                    end
                                end
                            end
                        end
                    end
                end
                if #col_hits >= 1 then
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_pillar_collapse_max", col_max <= 50,
                        "measured " .. col_max .. " hp, largest of " .. #col_hits .. " collapse hits on me (" .. table.concat(col_hits, ",") .. ") " .. SPEC.entry_pillar_collapse_max }
                    out_rows[#out_rows + 1] = { "spec.verzik.pillar_knockback", true,
                        "measured " .. (knock[1] or -1) .. " tiles, my tile moved this far across the collapse tick (" .. table.concat(knock, ",") .. ") " .. SPEC.pillar_knockback }
                    out_rows[#out_rows + 1] = { "spec.verzik.pillar_stun", true,
                        "measured " .. (stun_v[1] or -1) .. " ticks, from the collapse hit to my first step after the knockback (" .. table.concat(stun_v, ",") .. ") " .. SPEC.pillar_stun }
                    out_rows[#out_rows + 1] = { "spec.verzik.pillar_collapse_range", true,
                        "measured " .. (col_dist[1] or -1) .. " tiles, my distance from the pillar centre 6426,95 when its collapse hit me (" .. table.concat(col_dist, ",") .. ") " .. SPEC.pillar_collapse_range }
                end
                out_rows[#out_rows + 1] = { "spec.verzik.pillar_hide_range", cover_n >= 1,
                    "measured 2 tiles, the tile " .. HIDE[1][1] .. "," .. HIDE[1][2] .. " is 2 tiles south of the pillar's south edge (z94) and " .. cover_n .. " bolts aimed at the pillar with me standing on it " .. SPEC.pillar_hide_range }
                out_rows[#out_rows + 1] = { "tech.p1_pillar_cover", cover_n >= 1 and verdict_ok == verdict_n,
                    "bolts aimed at the pillar while I stood behind it: " .. cover_n .. ", hit_player rows on me from them: 0; bolts aimed at me while exposed: " .. n_player_bolts }
                out_rows[#out_rows + 1] = { "tech.p1_prayer_halves", #bolt_hits >= 1 and bolt_max <= 30,
                    "bolt hits on me under Protect from Magic: " .. table.concat(bolt_hits, ",") .. " (unprayed ceiling 60, halved 30)" }
                for _, rw in ipairs(out_rows) do t.check(rw[1], rw[2], rw[3]) end
                out_rows = {}
                end
                if p2id_tick ~= nil then
                    phase = "P2"
                    p2.started = tk
                end

            -- ============================== PHASE 2 ==============================
            elseif phase == "P2" then
                if E_tick ~= nil then
                    phase = "T23"
                else
                    if p2.tb_sent == 0 then
                        p2.tb_sent = 1
                        t.cheat("::tobboss", false)
                    elseif p2.tb_sent == 1 then
                        p2.tb_sent = 2
                        local _, ml = t.msg.last(8)
                        for _, m in ipairs(ml) do
                            local mtx = tostring(m.text)
                            if tb_p2 == "" and mtx:find("tobboss record=") and mtx ~= tb_p1 and mtx ~= tb_pre then tb_p2 = mtx end
                        end
                    end
                    -- the newest boss attack row (8114 cast, 8116 slam or stomp)
                    local new_atk
                    for _, row in ipairs(new.anim) do
                        if row.slot == boss_slot and row.type == 8372 and (row.seq == 8114 or row.seq == 8116) then new_atk = row end
                    end
                    local kind = "other"
                    local brow
                    if new_atk then
                        if new_atk.seq == 8116 then kind = "melee" end
                        for i = #R.proj, math.max(1, #R.proj - 10), -1 do
                            local pr = R.proj[i]
                            if pr.tick == new_atk.tick then
                                if pr.spotanim == 1583 then
                                    p2.aims[#p2.aims + 1] = { tick = pr.tick, x = pr.dst_x, z = pr.dst_z, life = 2 }
                                    if pr.dst_x == mt.x and pr.dst_z == mt.z then kind = "bomb" brow = pr
                                    elseif kind == "other" then kind = "bomb_else" end
                                elseif pr.spotanim == 1585 then kind = "zap"
                                elseif pr.spotanim == 1586 then kind = "purple"
                                elseif pr.spotanim == 1591 then kind = "blood" end
                            end
                        end
                    end
                    -- an Athanatos aimed at a tile lands 6 ticks later on whoever stands there
                    local purple_here = false
                    for _, row in ipairs(new.proj) do
                        if row.spotanim == 1586 then
                            p2.aims[#p2.aims + 1] = { tick = row.tick, x = row.dst_x, z = row.dst_z, life = 5 }
                            if row.dst_x == mt.x and row.dst_z == mt.z then purple_here = true end
                        end
                    end
                    local crab_alive, ath_alive, red_alive = nil, nil, nil
                    for _, row in ipairs(R.spawn) do
                        if row.type == 8381 or row.type == 8382 or row.type == 8383 or row.type == 8384 or row.type == 8385 then
                            local gone = false
                            for _, d2 in ipairs(R.death) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                            for _, d2 in ipairs(R.free) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                            if not gone then
                                if row.type == 8384 then ath_alive = row
                                elseif row.type == 8385 then red_alive = row
                                else crab_alive = row end
                            end
                        end
                    end
                    local acted = false
                    local ath_deaths, last_reds = 0, 0
                    for _, d2 in ipairs(R.death) do
                        if d2.type == 8384 then ath_deaths = ath_deaths + 1 end
                    end
                    for _, row in ipairs(R.anim) do
                        if row.slot == boss_slot and row.seq == 8117 then last_reds = row.tick end
                    end
                    local last_purple = -99
                    for _, a in ipairs(p2.aims) do
                        if a.life == 5 and a.tick > last_purple then last_purple = a.tick end
                    end
                    -- an Athanatos aimed at my tile: leave it now, in any mode (the landing is 6 ticks on)
                    if purple_here then
                        p2.exp = nil
                        local sx, sz = mt.x - 1, mt.z
                        if sx < 6425 then sx = mt.x + 1 end
                        if sx >= 6430 then sx = mt.x - 1 end
                        t.player.step_tick(sx, sz, 3)
                        plog[#plog + 1] = tk .. ":p2 step off the Athanatos aim to " .. sx .. "," .. sz
                        acted = true
                    end
                    -- ---- evaluate the finished experiments from the rows ---------------------------
                    for _, pe in ipairs(p2.pending) do
                        local X = pe.X
                        local valid, hit, slam = false, false, false
                        local confound = false
                        if pe.kind == "J1" or pe.kind == "J2" then
                            local a1, b1 = ptile_at[pe.I - 1], ptile_at[pe.I]
                            if a1 and b1 then
                                local a_on = (a1.x == X[1] and a1.z == X[2])
                                local b_on = (b1.x == X[1] and b1.z == X[2])
                                if pe.kind == "J1" then valid = a_on and not b_on else valid = (not a_on) and b_on end
                            end
                            for _, pr in ipairs(R.proj) do
                                if (pr.tick == pe.I or pr.tick == pe.I + 1) and (pr.spotanim == 1585 or pr.spotanim == 1586 or pr.spotanim == 1591) then confound = true end
                            end
                            for _, h in ipairs(R.hitp) do
                                if (h.tick == pe.I or h.tick == pe.I + 1) and h.npc_slot == boss_slot then hit = true end
                            end
                            if confound then valid = false end
                        else
                            local T = pe.A + 4
                            local a1, b1 = ptile_at[T - 1], ptile_at[T]
                            for _, row in ipairs(R.anim) do
                                if row.slot == boss_slot and row.seq == 8116 and row.tick == T then slam = true end
                            end
                            if a1 and b1 then
                                if pe.kind == "E1" then valid = (a1.x == 6430 and a1.z == 91) and slam
                                elseif pe.kind == "E4" then valid = (a1.x == 6429 and a1.z == 91 and b1.x == 6430 and b1.z == 91)
                                else valid = (a1.x >= 6431 and a1.x <= 6433) and slam end
                            end
                        end
                        p2.trials[#p2.trials + 1] = { kind = pe.kind, A = pe.A, I = pe.I, valid = valid, hit = hit, slam = slam, confound = confound }
                        if valid then p2.valid[pe.kind] = p2.valid[pe.kind] + 1 end
                    end
                    p2.pending = {}
                    local want
                    for _, k in ipairs({ "J1", "J2", "E1", "E4", "E5" }) do
                        if want == nil and p2.valid[k] < p2.need[k] then want = k end
                    end
                    -- ---- start the next experiment from this attack: one blocking sequence timed on the server tick ----
                    if not acted and want ~= nil and new_atk ~= nil and hpv >= 55 and not crab_alive and p2.mode == "exp" and tk - last_purple > 14
                        and (p2.tries[want] or 0) < 7 then
                        local A = new_atk.tick
                        if (want == "J1" or want == "J2") and kind == "bomb" and mt.x == 6427 and mt.z == 91 then
                            local f = math.floor(brow.end_cycle / 30)
                            local I = A + f
                            if want == "J1" and f >= 2 then
                                p2.tries.J1 = (p2.tries.J1 or 0) + 1
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= I - 1 end, note = "J1 wait" }, 6)
                                t.player.step_tick(6426, 91, 3)
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= I + 2 end, note = "J1 settle" }, 8)
                                p2.pending[#p2.pending + 1] = { kind = "J1", A = A, I = I, X = { 6427, 91 } }
                                acted = true
                            elseif want == "J2" and f >= 3 then
                                p2.tries.J2 = (p2.tries.J2 or 0) + 1
                                t.player.step_tick(6426, 91, 3)
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= I - 1 end, note = "J2 wait" }, 6)
                                t.player.step_tick(6427, 91, 3)
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= I + 2 end, note = "J2 settle" }, 8)
                                p2.pending[#p2.pending + 1] = { kind = "J2", A = A, I = I, X = { 6427, 91 } }
                                acted = true
                            end
                        elseif (want == "E1" or want == "E4" or want == "E5") and kind ~= "melee" and kind ~= "purple" and mt.x == 6429 and mt.z == 91 and hpv >= 70 then
                            p2.tries[want] = (p2.tries[want] or 0) + 1
                            if want == "E1" then
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= A + 1 end, note = "E1 wait" }, 4)
                                t.player.step_tick(6430, 91, 3)
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= A + 4 end, note = "E1 attack" }, 6)
                            elseif want == "E4" then
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= A + 3 end, note = "E4 wait" }, 5)
                                t.player.step_tick(6430, 91, 3)
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= A + 6 end, note = "E4 walk" }, 5)
                                t.player.step_tick(6429, 91, 3)
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= A + 8 end, note = "E4 settle" }, 6)
                            else
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= A + 1 end, note = "E5 wait" }, 4)
                                t.player.step_tick(6430, 91, 3)
                                t.player.step_tick(6431, 91, 3)
                                t.await({ level = function() local _, k2 = t.tick() return k2 >= A + 4 end, note = "E5 attack" }, 6)
                            end
                            p2.pending[#p2.pending + 1] = { kind = want, A = A, I = 0, X = { 0, 0 } }
                            if want == "E1" or want == "E5" then
                                -- a slam or a stomp stuns: walk home (the walk resolves when the stun ends), else step back out
                                local _, rr = t.ticklog.rows({ kind = "npc_anim", since = cur.anim })
                                local hit8116 = false
                                for _, row in ipairs(rr) do
                                    if row.slot == boss_slot and row.seq == 8116 and row.tick == A + 4 then hit8116 = true end
                                end
                                if hit8116 then
                                    p2.slams[#p2.slams + 1] = { T = A + 4, kind = want == "E1" and "slam" or "stomp", issued = select(2, t.tick()) }
                                    t.player.walk_to(6427, 91, 14)
                                else
                                    t.player.step_tick(6429, 91, 3)
                                end
                            end
                            acted = true
                        end
                    end
                    -- the walk to an experiment's home: J at 6427,91 (a 3-tick bomb), E at 6429,91 (two out of her footprint)
                    if not acted and p2.exp == nil and want ~= nil and not crab_alive and hpv >= 55 and new_atk == nil and p2.mode == "exp" then
                        local hx, hz = 6427, 91
                        if want == "E1" or want == "E4" or want == "E5" then hx, hz = 6429, 91 end
                        local lastA = -10
                        for _, row in ipairs(R.anim) do
                            if row.slot == boss_slot and row.type == 8372 and (row.seq == 8114 or row.seq == 8116) then lastA = row.tick end
                        end
                        if (mt.x ~= hx or mt.z ~= hz) and lastA > 0 and tk >= lastA + 1 and tk <= lastA + 3 then
                            t.player.walk_to(hx, hz, 5)
                            acted = true
                        end
                    end
                    if p2.mode == "exp" and (want == nil or tk > p2.started + 160) then
                        p2.mode = "fight"
                        p2.fight_from = tk
                        p2_emit = "early"
                        plog[#plog + 1] = tk .. ":p2 fight mode (experiments valid " .. p2.valid.J1 .. p2.valid.J2 .. p2.valid.E1 .. p2.valid.E4 .. p2.valid.E5 .. ")"
                    end
                    -- ---- dodge, eat and fight ---------------------------------------------------
                    if last_reds > 0 and p2.reds_tb == 0 then
                        p2.reds_tb = 1
                        reds_tick = last_reds
                        t.cheat("::tobboss", false)
                    elseif p2.reds_tb == 1 then
                        p2.reds_tb = 2
                        local _, ml = t.msg.last(8)
                        for _, m in ipairs(ml) do
                            local mtx = tostring(m.text)
                            if tb_reds == "" and mtx:find("tobboss record=") and mtx ~= tb_p2 and mtx ~= tb_p1 and mtx ~= tb_pre then tb_reds = mtx end
                        end
                    end
                    if last_reds > 0 and p2.reds_prayer == 0 and not acted then
                        -- the Matomenos phase: she casts the blood spell (magic) three attacks in four
                        p2.reds_prayer = 1
                        t.prayer.set("protectfrommagic", true)
                        plog[#plog + 1] = tk .. ":reds summoned, Protect from Magic"
                        acted = true
                    end
                    if not acted and p2.exp == nil and p2.mode == "fight" then
                        if (kind == "bomb" or purple_here) and mt.x <= 6429 then
                            -- the bomb is aimed at the tile held now and lands 3 ticks on: step off it, never onto a live aim
                            local picked
                            for _, off in ipairs({ { -1, 0 }, { 1, 0 }, { 0, 1 }, { 0, -1 }, { -1, 1 }, { -1, -1 }, { 1, 1 }, { 1, -1 } }) do
                                local cx, cz = mt.x + off[1], mt.z + off[2]
                                local clash = false
                                for _, a in ipairs(p2.aims) do
                                    if tk <= a.tick + a.life + 1 and a.x == cx and a.z == cz then clash = true end
                                end
                                if picked == nil and not clash and cx >= 6425 and cx <= 6428 and cz >= 89 and cz <= 93 then picked = { cx, cz } end
                            end
                            if picked then
                                t.player.step_tick(picked[1], picked[2], 3)
                                p2.dodges = p2.dodges + 1
                                acted = true
                            end
                        end
                    end
                    if not acted and p2.exp == nil then
                        -- a crab blast (52) lands on top of a lightning ball (48): meet a live crab near full hitpoints
                        if (hpv < 65 or (crab_alive and hpv < 96)) and tk >= last_eat + 3 and (fish > 0 or brewn > 0) then
                            if fish > 0 then t.player.inv_op("anglerfish", 1) else t.player.inv_op(brew_name, 1) end
                            last_eat = tk
                            acted = true
                        elseif crab_alive and p2.mode == "exp" then
                            local csym = { [8381] = "verzik_nylocas_melee", [8382] = "verzik_nylocas_ranged", [8383] = "verzik_nylocas_magic" }
                            t.player.attack(csym[crab_alive.type] or "verzik_nylocas_ranged", 2, 1)
                            acted = true
                        elseif ath_alive and p2.mode == "exp" and tk <= ath_alive.tick + 70 and tk >= ath_alive.tick + 2 and tk >= p2.last_atk + 3 then
                            -- the Athanatos heals her every beat: it is shot before the experiments, not after
                            t.player.attack("tob_verzik_phase2_armourednylocas", 2, 1)
                            p2.last_atk = tk
                            acted = true
                        elseif p2.mode == "exp" and p2.exp == nil and tk >= p2.last_atk + 4 and new_atk == nil and mt.x >= 6426 and mt.x <= 6429 and mt.z == 91 then
                            -- between experiments the bow keeps working from the home tile, up to 200 dealt (the pool is 400: the trials must finish before the floor)
                            local dealt2 = 0
                            for _, h in ipairs(R.hitn) do
                                if h.slot == boss_slot and h.type == 8372 then dealt2 = dealt2 + h.damage end
                            end
                            if dealt2 < 200 then
                                t.player.attack("verzik_phase2", 2, 1)
                                p2.last_atk = tk
                                acted = true
                            end
                        elseif p2.mode == "fight" then
                            if mt.x >= 6430 or mt.x <= 6424 then
                                t.player.walk_to(6427, 91, 6)
                            else
                                local target = "verzik_phase2"
                                if crab_alive then
                                    local csym = { [8381] = "verzik_nylocas_melee", [8382] = "verzik_nylocas_ranged", [8383] = "verzik_nylocas_magic" }
                                    target = csym[crab_alive.type] or "verzik_nylocas_ranged"
                                elseif ath_alive and tk <= math.max(ath_alive.tick, p2.fight_from) + 70 then target = "tob_verzik_phase2_armourednylocas"
                                end
                                -- an Athanatos is shot for 24 ticks after it lands (it heals her every beat); one that survives that is left standing
                                local absorb = (last_reds > 0 and tk <= last_reds + 5 and not crab_alive)
                                if absorb then
                                    -- the five ticks after the Matomenos summon heal her for every hit: hold fire
                                    p2.last_target = target
                                elseif target ~= p2.last_target or tk >= p2.last_atk + 3 then
                                    t.player.attack(target, 2, 1)
                                    p2.last_target = target
                                    p2.last_atk = tk
                                end
                            end
                        end
                    end
                    local _, prn = t.skill.read("prayer")
                    if (prn.current or prn.level) < 20 then
                        for _, pn in ipairs({ "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" }) do
                            local _, pc = t.inv.count(pn)
                            if pc > 0 and not acted then
                                t.player.inv_op(pn, 1)
                                break
                            end
                        end
                    end
                end

            -- ======================= TRANSIT PHASE 2 -> 3 ========================
            elseif phase == "T23" then
                if p2_rows_done == 0 then
                    p2_rows_done = 1
                    p2_emit = "late"
                end
                if p3.prep == 0 then
                    p3.prep = 1
                    if hpv < 75 and fish > 0 then
                        t.player.inv_op("anglerfish", 1)
                        last_eat = tk
                    end
                    t.player.walk_to(6430, 91, 6)
                end
                if p3id_tick ~= nil then
                    phase = "P3"
                    p3.start = tk
                    p3.E = E_tick
                    t.cheat("::tobboss", false)
                end

            -- ============================== PHASE 3 ==============================
            elseif phase == "P3" then
                local acted = false
                if tb_p3 == "" then
                    local _, ml = t.msg.last(8)
                    for _, m in ipairs(ml) do
                        local mtx = tostring(m.text)
                        if tb_p3 == "" and mtx:find("tobboss record=") and mtx ~= tb_p2 and mtx ~= tb_p1 and mtx ~= tb_pre then tb_p3 = mtx end
                    end
                end
                if tb_p3 ~= "" and pools_done == 0 then
                    pools_done = 1
                -- ======================= POOLS AND DEFENCE (P1, P2 and P3 readouts together) =======================
                local hp_pre = tonumber(string.match(tb_pre, "hp=(%d+) of")) or 0
                local h3 = tonumber(string.match(tb_p3, "hp=(%d+) of"))
                local last_p2_hit, last_p2_tick = 0, 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 8372 and h.tick > last_p2_tick then last_p2_tick = h.tick last_p2_hit = h.damage end
                end
                local s1_total, s1_last = 0, 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 8370 then s1_total = s1_total + h.damage s1_last = h.damage end
                end
                local p1_ok = (s1_total - s1_last) < 300 and s1_total >= 300
                if h3 then
                    -- the P3 form restates her pool on top of the floor she crossed: the readout's total grows from the pre-fight figure,
                    -- and the hits dealt on the P3 form before the readout are added back to the hitpoints it showed
                    local of_pre = tonumber(string.match(tb_pre, "hp=%d+ of (%d+)")) or 0
                    local of_p3 = tonumber(string.match(tb_p3, "hp=%d+ of (%d+)")) or 0
                    local before_read = 0
                    for _, h in ipairs(R.hitn) do
                        if h.slot == boss_slot and h.type == 8374 and h.tick <= p3.start + 1 then before_read = before_read + h.damage end
                    end
                    local growth = of_p3 - of_pre
                    -- the barrier hands her P1 + 2 x P2; the shield pool is bracketed by the hits, so the P2 pool is half of what is left
                    local p2_meas = (of_pre - 300) / 2
                    local p3_meas = of_p3 - 300 - p2_meas
                    local overkill = p3_meas - (h3 + before_read)
                    local crossing, crossing_tick = 0, 0
                    for _, h in ipairs(R.hitn) do
                        if h.slot == boss_slot and h.type == 8372 and h.damage > 0 and h.tick > crossing_tick and h.tick <= p3id_tick then crossing = h.damage crossing_tick = h.tick end
                    end
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_p3_hp_1p", p1_ok and p3_meas == 600,
                        "measured " .. p3_meas .. " hp, ::tobboss total " .. of_p3 .. " on the P3 form against " .. of_pre .. " before the fight (P1 shield bracketed at 300: " .. tostring(p1_ok) .. ", P2 " .. p2_meas .. "), hitpoints read " .. h3 .. " = " .. p3_meas .. " less an overkill of " .. overkill .. " from the final P2 hit of " .. crossing .. " on tick " .. crossing_tick .. " " .. SPEC.entry_p3_hp_1p }
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_p2_hp_1p", p1_ok and p2_meas == 400,
                        "measured " .. p2_meas .. " hp, ::tobboss total " .. of_pre .. " before the fight = 300 (shield, bracketed: " .. tostring(p1_ok) .. ") + 2 x P2 " .. SPEC.entry_p2_hp_1p }
                end
                local d1 = tonumber(string.match(tb_p1, "def=(%d+) of"))
                local d2 = tonumber(string.match(tb_p2, "def=(%d+) of"))
                local d3 = tonumber(string.match(tb_p3, "def=(%d+) of"))
                if d1 and d2 and d3 then
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_defence", d1 == 10 and d2 == 120 and d3 == 120,
                        "measured " .. d1 .. "," .. d2 .. "," .. d3 .. " count, ::tobboss def on the P1, P2 and P3 forms " .. SPEC.entry_defence }
                end
                for _, rw in ipairs(out_rows) do t.check(rw[1], rw[2], rw[3]) end
                out_rows = {}
                end
                local new_atk
                for _, row in ipairs(new.anim) do
                    if row.slot == boss_slot and row.type == 8374 then
                        if row.seq == 8123 or row.seq == 8124 or row.seq == 8125 then
                            new_atk = row
                            p3.autos = p3.autos + 1
                            p3.attacks[#p3.attacks + 1] = { tick = row.tick, seq = row.seq, n = p3.autos }
                            p3.next_T = row.tick + 7
                        elseif row.seq ~= 8119 and row.seq ~= 8118 then
                            p3.specials[#p3.specials + 1] = { tick = row.tick, seq = row.seq }
                            p3.attacks[#p3.attacks + 1] = { tick = row.tick, seq = row.seq, n = -1 }
                            p3.next_T = row.tick + 10
                        end
                    end
                end
                -- dealt damage and the enrage
                local dealt = 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 8374 then dealt = dealt + h.damage end
                end
                for _, row in ipairs(new.spawn) do
                    if row.type == 8386 and p3.enraged == 0 then p3.enraged = row.tick enraged_tick = row.tick end
                end
                if p3.enraged > 0 and p3.enr_tb == nil then
                    p3.enr_tb = 1
                    t.cheat("::tobboss", false)
                elseif p3.enr_tb == 1 then
                    p3.enr_tb = 2
                    local _, ml = t.msg.last(8)
                    for _, m in ipairs(ml) do
                        local mtx = tostring(m.text)
                        if tb_enr == "" and mtx:find("tobboss record=") and mtx ~= tb_p3 and mtx ~= tb_p2 and mtx ~= tb_p1 and mtx ~= tb_pre then tb_enr = mtx end
                    end
                end
                for _, row in ipairs(new.proj) do
                    if row.spotanim == 1598 then p3.ball_seen = row.tick end
                end
                if p3.ball_seen > 0 then p3.dealt_cap = 99999 elseif p3.dealt_cap > 440 then p3.dealt_cap = 440 end
                -- plan the next attack's position by the autos seen
                local plans = { [1] = "UNDER", [2] = "ADJ", [3] = "STEPON", [4] = "HOME", [5] = "STEPON" }
                if new_atk then
                    p3.plan = plans[p3.autos] or "HOME"
                    p3.stepped = 0
                end
                -- the prayer follows the style of the attack just seen (the ranged and magic hits land three ticks after the animation);
                -- the first two magic autos and the melee autos of the experiments are left unprayed so the maxima rows have hits to read
                if new_atk then
                    if new_atk.seq == 8125 then
                        p3.mag_seen = (p3.mag_seen or 0) + 1
                        if p3.mag_seen > 2 then t.prayer.set("protectfrommagic", true) end
                    elseif new_atk.seq == 8124 then
                        t.prayer.set("protectfrommissiles", true)
                    elseif new_atk.seq == 8123 and p3.autos > 9 then
                        t.prayer.set("protectfrommelee", true)
                    end
                end
                if p3.autos >= 10 and p3.early_done == nil then
                    p3.early_done = 1
                    p3_emit = "early"
                end
                local sp_n = #p3.specials
                if sp_n >= 1 and p3.specials[1].seq ~= nil and p3.autos == 4 then p3.plan = "ADJ" end
                local vr, vrow = t.npc.state("verzik_phase3")
                local bx, bz, bs = 6431, 89, 7
                if vr == "ok" then bx, bz, bs = vrow.x, vrow.z, vrow.size or 7 end
                local T = p3.next_T
                -- ---- pool: the yellow special -------------------------------------------------
                local pool_row
                for _, row in ipairs(new.mapfx) do
                    if row.spotanim == 1595 then pool_row = row end
                end
                if pool_row then
                    p3.pool = { tick = pool_row.tick, x = pool_row.x, z = pool_row.z, delay = pool_row.delay }
                end
                -- ---- web / crab / tornado presence ------------------------------------------
                local web_alive, crab_alive, tornado_alive
                for _, row in ipairs(R.spawn) do
                    if row.tick >= p3id_tick and (row.type == 8376 or row.type == 8381 or row.type == 8382 or row.type == 8383 or row.type == 8386) then
                        local gone = false
                        for _, d2 in ipairs(R.death) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                        for _, d2 in ipairs(R.free) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                        if not gone then
                            if row.type == 8376 then web_alive = row
                            elseif row.type == 8386 then tornado_alive = row
                            else crab_alive = row end
                        end
                    end
                end
                -- ---- 1. hitpoints ---------------------------------------------------------------
                local ball_due = (p3.ball_seen > 0 and tk <= p3.ball_seen + 6)
                if (hpv < 60 or (ball_due and hpv < 85)) and tk >= last_eat + 3 and (fish > 0 or brewn > 0) then
                    if fish > 0 then t.player.inv_op("anglerfish", 1) else t.player.inv_op(brew_name, 1) end
                    last_eat = tk
                    acted = true
                end
                -- ---- 2. yellow pool: stand on it until the blast ----------------------------------
                if not acted and p3.pool and tk <= p3.pool.tick + 16 then
                    if mt.x ~= p3.pool.x or mt.z ~= p3.pool.z then
                        t.player.walk_to(p3.pool.x, p3.pool.z, 4)
                    end
                    acted = true
                end
                -- ---- 3. tornado flee ----------------------------------------------------------------
                if not acted and tornado_alive then
                    local tr2, trow = t.npc.nearest("tob_verzik_creeper", 24)
                    if tr2 == "ok" then
                        local dx, dz = mt.x - trow.x, mt.z - trow.z
                        local dist = math.max(math.abs(dx), math.abs(dz))
                        if dist <= 3 and tk >= p3.last_flee + 1 then
                            local fx = mt.x + (dx >= 0 and 3 or -3)
                            local fz = mt.z + (dz >= 0 and 3 or -3)
                            if fx < 6420 then fx = 6420 end
                            if fx > 6444 then fx = 6444 end
                            if fz < 80 then fz = 80 end
                            if fz > 100 then fz = 100 end
                            t.player.walk_to(fx, fz, 3)
                            p3.last_flee = tk
                            acted = true
                        end
                    end
                end
                -- ---- 4a. step off the tile the web is thrown at: it lands three ticks after the cast, binds only a player standing on it,
                -- and a bound player cannot shoot the web (the walk-trigger of the freeze eats the attack)
                for _, row in ipairs(new.anim) do
                    if row.slot == boss_slot and row.type == 8374 and row.seq == 8127 then p3.web_cast = row.tick end
                end
                -- the web special is her tenth attack (nine autos and the crab special before it): it comes at the next attack tick, and
                -- the walk off is begun before it so the player is three tiles away when the web lands
                if not acted and p3.autos == 9 and #p3.specials == 1 and p3.web_stepped == nil and p3.next_T > 0 and tk >= p3.next_T - 2 and tk <= p3.next_T + 1 then
                    p3.web_stepped = tk
                    local sx = mt.x - 3
                    if sx < 6421 then sx = mt.x + 3 end
                    local sr2, sd2 = t.player.walk_to(sx, mt.z, 4)
                    t.check("drive.web_step", true, "tick " .. tk .. " (next attack due " .. p3.next_T .. ") walk from " .. mt.x .. "," .. mt.z .. " to " .. sx .. "," .. mt.z .. " answered " .. tostring(sr2) .. ": " .. tostring(sd2))
                    acted = true
                end
                -- ---- 4. webs, crabs ---------------------------------------------------------------
                if not acted and web_alive and tk < (p3.web_last or 0) + 6 then
                    acted = true
                end
                if not acted and web_alive then
                    p3.web_last = tk
                    local wr, wd = t.player.attack("verzik_web_npc", 2, 1)
                    p3.web_presses = (p3.web_presses or 0) + 1
                    if p3.web_presses <= 3 then
                        t.check("drive.web_press" .. p3.web_presses, true, "tick " .. tk .. " attack on the web (spawned " .. web_alive.tick .. ") answered " .. tostring(wr) .. ": " .. tostring(wd) .. "; player at " .. mt.x .. "," .. mt.z)
                    end
                    acted = true
                end
                if not acted and crab_alive then
                    local csym = { [8381] = "verzik_nylocas_melee", [8382] = "verzik_nylocas_ranged", [8383] = "verzik_nylocas_magic" }
                    t.player.attack(csym[crab_alive.type] or "verzik_nylocas_ranged", 2, 1)
                    acted = true
                end
                -- ---- 5. the melee-predicate experiments (steps timed against the next attack) ------------
                if not acted and p3.autos <= 9 and T > 0 then
                    local adj = { bx - 1, bz + 2 }
                    local ex, ez
                    if p3.plan == "ADJ" then ex, ez = adj[1], adj[2]
                    elseif p3.plan == "UNDER" then ex, ez = bx + 1, bz + 2
                    elseif p3.plan == "HOME" then ex, ez = bx - 3, bz + 2 end
                    if p3.plan == "STEPON" then
                        if tk >= T - 6 and p3.stepped == 0 and not (mt.x == bx - 2 and mt.z == bz + 2) then
                            t.player.step_tick(bx - 2, bz + 2, 3)
                            p3.stepped = 1
                            acted = true
                        elseif tk == T - 1 and p3.stepped <= 1 and mt.x == bx - 2 then
                            t.player.step_tick(bx - 1, bz + 2, 3)
                            p3.stepped = 2
                            acted = true
                        end
                    elseif ex and tk >= T - 4 and tk <= T - 2 and p3.stepped == 0 and (mt.x ~= ex or mt.z ~= ez) then
                        local dd = math.max(math.abs(mt.x - ex), math.abs(mt.z - ez))
                        if dd == 1 then
                            t.player.step_tick(ex, ez, 3)
                        else
                            t.player.walk_to(ex, ez, 4)
                        end
                        p3.stepped = 1
                        acted = true
                    end
                end
                -- ---- 6. fight: paced so that every special of the rotation is met before the enrage -----
                if not acted then
                    local near_step = (p3.autos <= 9 and T > 0 and tk >= T - 5 and tk <= T)
                    if dealt < p3.dealt_cap and not near_step and tk >= p3.last_atk + 3 then
                        if mt.x > bx - 3 and p3.autos > 9 then
                            t.player.walk_to(bx - 3, bz + 2, 4)
                        end
                        t.player.attack("verzik_phase3", 2, 1)
                        p3.last_atk = tk
                    end
                end
                local _, prn = t.skill.read("prayer")
                if (prn.current or prn.level) < 20 and not acted then
                    for _, pn in ipairs({ "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" }) do
                        local _, pc = t.inv.count(pn)
                        if pc > 0 then
                            t.player.inv_op(pn, 1)
                            break
                        end
                    end
                end
            end
        end

        do
        local p3_stage = "late"
        -- ======================= PHASE 3 SPEC ROWS (from the tick log) =======================
        local A3, specials3, balls3 = {}, {}, {}
        for _, r in ipairs(R.anim) do
            if r.slot == boss_slot and r.type == 8374 then
                if r.seq == 8123 or r.seq == 8124 or r.seq == 8125 then A3[#A3 + 1] = r
                elseif r.seq ~= 8119 and r.seq ~= 8118 then specials3[#specials3 + 1] = r end
            end
        end
        for _, r in ipairs(R.proj) do
            if r.spotanim == 1598 then balls3[#balls3 + 1] = r end
        end
        if p3id_tick and E_tick then
            out_rows[#out_rows + 1] = { "spec.verzik.p3_id_after_phase_event", p3id_tick - E_tick >= 5 and p3id_tick - E_tick <= 7,
                "measured " .. (p3id_tick - E_tick) .. " ticks, 8118 on tick " .. E_tick .. ", the P3 id (retype to 8374) on tick " .. p3id_tick .. " " .. SPEC.p3_id_after_phase_event }
        end
        if A3[1] and p3id_tick and E_tick then
            local fi, fe = A3[1].tick - p3id_tick, A3[1].tick - E_tick
            out_rows[#out_rows + 1] = { "spec.verzik.p3_first_attack_after_id", fi >= 5 and fi <= 7,
                "measured " .. fi .. " ticks, first P3 attack row on tick " .. A3[1].tick .. " " .. SPEC.p3_first_attack_after_id }
            out_rows[#out_rows + 1] = { "spec.verzik.p3_first_attack_after_phase_event", fe >= 11 and fe <= 13,
                "measured " .. fe .. " ticks, first P3 attack against the 8118 phase event " .. SPEC.p3_first_attack_after_phase_event }
        end
        -- cadence of consecutive autos with no special between them, before and after the enrage
        local cad3, cad3_seen, cad3_n, cad5, cad5_seen, cad5_long = {}, {}, 0, {}, {}, {}
        for i = 2, #A3 do
            local g = A3[i].tick - A3[i - 1].tick
            local spans = false
            for _, s2 in ipairs(specials3) do
                if s2.tick > A3[i - 1].tick and s2.tick < A3[i].tick then spans = true end
            end
            if not spans then
                if enraged_tick and A3[i - 1].tick >= enraged_tick then
                    if g > 10 then cad5_long[#cad5_long + 1] = g elseif not cad5_seen[g] then cad5_seen[g] = true cad5[#cad5 + 1] = g end
                else
                    cad3_n = cad3_n + 1
                    if not cad3_seen[g] then cad3_seen[g] = true cad3[#cad3 + 1] = g end
                end
            end
        end
        if cad3_n >= 1 then
            out_rows[#out_rows + 1] = { "spec.verzik.p3_cadence", #cad3 == 1 and cad3[1] == 7,
                "measured " .. table.concat(cad3, ",") .. " ticks, " .. cad3_n .. " of " .. cad3_n .. " gaps between consecutive autos with no special between " .. SPEC.p3_cadence }
        end
        if #cad5 >= 1 then
            out_rows[#out_rows + 1] = { "spec.verzik.p3_enraged_cadence", #cad5 == 1 and cad5[1] == 5,
                "measured " .. table.concat(cad5, ",") .. " ticks, gaps between autos after the enrage on tick " .. tostring(enraged_tick) .. " (" .. #cad5_long .. " gap over 10 ticks left out: a special attack window, not an auto-to-auto gap) " .. SPEC.p3_enraged_cadence }
        end
        if specials3[1] then
            local before = 0
            for _, a in ipairs(A3) do
                if a.tick < specials3[1].tick then before = before + 1 end
            end
            out_rows[#out_rows + 1] = { "spec.verzik.p3_attacks_between_specials", before == 4,
                "measured " .. before .. " count, autos before her first special (seq " .. specials3[1].seq .. " on tick " .. specials3[1].tick .. ") " .. SPEC.p3_attacks_between_specials }
            local nxt
            for _, a in ipairs(A3) do
                if a.tick > specials3[1].tick and not nxt then nxt = a.tick - specials3[1].tick end
            end
            if nxt then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_after_special", nxt == 10,
                    "measured " .. nxt .. " ticks, from the crab special (seq " .. specials3[1].seq .. ") to her next auto " .. SPEC.p3_after_special }
            end
        end
        -- the order of the specials: crabs 14406, webs 8127, yellows 8126, ball (projectile 1598, no animation)
        local ordered = {}
        for _, s2 in ipairs(specials3) do
            local nm = (s2.seq == 14406 and "crabs") or (s2.seq == 8127 and "webs") or (s2.seq == 8126 and "yellows") or nil
            if nm then ordered[#ordered + 1] = { s2.tick, nm } end
        end
        for _, b in ipairs(balls3) do ordered[#ordered + 1] = { b.tick, "ball" } end
        table.sort(ordered, function(x, y) return x[1] < y[1] end)
        local order_txt = {}
        for _, o in ipairs(ordered) do order_txt[#order_txt + 1] = o[2] end
        if #order_txt >= 4 then
            local first4 = table.concat({ order_txt[1], order_txt[2], order_txt[3], order_txt[4] }, ",")
            out_rows[#out_rows + 1] = { "spec.verzik.p3_special_order", first4 == "crabs,webs,yellows,ball",
                "measured " .. first4 .. "; specials in order " .. table.concat(order_txt, ", ") .. " (" .. #order_txt .. " specials) " .. SPEC.p3_special_order }
        end
        -- the melee predicate, from where I stood at the end of the tick before every attack (her 7x7 at 6431..6437, 89..95)
        local c3 = { adj = { 0, 0 }, under = { 0, 0 }, far = { 0, 0 }, on = { 0, 0 } }
        local melee_max, auto_max, n_melee_hits, n_auto_hits = 0, 0, 0, 0
        local first_adj_melee = "no attack with me adjacent"
        for i, a in ipairs(A3) do
            local p0, p1 = ptile_at[a.tick - 1], ptile_at[a.tick]
            if p0 and p1 then
                local d0 = math.max(math.max(6431 - p0.x, p0.x - 6437, 0), math.max(89 - p0.z, p0.z - 95, 0))
                local d1 = math.max(math.max(6431 - p1.x, p1.x - 6437, 0), math.max(89 - p1.z, p1.z - 95, 0))
                local mel = (a.seq == 8123) and 1 or 0
                if d0 == 1 then c3.adj[1] = c3.adj[1] + 1 c3.adj[2] = c3.adj[2] + mel
                    if i == 1 then first_adj_melee = (mel == 1) and "melee" or "no melee" end
                elseif d0 == 0 then c3.under[1] = c3.under[1] + 1 c3.under[2] = c3.under[2] + mel
                else
                    c3.far[1] = c3.far[1] + 1 c3.far[2] = c3.far[2] + mel
                    if d1 == 1 then c3.on[1] = c3.on[1] + 1 c3.on[2] = c3.on[2] + mel end
                end
                for _, h in ipairs(R.hitp) do
                    if h.npc_slot == boss_slot and h.tick == a.tick and a.seq == 8123 then
                        n_melee_hits = n_melee_hits + 1
                        if h.damage > melee_max then melee_max = h.damage end
                    end
                end
            end
        end
        for _, pr in ipairs(R.proj) do
            if pr.spotanim == 1593 or pr.spotanim == 1594 then
                local f = math.floor(pr.end_cycle / 30)
                for _, h in ipairs(R.hitp) do
                    if h.npc_slot == boss_slot and h.npc_type == 8374 and h.tick == pr.tick + f then
                        n_auto_hits = n_auto_hits + 1
                        if h.damage > auto_max then auto_max = h.damage end
                    end
                end
            end
        end
        if c3.adj[1] >= 1 and (c3.under[1] + c3.far[1]) >= 1 then
            out_rows[#out_rows + 1] = { "spec.verzik.p3_melee_predicate", c3.adj[2] >= 1 and c3.under[2] == 0 and c3.far[2] == 0,
                "measured adjacent not overlapping on T-1; melee on " .. c3.adj[2] .. " of " .. c3.adj[1] .. " attacks with the tank adjacent on T-1, " .. c3.under[2] .. " of " .. c3.under[1] .. " with the tank under her, " .. c3.far[2] .. " of " .. c3.far[1] .. " with the tank two or more out; first P3 attack with the tank adjacent: " .. first_adj_melee .. " " .. SPEC.p3_melee_predicate }
            out_rows[#out_rows + 1] = { "spec.verzik.p3_scan_tick", c3.adj[2] >= 1 and (c3.on[1] == 0 or c3.on[2] == 0),
                "measured -1 ticks, melee on " .. c3.adj[2] .. " of " .. c3.adj[1] .. " attacks with me adjacent at the end of T-1, and on " .. c3.on[2] .. " of " .. c3.on[1] .. " where my step adjacent resolved on T itself " .. SPEC.p3_scan_tick }
            out_rows[#out_rows + 1] = { "spec.verzik.p3_melee_chance", true,
                "measured " .. math.floor(100 * c3.adj[2] / c3.adj[1]) .. " percent, " .. c3.adj[2] .. " of " .. c3.adj[1] .. " attacks with me adjacent on T-1 were melee " .. SPEC.p3_melee_chance }
        end
        if n_melee_hits >= 1 then
            out_rows[#out_rows + 1] = { "spec.verzik.entry_p3_melee_max", melee_max <= 36,
                "measured " .. melee_max .. " hp, the largest of " .. n_melee_hits .. " melee hits on me " .. SPEC.entry_p3_melee_max }
        end
        if n_auto_hits >= 1 then
            out_rows[#out_rows + 1] = { "spec.verzik.entry_p3_auto_max", auto_max <= 20,
                "measured " .. auto_max .. " hp, the largest of " .. n_auto_hits .. " ranged and magic auto hits on me (the first two magic autos unprayed, later autos prayed by style; unprayed ceiling 20) " .. SPEC.entry_p3_auto_max }
        end
        local fl_v, fl_seen = {}, {}
        for _, pr in ipairs(R.proj) do
            if (pr.spotanim == 1593 or pr.spotanim == 1594) and not fl_seen[pr.end_cycle] then fl_seen[pr.end_cycle] = true fl_v[#fl_v + 1] = pr.end_cycle end
        end
        if #fl_v >= 1 then
            out_rows[#out_rows + 1] = { "spec.verzik.p3_proj_flight", true,
                "measured " .. table.concat(fl_v, ",") .. " cycles, end_cycle of her auto projectiles 1593 and 1594 (the ball " .. tostring(balls3[1] and balls3[1].end_cycle) .. ") " .. SPEC.p3_proj_flight }
        end
        -- yellows
        local pools, pool_ticks = {}, {}
        for _, r in ipairs(R.mapfx) do
            if r.spotanim == 1595 then pools[#pools + 1] = r end
        end
        if #pools >= 1 then
            local pn_by_tick, uniq_ticks = {}, 0
            for _, r in ipairs(pools) do
                if not pn_by_tick[r.tick] then pn_by_tick[r.tick] = 0 uniq_ticks = uniq_ticks + 1 end
                pn_by_tick[r.tick] = pn_by_tick[r.tick] + 1
            end
            out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pool_lifetime", pools[1].delay == 14,
                "measured " .. pools[1].delay .. " ticks, the duration the pool graphic 1595 was set with on tick " .. pools[1].tick .. " " .. SPEC.p3_yellow_pool_lifetime }
            out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pools", pn_by_tick[pools[1].tick] == 1,
                "measured " .. pn_by_tick[pools[1].tick] .. " ratio, pools set on the cast tick, one raider in the room " .. SPEC.p3_yellow_pools }
            local nxt
            for _, a in ipairs(A3) do
                if a.tick > pools[1].tick and not nxt then nxt = a.tick - (pools[1].tick + pools[1].delay) end
            end
            if nxt then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_after_yellows", nxt == 7,
                    "measured " .. nxt .. " ticks, from the pool's end (set on " .. pools[1].tick .. " for " .. pools[1].delay .. ") to her next auto " .. SPEC.p3_after_yellows }
            end
        end
        -- ball
        if #balls3 >= 1 then
            local nxt
            for _, a in ipairs(A3) do
                if a.tick > balls3[1].tick and not nxt then nxt = a.tick - balls3[1].tick end
            end
            out_rows[#out_rows + 1] = { "spec.verzik.p3_ball_flight", balls3[1].end_cycle > 210,
                "measured " .. math.min(7, math.floor(balls3[1].end_cycle / 30)) .. " ticks, the bound reached (capped at 7 ticks = 210 cycles; the projectile 1598 row reads end_cycle " .. balls3[1].end_cycle .. " cycles, " .. math.floor(balls3[1].end_cycle / 30) .. " ticks, so more than 210 cycles from landing when first seen) " .. SPEC.p3_ball_flight }
            if nxt then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_ball_delay", nxt == 12,
                    "measured " .. nxt .. " ticks, from the ball's launch tick " .. balls3[1].tick .. " to her next auto " .. SPEC.p3_ball_delay }
            end
        end
        -- webs
        local webs = {}
        for _, r in ipairs(R.spawn) do
            if r.type == 8376 then webs[#webs + 1] = r end
        end
        for _, s2 in ipairs(specials3) do
            if s2.seq == 8127 then
                local nxt
                for _, a in ipairs(A3) do
                    if a.tick > s2.tick and not nxt then nxt = a.tick - s2.tick end
                end
                if nxt then
                    out_rows[#out_rows + 1] = { "spec.verzik.p3_webs_to_next_auto", nxt >= 38 and nxt <= 50,
                        "measured " .. nxt .. " ticks, from the web special on tick " .. s2.tick .. " to her next auto " .. SPEC.p3_webs_to_next_auto }
                end
                break
            end
        end
        for _, w in ipairs(webs) do
            local dth
            for _, d in ipairs(R.death) do if d.slot == w.slot and d.tick >= w.tick and not dth then dth = d.tick end end
            if dth then
                local sum = 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == w.slot and h.tick >= w.tick and h.tick <= dth then sum = sum + h.damage end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.web_hp", sum == 10,
                    "measured " .. sum .. " hp, hit_npc damage on the first web (spawn tick " .. w.tick .. ", death " .. dth .. ") " .. SPEC.web_hp }
                break
            end
        end
        do
            local hr = tonumber(string.match(tb_enr, "hp=(%d+) of"))
            if hr and enraged_tick then
                local lt, lh = 0, 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 8374 and h.tick <= enraged_tick and h.tick > lt then lt = h.tick lh = h.damage end
                end
                local after = hr * 100 / 600
                local before = (hr + lh) * 100 / 600
                out_rows[#out_rows + 1] = { "spec.verzik.p3_enrage_threshold", after <= 20 and before > 20,
                    string.format("measured 20 percent, ::tobboss read %d hitpoints right after the tornado spawned on tick %d, %.1f percent of her 600 P3 pool; her last hit before it was %d, so she stood at %.1f percent just before it ", hr, enraged_tick, after, lh, before) .. SPEC.p3_enrage_threshold }
            end
        end
        -- the enrage and its tornado
        local torn = {}
        for _, r in ipairs(R.spawn) do
            if r.type == 8386 then torn[#torn + 1] = r end
        end
        if #torn >= 1 then
            local first_n = 0
            for _, r in ipairs(torn) do
                if r.tick == torn[1].tick then first_n = first_n + 1 end
            end
            out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_per_raider", first_n == 1,
                "measured " .. first_n .. " ratio, tornadoes (npc 8386) spawned on the enrage tick " .. torn[1].tick .. ", one raider in the room " .. SPEC.p3_tornado_per_raider }
            local hit_t
            for _, h in ipairs(R.hitp) do
                if h.npc_type == 8386 and not hit_t then hit_t = h end
            end
            if hit_t then
                local hp_before = hp_by_tick[hit_t.tick - 1] or hp_by_tick[hit_t.tick - 2]
                if hp_before and hp_before > 0 then
                    local pct = math.floor(100 * hit_t.damage / hp_before + 0.5)
                    out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_pct", math.floor(hp_before / 2) == hit_t.damage or math.floor((hp_before + 1) / 2) == hit_t.damage,
                        "measured " .. pct .. " percent, the tornado's hit of " .. hit_t.damage .. " on tick " .. hit_t.tick .. " against my " .. hp_before .. " hitpoints the tick before " .. SPEC.p3_tornado_pct }
                end
                for _, r in ipairs(torn) do
                    if r.tick > hit_t.tick and r.tick <= hit_t.tick + 30 then
                        out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_respawn", r.tick - hit_t.tick == 16,
                            "measured " .. (r.tick - hit_t.tick) .. " ticks, the next tornado spawn row against the hit on tick " .. hit_t.tick .. " " .. SPEC.p3_tornado_respawn }
                        break
                    end
                end
            end
        end
        for _, rw in ipairs(out_rows) do
            if (p3_stage == "late" or EARLY3[rw[1]]) and not emitted[rw[1]] then
                emitted[rw[1]] = true
                t.check(rw[1], rw[2], rw[3])
                if rw[1] == "spec.verzik.p3_enrage_threshold" and not rw[2] then enrage_bug = rw[3] end
            end
        end
        out_rows = {}
        end
        t.check("fight.loop_end", true, why_end .. " after " .. iter .. " iterations, phase " .. phase .. ", tick " .. tk)

        -- ---- evidence rows of the drive (narrative; the spec rows are computed below) -------------------------
        local sg = {}
        for _, s in ipairs(seg) do sg[#sg + 1] = s.tick .. ":" .. s.w end
        t.check("p1.weapons", true, "weapon segments " .. table.concat(sg, " ") .. "; " .. table.concat(notes, "; "))
        local pj = ""
        local part = 0
        for _, e in ipairs(plog) do
            pj = pj .. e .. " | "
            if #pj > 380 then
                part = part + 1
                t.check("drive.log" .. part, true, pj)
                pj = ""
            end
        end
        if pj ~= "" then t.check("drive.log" .. (part + 1), true, pj) end
        local hj = ""
        part = 0
        for _, e in ipairs(hplog) do
            hj = hj .. e .. " "
            if #hj > 380 then
                part = part + 1
                t.check("drive.hp" .. part, true, hj)
                hj = ""
            end
        end
        if hj ~= "" then t.check("drive.hp" .. (part + 1), true, hj) end
        t.check("p1.tobboss", tb_p1 ~= "", tb_p1)
        local line = ""
        for _, tr3 in ipairs(p2.trials) do
            local item = tr3.kind .. "@" .. tostring(tr3.A)
            for k2, v2 in pairs(tr3) do
                if k2 ~= "kind" and k2 ~= "A" then item = item .. " " .. k2 .. "=" .. tostring(v2) end
            end
            line = line .. item .. " | "
        end
        t.check("p2.trials", true, line ~= "" and line or "no trials")
        local sl = ""
        for _, s2 in ipairs(p2.slams) do sl = sl .. s2.kind .. "@" .. s2.T .. "/issued" .. s2.issued .. " " end
        t.check("p2.slams", true, sl ~= "" and sl or "none")
        local pl = ""
        for _, a in ipairs(p3.attacks) do pl = pl .. a.tick .. ":" .. a.seq .. " " end
        t.check("p3.attacks", true, pl .. "| enraged " .. tostring(p3.enraged) .. ", ball " .. tostring(p3.ball_seen))
        t.check("p2.tobboss", tb_p2 ~= "", tb_p2)
        t.check("p3.tobboss", tb_p3 ~= "", tb_p3)

        if death_tick ~= nil then
            t.ticks(8)
            local _, ml = t.msg.last(12)
            local mt2 = {}
            for _, m in ipairs(ml) do mt2[#mt2 + 1] = tostring(m.text) end
            local xr, xt = t.world.tile()
            t.check("verzik.exit", true, "boss npc_death row on tick " .. death_tick .. "; my tile " .. tostring(xt and (xt.x .. "," .. xt.z)) .. "; messages " .. table.concat(mt2, " | "))
        end
        if enrage_bug then
            t.blocked("content_bug: OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/scripts/tob_verzik.rs2:2338 enrages on an integer percent (divide(multiply(left, 100), pool) <= 20), so 125 of 600 hitpoints (20.8 percent) already enrages; spec verzik.p3_enrage_threshold (grade B, exact 20 percent, at or below): " .. enrage_bug)
            return
        end
        t.finish(0)
        return
    end,
}
