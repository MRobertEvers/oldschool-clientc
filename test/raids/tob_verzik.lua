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
        "::give armadyl_helmet 1", "::wield armadyl_helmet", "::give armadyl_chestplate 1", "::wield armadyl_chestplate",
        "::give armadyl_skirt 1", "::wield armadyl_skirt",
        "::give verzik_special_weapon 1",
        -- the P2 and P3 weapon and its ammunition (swapped to once the shield is down)
        "::give twisted_bow 1", "::give dragon_arrow 500", "::wield dragon_arrow",
        -- a brew and two prayer restores: Protect from Missiles/Magic and Rigour drain 99 prayer points by about tick 420 and a restore dose is worth more than a fish
        "::give br_4dosepotionofsaradomin 1", "::give br_4dosepotionofsaradomin 1", "::give br_4dosepotionofsaradomin 1", "::give br_4dosepotionofsaradomin 1", "::give br_4dose2restore 1", "::give br_4dose2restore 1",
        -- food for the fight: anglerfish heal 31 at 99 hitpoints (28 slots minus the five items above)
        "::give anglerfish 16", "::give br_4doserangerspotion 1", "::give staff_of_air 1", "::give death_rune 12",
        -- a charged serpentine helm makes a hit poisonous (tob_damage.rs2 ~tob_hit_is_poisonous): worn for one shot at the second Athanatos only
        "::give serpentine_helm_charged 1",
        -- the ranged set a Theatre raider shoots in for the P2 and P3 bow: worn at the P1 -> P2 transition, not before (the Dawnbringer wants the bare magic bonus)
        -- (worn from the start so the three backpack slots hold food: the fight is food-limited)
        -- ranging potion for the bow (drunk at the same transition)
    },
    run = function(t)
        -- the log is on BEFORE the room is entered, so the title card's sound and the pillars' placement are rows
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("verzik.ticklog", tl_ok == "ok", tostring(tl_detail))
        local r, d = t.raid.enter("tob", "verzik", { mode = "entry" })
        t.check("verzik.enter", r == "ok", tostring(d))
        local sr, st = t.raid.state()
        t.check("verzik.state", sr == "ok" and st.started == false, tostring(st and st.line))
        t.check("spec.scope", true, "mode=entry party=1")
        local boss_found, boss = t.npc.nearest("verzik_initial_story", 30)
        t.check("verzik.boss_present", boss_found == "ok", "boss row")

        -- the throne room scenery, read from the live scene before the fight (the throne seat is replaced during it)
        local loc_symbols = { "tob_dungeon_verzik_throne_window", "tob_dungeon_verzik_throne_window_back", "tob_dungeon_verzik_throne_right_side", "tob_dungeon_verzik_throne_left_side", "tob_dungeon_verzik_throne_right_side_back", "tob_dungeon_verzik_throne_left_side_back", "tob_dungeon_verzik_throne_wall_window", "tob_dungeon_verzik_throne_wall_window_light", "tob_dungeon_verzik_throne_wall", "tob_dungeon_verzik_throne_wall_corner", "tob_dungeon_verzik_entrance_door", "tob_dungeon_verzik_throne_floor_right_side", "tob_dungeon_verzik_throne_floor_left_side", "tob_dungeon_verzik_throne_floor_right_side_bottom", "tob_dungeon_verzik_throne_floor_left_side_botom", "tob_dungeon_verzik_throne_floor_blank", "tob_dungeon_verzik_throne_floor_steps_left", "tob_dungeon_verzik_throne_floor_steps_middle", "tob_dungeon_verzik_throne_floor_steps_right", "tob_dungeon_verzik_throne_side_blocker", "tob_dungeon_verzik_throne_side_blocker_end_right", "tob_dungeon_verzik_throne_side_blocker_end_left", "tob_dungeon_verzik_death_cage", "tob_dungeon_verzik_floor_tile_middle_a", "tob_dungeon_verzik_floor_tile_middle_b", "tob_dungeon_verzik_floor_tile_middle_c", "tob_dungeon_verzik_floor_tile_middle_d", "tob_dungeon_verzik_floor_tile_middle_e", "tob_dungeon_verzik_floor_tile_middle_f", "tob_dungeon_verzik_floor_tile_middle_g", "tob_dungeon_verzik_floor_tile_middle_h", "tob_dungeon_verzik_floor_tile_middle_i", "tob_dungeon_verzik_floor_tile_carpet_edge", "tob_dungeon_verzik_floor_tile_outside_straight_a", "tob_dungeon_verzik_floor_tile_outside_straight_b", "tob_dungeon_verzik_floor_tile_outside_corner", "tob_dungeon_verzik_floor_tile_outside_straight_a_end", "tob_dungeon_verzik_skeleton", "tob_dungeon_verzik_throne_empty", "tob_verzik_throne_barrier_connector_right", "tob_verzik_throne_barrier_connector_left", "tob_walkway_verzik_barrier" }
        local locs_found, locs_missing = 0, {}
        for _, sym in ipairs(loc_symbols) do
            local lr2 = t.world.loc_near(sym, 0)
            if lr2 == "ok" then locs_found = locs_found + 1 else locs_missing[#locs_missing + 1] = sym end
        end
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

        local tr, td = t.player.talk_to("verzik_initial_story", 1)
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
        local t12 = { done = 0, hold = nil, walk_tk = nil }
        local p2 = { kite_walk = -10, kite_from = 0, pending = {}, tries = {}, exp = nil, valid = { J1 = 0, J2 = 0, E1 = 0, E4 = 0, E5 = 0 }, need = { J1 = 1, J2 = 1, E1 = 1, E4 = 1, E5 = 1 },
            trials = {}, last_atk = -10, aims = {}, mode = "exp", started = 0, slams = {}, last_target = "", fight_from = 0, tb_sent = 0, dodges = 0, reds_prayer = 0, reds_tb = 0 }
        local pool_diag = ""
        local p3 = { pray_at = {}, switches = {}, autos = 0, plan = "ADJ", next_T = 0, last_atk = -10, specials = {}, attacks = {}, dealt_cap = 99999,
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
                    if row.to_type == 10832 then leave_tick = row.tick
                    elseif row.to_type == 10833 then p2id_tick = row.tick
                    elseif row.to_type == 10835 then p3id_tick = row.tick end
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
            if hpv < 6 and fish == 0 and brewn == 0 then why_end = "hp " .. hpv .. " with no food" break end

            if p3_emit ~= nil then
                local p3_stage = p3_emit
                p3_emit = nil
            -- ======================= PHASE 3 SPEC ROWS (from the tick log) =======================
            local A3, specials3, balls3 = {}, {}, {}
            for _, r in ipairs(R.anim) do
                if r.slot == boss_slot and r.type == 10835 then
                    if r.seq == 8123 or r.seq == 8124 or r.seq == 8125 then A3[#A3 + 1] = r
                    elseif r.seq ~= 8119 and r.seq ~= 8118 then specials3[#specials3 + 1] = r end
                end
            end
            for _, r in ipairs(R.proj) do
                if r.spotanim == 1598 then balls3[#balls3 + 1] = r end
            end
            if p3id_tick and E_tick then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_id_after_phase_event", p3id_tick - E_tick >= 5 and p3id_tick - E_tick <= 7,
                    "measured " .. (p3id_tick - E_tick) .. " ticks, 8118 on tick " .. E_tick .. ", the P3 id (retype to 10835) on tick " .. p3id_tick .. " " .. SPEC.p3_id_after_phase_event }
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
            local auto_used = {}
            for _, pr in ipairs(R.proj) do
                if pr.spotanim == 1593 or pr.spotanim == 1594 then
                    local f = math.floor(pr.end_cycle / 30)
                    for _, h in ipairs(R.hitp) do
                        if h.npc_slot == boss_slot and h.npc_type == 10835 and h.tick >= pr.tick + 2 and h.tick <= pr.tick + 5 and not auto_used[h.serial] then
                            auto_used[h.serial] = true
                            n_auto_hits = n_auto_hits + 1
                            if h.damage > auto_max then auto_max = h.damage end
                            break
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
                    if r.delay == 0 then pn_by_tick[r.tick] = pn_by_tick[r.tick] + 1 end
                end
                local pl_max, pl_len, pl_list = 0, 0, {}
                for _, r in ipairs(pools) do
                    if r.tick == pools[1].tick then
                        pl_list[#pl_list + 1] = r.delay
                        if r.delay > pl_max then pl_max = r.delay end
                        if r.delay > 0 and (pl_len == 0 or r.delay < pl_len) then pl_len = r.delay end
                    end
                end
                local pl_ticks = (pl_max + pl_len) / 30
                out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pool_lifetime", pl_ticks == 14,
                    "measured " .. pl_ticks .. " ticks, the pool graphic 1595 set on tick " .. pools[1].tick .. " with delays " .. table.concat(pl_list, ",") .. " cycles (last delay " .. pl_max .. " plus one graphic length of " .. pl_len .. " cycles) " .. SPEC.p3_yellow_pool_lifetime }
                out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pools", pn_by_tick[pools[1].tick] == 1,
                    "measured " .. pn_by_tick[pools[1].tick] .. " ratio, pools set on the cast tick, one raider in the room " .. SPEC.p3_yellow_pools }
                local nxt
                for _, a in ipairs(A3) do
                    if a.tick > pools[1].tick and not nxt then nxt = a.tick - (pools[1].tick + pl_ticks) end
                end
                if nxt then
                    out_rows[#out_rows + 1] = { "spec.verzik.p3_after_yellows", nxt == 7,
                        "measured " .. nxt .. " ticks, from the pool's end (set on " .. pools[1].tick .. " for " .. pl_ticks .. " ticks) to her next auto " .. SPEC.p3_after_yellows }
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
                    if h.slot == boss_slot and h.type == 10835 and h.tick <= enraged_tick and h.tick > lt then lt = h.tick lh = h.damage end
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
                if r.type == 10846 then torn[#torn + 1] = r end
            end
            if #torn >= 1 then
                local first_n = 0
                for _, r in ipairs(torn) do
                    if r.tick == torn[1].tick then first_n = first_n + 1 end
                end
                out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_per_raider", first_n == 1,
                    "measured " .. first_n .. " ratio, tornadoes (npc 10846) spawned on the enrage tick " .. torn[1].tick .. ", one raider in the room " .. SPEC.p3_tornado_per_raider }
                local hit_t, hit_half
                for _, h in ipairs(R.hitp) do
                    if h.npc_type == 10846 then
                    local hpb = hp_by_tick[h.tick - 1] or hp_by_tick[h.tick - 2]
                    local half = hpb and (math.floor(hpb / 2) == h.damage or math.floor((hpb + 1) / 2) == h.damage)
                    if (not hit_t) or (half and not hit_half) then hit_t = h hit_half = half end
                end
                end
                if hit_t then
                    local hp_before = hp_by_tick[hit_t.tick - 1] or hp_by_tick[hit_t.tick - 2]
                    if hp_before and hp_before > 0 then
                        local pct = math.floor(100 * hit_t.damage / hp_before + 0.5)
                    if math.floor(hp_before / 2) == hit_t.damage or math.floor((hp_before + 1) / 2) == hit_t.damage then pct = 50 end
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
                    if r.slot == boss_slot and r.type == 10833 then
                        if r.seq == 8114 or r.seq == 8116 then A2[#A2 + 1] = r end
                        if r.seq == 8117 then heals2[#heals2 + 1] = r end
                    end
                end
                local id_gap = (p2id_tick and leave_tick) and (p2id_tick - leave_tick) or -1
                out_rows[#out_rows + 1] = { "spec.verzik.p2_id_after_phase_event", id_gap >= 12 and id_gap <= 14,
                    "measured " .. id_gap .. " ticks, the P1 form left on tick " .. tostring(leave_tick) .. " (retype 10831 to 10832) and the P2 id came on " .. tostring(p2id_tick) .. " " .. SPEC.p2_id_after_phase_event }
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
                            if r.type == 10844 and r.tick >= casts[i - 1].tick and r.tick < casts[i].tick then
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
                    if r.type == 10844 then ath[#ath + 1] = r end
                    if r.type == 10841 or r.type == 10842 or r.type == 10843 then crabs[#crabs + 1] = r end
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
                            "measured " .. land .. " ticks, the Athanatos (npc 10844) spawn row against its cast " .. SPEC.p2_purple_land }
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
                    if r.type == 10844 and r.spotanim == 1587 then
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
                    if h.type == 10841 or h.type == 10842 or h.type == 10843 then
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
                    if r.type == 10845 then reds[#reds + 1] = r end
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
                    local hr = tonumber(string.match(tb_reds, "phase_hp=(%d+) of"))
                    if hr and reds_tick > 0 then
                        local lt, lh = 0, 0
                        for _, h in ipairs(R.hitn) do
                            if h.slot == boss_slot and h.type == 10833 and h.tick <= reds_tick and h.tick > lt then lt = h.tick lh = h.damage end
                        end
                        local after = hr * 100 / 400
                        local before = (hr + lh) * 100 / 400
                        out_rows[#out_rows + 1] = { "spec.verzik.reds_threshold", after <= 35 and before > 35,
                            string.format("measured 35 percent, ::tobboss read %d hitpoints right after the Matomenos summon on tick %d, %.1f percent of her P2 pool (phase_hp of 400); her last hit before it was %d, so she stood at %.1f percent just before it (the summon came on the first hit under 35) ", hr, reds_tick, after, lh, before) .. SPEC.reds_threshold }
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
                                -- a slam or a stomp (8116) landing on the same tick is not a bomb hit
                                for _, a8 in ipairs(R.anim) do
                                    if a8.slot == boss_slot and a8.seq == 8116 and (a8.tick == h.tick or a8.tick == h.tick - 1) then other = true end
                                end
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
                    t.player.attack("verzik_phase1_story", 2, 6)
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
                        t.player.attack("verzik_phase1_story", 2, 1)
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
                        if h.slot == boss_slot and h.type == 10831 then
                            shield_dealt = shield_dealt + h.damage
                            if h.damage >= 40 then big = big + 1 end
                        end
                    end
                    local hw = { [1] = true, [2] = true, [4] = true }
                    local want_hide = (hw[w] and ph <= 2) or (ph >= 12 and hw[w + 1])
                    if shield_dealt >= 300 then
                        -- the shield is gone: no more swings (a press would walk me after the dying form); stand south of the west pillar,
                        -- which falls with the shield three ticks from now on whoever is within two tiles of it
                        if p1.pos ~= "fall" then
                            t.player.walk_to(HIDE[1][1], HIDE[1][2], 1)
                            p1.pos = "fall"
                            plog[#plog + 1] = tk .. ":shield dealt " .. shield_dealt .. ", to the pillar for its collapse"
                        end
                    elseif want_hide then
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
                        -- the magic cap (spec p1_cap 10,3,3): a wind bolt from the staff after the bow's first swings, until one cast lands damage
                        local mag_best = 0
                        for _, h in ipairs(R.hitn) do
                            if h.slot == boss_slot and h.type == 10831 then
                                local wp = "fists"
                                for _, sg in ipairs(seg) do
                                    if sg.tick <= h.tick - 2 then wp = sg.w end
                                end
                                if wp == "staff_of_air" and h.damage > mag_best then mag_best = h.damage end
                            end
                        end
                        if w == 3 then
                            want = "twisted_bow"
                            if ph >= 6 and mag_best < 3 then want = "staff_of_air" end
                        elseif w >= 4 and mag_best < 3 and p1.presses < 40 then
                            want = "staff_of_air"
                        end
                                                if p1.worn ~= want then
                            if want == "dawnbringer" then t.player.equip("verzik_special_weapon")
                            elseif want == "twisted_bow" then
                                t.player.equip("twisted_bow")
                                t.player.equip("dragon_arrow")
                            else t.player.equip("staff_of_air") end
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
                                t.player.attack("verzik_phase1_story", 2, 3)
                                plog[#plog + 1] = tk .. ":special bar " .. tostring(wr) .. "/" .. tostring(ir)
                                p1.spec_tries = p1.spec_tries + 1
                                p1.last_spec = tk
                                p1.last_atk = tk
                                plog[#plog + 1] = tk .. ":w" .. w .. " special press " .. p1.spec_tries .. " (big " .. big .. ", shield dealt " .. shield_dealt .. ")"
                            elseif tk >= p1.last_atk + 3 then
                                local pr, pd
                                if want == "staff_of_air" then
                                    pr, pd = t.player.cast("wind_blast", "verzik_phase1_story", 1, 2)
                                else
                                    pr, pd = t.player.attack("verzik_phase1_story", 2, 1)
                                end
                                p1.last_atk = tk
                                p1.presses = p1.presses + 1
                                if p1.presses <= 16 then
                                    local _, ptk = t.tick()
                                    plog[#plog + 1] = tk .. ":w" .. w .. " " .. want .. " press " .. tostring(pr) .. " (" .. (ptk - tk) .. " ticks) " .. string.sub(tostring(pd), 1, want == "staff_of_air" and 420 or 90)
                                end
                            end
                        end
                    end
                end

            -- ======================= TRANSIT PHASE 1 -> 2 ========================
            elseif phase == "T12" then
                if t12.hold ~= -1 then
                    -- every pillar falls with the shield on anyone within two tiles: take the collapse standing south of the west pillar,
                    -- then ask for a step every tick; the first one that moves me ends the stun
                    if t12.hold == nil then
                        t12.hold = tk
                    elseif #pillar_retypes >= 6 then
                        if t12.walk_tk == nil then t12.walk_tk = tk end
                        if tk >= t12.walk_tk + 8 then t12.hold = -1 else t.player.walk_to(6427, 91, 1) end
                    elseif tk > t12.hold + 16 then
                        t12.hold = -1
                    end
                elseif t12.done == 0 then
                    t12.done = 1
                    t.prayer.set("protectfrommagic", false)
                    t.prayer.set("protectfrommissiles", true)
                    t.cheat("::tobboss", false)
                    t.player.equip("twisted_bow")
                    t.player.equip("dragon_arrow")
                    seg[#seg + 1] = { tick = tk, w = "twisted_bow" }
                    local worn_ok = 0
                    for _, piece in ipairs({ "armadyl_helmet", "armadyl_chestplate", "armadyl_skirt" }) do
                        local _, in_pack = t.inv.count(piece)
                        if (in_pack or 0) == 0 then worn_ok = worn_ok + 1 end
                    end
                    local dr = t.player.inv_op("br_4doserangerspotion", 1)
                    t.check("p2.ranged_kit", worn_ok == 3, "worn " .. worn_ok .. " of 3 ranged armour pieces at the P1 -> P2 transition, ranging potion press " .. tostring(dr))
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
                    if h.slot == boss_slot and h.type == 10831 then
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
                local magic_max, magic_n = 0, 0
                local _, cap_anims_rows = t.ticklog.rows({ kind = "player_anim" })
                local cap_anims = {}
                for _, pa in ipairs(cap_anims_rows or {}) do cap_anims[#cap_anims + 1] = { tick = pa.tick, seq = tonumber(pa.seq or pa.a or pa.b) } end
                local cap_diag = ""
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 10831 then
                        -- the hit belongs to the swing that launched it: fists land +1, the Dawnbringer +4, a bow or a spell the latest 426 / 1162 within 2..6 ticks
                        local wp = "other"
                        local anim_at = {}
                        for _, pa in ipairs(cap_anims) do anim_at[pa.tick] = pa.seq end
                        if anim_at[h.tick - 4] == 1167 then wp = "dawnbringer"
                        elseif anim_at[h.tick - 1] == 422 then wp = "fists"
                        else
                            for back = 2, 6 do
                                local sq = anim_at[h.tick - back]
                                if wp == "other" and sq == 426 then wp = "twisted_bow" end
                                if wp == "other" and sq == 1162 then wp = "staff_of_air" end
                            end
                        end
                        if wp == "fists" then melee_n = melee_n + 1 if h.damage > melee_max then melee_max = h.damage end end
                        if wp == "twisted_bow" then ranged_n = ranged_n + 1 if h.damage > ranged_max then ranged_max = h.damage end cap_diag = cap_diag .. h.tick .. ":" .. h.damage .. " " end
                        if wp == "staff_of_air" then magic_n = magic_n + 1 if h.damage > magic_max then magic_max = h.damage end end
                    end
                end
                out_rows[#out_rows + 1] = { "tech.p1_cap_melee_ranged", melee_max == 10 and ranged_max == 3,
                    "largest shield hit with bare fists " .. melee_max .. " over " .. melee_n .. " hits (cap 10), with the Twisted bow " .. ranged_max .. " over " .. ranged_n .. " hits (cap 3, bow hits tick:damage " .. cap_diag .. "); wind bolt hits ".. magic_n .. " with the largest " .. magic_max .. " (cap 3)" }
                out_rows[#out_rows + 1] = { "spec.verzik.p1_cap", melee_max == 10 and ranged_max == 3 and magic_max == 3,
                    "measured " .. melee_max .. "," .. ranged_max .. "," .. magic_max .. " hp, largest shield hit by fists/Dawnbringer-less melee, Twisted bow and wind bolt (" .. melee_n .. " fist, " .. ranged_n .. " bow, " .. magic_n .. " staff hits) " .. SPEC.p1_cap }
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
                        if (h.tick == c or h.tick == c + 1) and h.npc_slot == pr.slot then
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
                        if row.slot == boss_slot and row.type == 10833 and (row.seq == 8114 or row.seq == 8116) then new_atk = row end
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
                    local ath_n = 0
                    for _, row in ipairs(R.spawn) do if row.type == 10844 and row.tick <= tk then ath_n = ath_n + 1 end end
                    for _, row in ipairs(R.spawn) do
                        if row.type == 10841 or row.type == 10842 or row.type == 10843 or row.type == 10844 or row.type == 10845 then
                            local gone = false
                            for _, d2 in ipairs(R.death) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                            for _, d2 in ipairs(R.free) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                            if not gone then
                                if row.type == 10844 then ath_alive = row
                                elseif row.type == 10845 then red_alive = row
                                else crab_alive = row end
                            end
                        end
                    end
                    local acted = false
                    local ath_deaths, last_reds = 0, 0
                    for _, d2 in ipairs(R.death) do
                        if d2.type == 10844 then ath_deaths = ath_deaths + 1 end
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
                    -- the first crab of the phase is kited round her body until it dies of age (spec p2_crab_lifetime): head for the ring corner
                    -- opposite the crab's nearest corner and keep it until reached; never path beside her body (a slam knocks me into the crab)
                    if not acted and crab_alive and p2.kite_state ~= "done" and (p2.kite_slot == nil or p2.kite_slot == crab_alive.slot) then
                        if p2.kite_slot == nil then p2.kite_slot, p2.kite_from = crab_alive.slot, tk end
                        local ring = { { 6427, 85 }, { 6427, 95 }, { 6437, 95 }, { 6437, 85 } }
                        local csym_k = { [10841] = "verzik_nylocas_melee_story", [10842] = "verzik_nylocas_ranged_story", [10843] = "verzik_nylocas_magic_story" }
                        local _, crow = t.npc.nearest(csym_k[crab_alive.type] or "verzik_nylocas_ranged_story", 30)
                        local cx_k, cz_k = crab_alive.x + 1, crab_alive.z + 1
                        if crow then cx_k, cz_k = crow.x + 1, crow.z + 1 end
                        local mi, md, ci = 1, 999, 1
                        local cd = 999
                        for i, c in ipairs(ring) do
                            local dd = math.max(math.abs(c[1] - mt.x), math.abs(c[2] - mt.z))
                            if dd < md then mi, md = i, dd end
                            local dc = math.max(math.abs(c[1] - cx_k), math.abs(c[2] - cz_k))
                            if dc < cd then ci, cd = i, dc end
                        end
                        local nxt = p2.kite_to
                        if nxt and mt.x == ring[nxt][1] and mt.z == ring[nxt][2] then nxt = nil end
                        if nxt == ci then nxt = nil end
                        if nxt == nil then
                            if md > 0 then
                                local bd = 999
                                for i, c in ipairs(ring) do
                                    local dd = math.max(math.abs(c[1] - mt.x), math.abs(c[2] - mt.z))
                                    if i ~= ci and dd < bd then nxt, bd = i, dd end
                                end
                            else
                                local fwd, back = mi % 4 + 1, (mi + 2) % 4 + 1
                                local goal = (ci + 1) % 4 + 1
                                if mi == goal then nxt = mi else nxt = (fwd ~= ci) and fwd or back end
                            end
                        end
                        p2.kite_to = nxt
                        if hpv < 50 and fish > 0 and tk >= last_eat + 3 then
                            t.player.inv_op("anglerfish", 1)
                            last_eat = tk
                            plog[#plog + 1] = tk .. ":eat"
                        elseif (mt.x ~= ring[nxt][1] or mt.z ~= ring[nxt][2]) and tk >= p2.kite_walk + 1 then
                            t.player.walk_to(ring[nxt][1], ring[nxt][2], 1)
                            p2.kite_walk = tk
                            plog[#plog + 1] = tk .. ":kite to corner " .. nxt .. " (crab corner " .. ci .. ")"
                        end
                        if tk > p2.kite_from + 60 then p2.kite_state = "done" end
                        acted = true
                    elseif p2.kite_slot ~= nil and p2.kite_state ~= "done" and not crab_alive then
                        p2.kite_state = "done"
                        plog[#plog + 1] = tk .. ":kited crab gone"
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
                            if row.slot == boss_slot and row.type == 10833 and (row.seq == 8114 or row.seq == 8116) then lastA = row.tick end
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
                    -- the first blood spell is taken unprayed (verzik.p2_heal_spell_fraction needs an unprayed cast: heal = floor(raw / 2)); Protect from Magic goes up once it has landed
                    local bf_land = false
                    for _, prr in ipairs(R.proj) do
                        if prr.spotanim == 1591 and prr.tick >= last_reds and tk >= prr.tick + 5 then bf_land = true end
                    end
                    if last_reds > 0 and p2.reds_prayer == 0 and not acted and (bf_land or tk >= last_reds + 45) then
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
                        if (hpv < 65 or (crab_alive and hpv < 88)) and tk >= last_eat + 3 and (fish > 0 or brewn > 0) then
                            if fish > 0 then t.player.inv_op("anglerfish", 1) else t.player.inv_op(brew_name, 1) end
                            last_eat = tk
                            acted = true
                        elseif crab_alive and p2.mode == "exp" then
                            local csym = { [10841] = "verzik_nylocas_melee_story", [10842] = "verzik_nylocas_ranged_story", [10843] = "verzik_nylocas_magic_story" }
                            t.player.attack(csym[crab_alive.type] or "verzik_nylocas_ranged_story", 2, 1)
                            acted = true
                        elseif ath_alive and p2.mode == "exp" and tk <= ath_alive.tick + 70 and tk >= ath_alive.tick + (ath_n >= 2 and 11 or 2) and tk >= p2.last_atk + 3 then
                            if ath_n >= 2 and p2.helm == nil then p2.helm = tk t.player.equip("serpentine_helm_charged") end
                            -- the Athanatos heals her every beat: it is shot before the experiments, not after
                            t.player.attack("tob_verzik_phase2_armourednylocas_story", 2, 1)
                            p2.last_atk = tk
                            acted = true
                        elseif p2.mode == "exp" and p2.exp == nil and tk >= p2.last_atk + 4 and new_atk == nil and mt.x >= 6426 and mt.x <= 6429 and mt.z == 91 then
                            -- between experiments the bow keeps working from the home tile, up to 200 dealt (the pool is 400: the trials must finish before the floor)
                            local dealt2 = 0
                            for _, h in ipairs(R.hitn) do
                                if h.slot == boss_slot and h.type == 10833 then dealt2 = dealt2 + h.damage end
                            end
                            if dealt2 < 200 then
                                t.player.attack("verzik_phase2_story", 2, 1)
                                p2.last_atk = tk
                                acted = true
                            end
                        elseif p2.mode == "fight" then
                            if mt.x >= 6430 or mt.x <= 6424 then
                                t.player.walk_to(6427, 91, 6)
                            else
                                local target = "verzik_phase2_story"
                                if crab_alive then
                                    local csym = { [10841] = "verzik_nylocas_melee_story", [10842] = "verzik_nylocas_ranged_story", [10843] = "verzik_nylocas_magic_story" }
                                    target = csym[crab_alive.type] or "verzik_nylocas_ranged_story"
                                elseif ath_alive and tk <= math.max(ath_alive.tick, p2.fight_from) + 70 and (ath_n < 2 or tk >= ath_alive.tick + 11) then
                                    target = "tob_verzik_phase2_armourednylocas_story"
                                    if ath_n >= 2 and p2.helm == nil then p2.helm = tk t.player.equip("serpentine_helm_charged") end
                                end
                                -- an Athanatos is shot for 24 ticks after it lands (it heals her every beat); one that survives that is left standing
                                if red_alive ~= nil and p2.red_first == nil then p2.red_first = red_alive.tick end
                                -- the first Matomenos is left to reach her: its death (absorb, 1587 on her) is a row, so fire is held until it is gone
                                local red_wait = (p2.red_first ~= nil and p2.red_done == nil and tk <= p2.red_first + 40)
                                if p2.red_first ~= nil and red_alive == nil and tk > p2.red_first then p2.red_done = tk end
                                local absorb = ((last_reds > 0 and tk <= last_reds + 5 and not crab_alive) or red_wait)
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
                    if tk % 12 == 0 then plog[#plog + 1] = tk .. ":prayer " .. tostring(prn.current or prn.level) end
                    if (prn.current or prn.level) < 35 then
                        for _, pn in ipairs({ "br_1dose2restore", "br_2dose2restore", "br_3dose2restore", "br_4dose2restore" }) do
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
                    if h.slot == boss_slot and h.type == 10833 and h.tick > last_p2_tick then last_p2_tick = h.tick last_p2_hit = h.damage end
                end
                local s1_total, s1_last = 0, 0
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 10831 then s1_total = s1_total + h.damage s1_last = h.damage end
                end
                local p1_ok = (s1_total - s1_last) < 300 and s1_total >= 300
                local ph2 = tonumber(string.match(tb_p2, "phase_hp=%d+ of (%d+)"))
                local ph3 = tonumber(string.match(tb_p3, "phase_hp=%d+ of (%d+)"))
                if h3 and ph2 and ph3 then
                    -- seam8: ::tobboss states the phase's own pool (phase_hp X of POOL) beside the HUD total (hp of 1000 = P2 + P3, of 600 on the P3 form)
                    local ph2_now = tonumber(string.match(tb_p2, "phase_hp=(%d+) of")) or 0
                    local ph3_now = tonumber(string.match(tb_p3, "phase_hp=(%d+) of")) or 0
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_p3_hp_1p", p1_ok and ph3 == 600,
                        "measured " .. ph3 .. " hp, ::tobboss phase_hp on the P3 form read " .. ph3_now .. " of " .. ph3 .. " (total hp " .. h3 .. " of " .. (string.match(tb_p3, "hp=%d+ of (%d+)") or "?") .. "), the barrier's pool 300 + P2 + P3 with the P1 shield bracketed at 300: " .. tostring(p1_ok) .. " " .. SPEC.entry_p3_hp_1p }
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_p2_hp_1p", p1_ok and ph2 == 400,
                        "measured " .. ph2 .. " hp, ::tobboss phase_hp on the P2 form read " .. ph2_now .. " of " .. ph2 .. " (total hp " .. (string.match(tb_p2, "hp=(%d+) of") or "?") .. " of " .. (string.match(tb_p2, "hp=%d+ of (%d+)") or "?") .. " = P2 + P3), P1 shield bracketed at 300: " .. tostring(p1_ok) .. " " .. SPEC.entry_p2_hp_1p }
                end
                local d1 = tonumber(string.match(tb_pre, "def=(%d+) of"))
                local d2 = tonumber(string.match(tb_p2, "def=(%d+) of"))
                local d3 = tonumber(string.match(tb_p3, "def=(%d+) of"))
                if d1 and d2 and d3 then
                    out_rows[#out_rows + 1] = { "spec.verzik.entry_defence", d1 == 10 and d2 == 120 and d3 == 120,
                        "measured " .. d1 .. "," .. d2 .. "," .. d3 .. " count, ::tobboss def on the P1 form (read before the fight), the P2 and the P3 forms " .. SPEC.entry_defence }
                end
                for _, rw in ipairs(out_rows) do t.check(rw[1], rw[2], rw[3]) end
                out_rows = {}
                end
                local new_atk
                for _, row in ipairs(new.anim) do
                    if row.slot == boss_slot and row.type == 10835 then
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
                    if h.slot == boss_slot and h.type == 10835 then dealt = dealt + h.damage end
                end
                for _, row in ipairs(new.spawn) do
                    if row.type == 10846 and p3.enraged == 0 then p3.enraged = row.tick enraged_tick = row.tick end
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
                local _, _, pset3 = t.prayer.read()
                for _, row in ipairs(new.proj) do
                    if row.spotanim == 1598 then
                        -- the green ball is told by projectile 1598 (always on 8125); no prayer stops it
                        p3.ball_seen = row.tick
                    elseif row.spotanim == 1593 then
                        -- ranged auto: the prayer counts when the projectile LANDS (2-3 ticks on), so the switch on the launch tick is in time; two unprayed samples first for the maxima rows
                        p3.mag_seen = (p3.mag_seen or 0) + 1
                        if p3.mag_seen > 2 then
                            local sw_res = t.prayer.set("protectfrommissiles", true)
                            p3.switches[row.tick] = { at = tk, was_on = (pset3 and pset3.protectfrommissiles == true) or false, style = 1593, res = sw_res }
                        end
                    elseif row.spotanim == 1594 then
                        p3.rng_seen = (p3.rng_seen or 0) + 1
                        if p3.rng_seen > 2 then
                            local sw_res = t.prayer.set("protectfrommagic", true)
                            p3.switches[row.tick] = { at = tk, was_on = (pset3 and pset3.protectfrommagic == true) or false, style = 1594, res = sw_res }
                        end
                    end
                end
                if p3.ball_seen > 0 then p3.dealt_cap = 99999 elseif p3.dealt_cap > 400 then p3.dealt_cap = 400 end
                -- plan the next attack's position by the autos seen
                local plans = { [1] = "UNDER", [2] = "ADJ", [3] = "STEPON", [4] = "HOME", [5] = "STEPON" }
                if new_atk then
                    p3.plan = plans[p3.autos] or "HOME"
                    p3.stepped = 0
                end
                -- the prayer follows the style of the attack just seen (the ranged and magic hits land three ticks after the animation);
                -- the first two magic autos and the melee autos of the experiments are left unprayed so the maxima rows have hits to read
                if new_atk and new_atk.seq == 8123 and p3.autos > 9 then
                    t.prayer.set("protectfrommelee", true)
                end
                if p3.autos >= 10 and p3.early_done == nil then
                    p3.early_done = 1
                    p3_emit = "early"
                end
                local sp_n = #p3.specials
                if sp_n >= 1 and p3.specials[1].seq ~= nil and p3.autos == 4 then p3.plan = "ADJ" end
                local vr, vrow = t.npc.state("verzik_phase3_story")
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
                    if row.tick >= p3id_tick and (row.type == 8376 or row.type == 10841 or row.type == 10842 or row.type == 10843 or row.type == 10846) then
                        local gone = false
                        for _, d2 in ipairs(R.death) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                        for _, d2 in ipairs(R.free) do if d2.slot == row.slot and d2.tick >= row.tick then gone = true end end
                        if not gone then
                            if row.type == 8376 then web_alive = row
                            elseif row.type == 10846 then tornado_alive = row
                            else crab_alive = row end
                        end
                    end
                end
                -- ---- 1. hitpoints ---------------------------------------------------------------
                local ball_due = (p3.ball_seen > 0 and tk <= p3.ball_seen + 6)
                -- after the ball the player stays low on purpose: a tornado takes half of the CURRENT hitpoints (min 5) and heals her three times that
                local after_ball = false
                if (hpv < (after_ball and 28 or 60) or (ball_due and hpv < 85)) and tk >= last_eat + 3 and (fish > 0 or brewn > 0) then
                    if fish > 0 then t.player.inv_op("anglerfish", 1) else t.player.inv_op(brew_name, 1) end
                    last_eat = tk
                    acted = true
                end
                -- ---- 2. yellow pool: stand on it until the blast ----------------------------------
                if not acted and p3.pool and tk <= p3.pool.tick + 16 then
                    pool_diag = pool_diag .. tk .. ":" .. mt.x .. "," .. mt.z .. "/" .. p3.pool.x .. "," .. p3.pool.z .. " "
                    if mt.x ~= p3.pool.x or mt.z ~= p3.pool.z then
                        t.player.walk_to(p3.pool.x, p3.pool.z, 4)
                    end
                    acted = true
                end
                -- ---- 3. tornado flee ----------------------------------------------------------------
                if not acted and tornado_alive then
                    -- the tornado is not interactable: its tile comes from the npc_tile rows (DRIVER_NOTES "Verzik enrage")
                    local _, trs = t.ticklog.rows({ kind = "npc_tile", type = 10846, slot = tornado_alive.slot, since = tornado_alive.serial })
                    -- no npc_tile row yet: it stands where it spawned, her south-west tile (the first rows of every tornado begin at 6431,91)
                    local trow = trs[#trs] or { x = 6431, z = 91 }
                    local touched = false
                    for _, h in ipairs(R.hitp) do if h.npc_type == 10846 then touched = true end end
                    if trow then
                        local dx, dz = mt.x - trow.x, mt.z - trow.z
                        local dist = math.max(math.abs(dx), math.abs(dz))
                        local want_touch = (not touched) and hpv >= 90 and fish >= 3 and not ball_due
                        if (not touched) and hpv < 90 and dist >= 5 and fish > 0 and tk >= last_eat + 3 then
                            t.player.inv_op("anglerfish", 1)
                            last_eat = tk
                            acted = true
                        elseif (not want_touch) and dist <= 6 then
                            -- outrun it (seam12 recipe): run the ring three tiles off her body, to the ring tile farthest from the tornado whose path does not cross it;
                            -- two tiles off the walls (a corner is a trap), within four tiles of a live yellow pool
                            local fbest, fscore = nil, nil
                            local cands = {}
                            for kk = -3, 9, 2 do
                                cands[#cands + 1] = { bx + kk, bz - 3 }
                                cands[#cands + 1] = { bx + kk, bz + 9 }
                                cands[#cands + 1] = { bx - 3, bz + kk }
                                cands[#cands + 1] = { bx + 9, bz + kk }
                            end
                            for cx0 = 6421, 6443, 2 do
                                for cz0 = 81, 97, 2 do
                                    if math.max(bx - cx0, 0, cx0 - (bx + 6), bz - cz0, 0, cz0 - (bz + 6)) >= 2 then cands[#cands + 1] = { cx0, cz0 } end
                                end
                            end
                            for ci, cd in ipairs(cands) do
                                local cx, cz = math.min(6443, math.max(6421, cd[1])), math.min(97, math.max(81, cd[2]))
                                local dm = math.max(math.abs(cx - mt.x), math.abs(cz - mt.z))
                                local dt = math.max(math.abs(cx - trow.x), math.abs(cz - trow.z))
                                local near_pool = (not (p3.pool and tk <= p3.pool.tick + 15)) or math.max(math.abs(cx - p3.pool.x), math.abs(cz - p3.pool.z)) <= 4
                                if dm >= 2 and dm <= 8 and dt >= dist and near_pool then
                                    local hx, hz = math.floor((cx + mt.x) / 2), math.floor((cz + mt.z) / 2)
                                    if math.max(math.abs(hx - trow.x), math.abs(hz - trow.z)) >= 3 then
                                        local edge = math.min(cx - 6421, 6443 - cx, cz - 81, 97 - cz)
                                        local sc = dt * 10 - dm + math.min(edge, 3) * 12 - ((edge < 2) and 40 or 0) - ((ci > 4 * 7) and 15 or 0)
                                        if fscore == nil or sc > fscore then fbest, fscore = { cx, cz }, sc end
                                    end
                                end
                            end
                            if fbest and tk >= p3.last_flee + 1 then
                                t.player.walk_to(fbest[1], fbest[2], 1)
                                p3.last_flee = tk
                            end
                            acted = true
                        end
                    end
                end
                -- ---- 4a. step off the tile the web is thrown at: it lands three ticks after the cast, binds only a player standing on it,
                -- and a bound player cannot shoot the web (the walk-trigger of the freeze eats the attack)
                for _, row in ipairs(new.anim) do
                    if row.slot == boss_slot and row.type == 10835 and row.seq == 8127 then p3.web_cast = row.tick end
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
                -- a web is shot with the Dawnbringer: the twisted bow scales down against a web's magic level (1, 4, 0 in three hits)
                if not acted and web_alive and not p3.on_dawn then
                    t.player.equip("verzik_special_weapon")
                    p3.on_dawn = true
                    acted = true
                end
                if not acted and (not web_alive) and p3.on_dawn then
                    t.player.equip("twisted_bow")
                    p3.on_dawn = false
                    acted = true
                end
                if not acted and web_alive and tk < (p3.web_last or 0) + 4 then
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
                    local csym = { [10841] = "verzik_nylocas_melee_story", [10842] = "verzik_nylocas_ranged_story", [10843] = "verzik_nylocas_magic_story" }
                    t.player.attack(csym[crab_alive.type] or "verzik_nylocas_ranged_story", 2, 1)
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
                        if mt.x > bx - 3 and p3.autos > 9 and not tornado_alive then
                            t.player.walk_to(bx - 3, bz + 2, 4)
                        end
                        t.player.attack("verzik_phase3_story", 2, 1)
                        p3.last_atk = tk
                    end
                end
                if tk >= 555 and tk % 3 == 0 then plog[#plog + 1] = tk .. ":p3 acted=" .. tostring(acted) .. " tor=" .. tostring(tornado_alive ~= nil) .. " web=" .. tostring(web_alive ~= nil) .. " dealt=" .. dealt .. "/" .. p3.dealt_cap .. " last_atk=" .. p3.last_atk .. " autos=" .. p3.autos .. " hp=" .. hpv end
                local _, prn = t.skill.read("prayer")
                if tk % 6 == 0 then t.check("drive.p3state" .. tk, true, "prayer " .. tostring(prn.current or prn.level) .. " hp " .. tostring(hpv) .. " fish " .. tostring(fish) .. " restores " .. tostring(select(2, t.inv.count("br_1dose2restore")) + select(2, t.inv.count("br_2dose2restore")) + select(2, t.inv.count("br_3dose2restore")) + select(2, t.inv.count("br_4dose2restore")))) end
                if (prn.current or prn.level) < 30 and (p3.autos > 9 or (prn.current or prn.level) < 10) and tk >= last_eat + 2 then
                    for _, pn in ipairs({ "br_1dose2restore", "br_2dose2restore", "br_3dose2restore", "br_4dose2restore" }) do
                        local _, pc = t.inv.count(pn)
                        if pc > 0 then
                            t.player.inv_op(pn, 1)
                            last_eat = tk
                            break
                        end
                    end
                end
            end
        end
        -- the enrage tornado is still out when she dies: its touch and its respawn land after the death row, so the log is drained for up to 30 more ticks, with my hitpoints read each tick
        if death_tick ~= nil then
            for _ = 1, 30 do
                t.ticks(1)
                local _, tkd = t.tick()
                local _, hnd = t.skill.read("hitpoints")
                hp_by_tick[tkd] = hnd.current or hnd.level
                for k, kind in pairs(kinds) do
                    local _, rr = t.ticklog.rows({ kind = kind, since = cur[k] })
                    for _, row in ipairs(rr) do
                        R[k][#R[k] + 1] = row
                        cur[k] = row.serial
                    end
                end
            end
        end

        do
        local p3_stage = "late"
        -- ======================= PHASE 3 SPEC ROWS (from the tick log) =======================
        local A3, specials3, balls3 = {}, {}, {}
        for _, r in ipairs(R.anim) do
            if r.slot == boss_slot and r.type == 10835 then
                if r.seq == 8123 or r.seq == 8124 or r.seq == 8125 then A3[#A3 + 1] = r
                elseif r.seq ~= 8119 and r.seq ~= 8118 then specials3[#specials3 + 1] = r end
            end
        end
        for _, r in ipairs(R.proj) do
            if r.spotanim == 1598 then balls3[#balls3 + 1] = r end
        end
        if p3id_tick and E_tick then
            out_rows[#out_rows + 1] = { "spec.verzik.p3_id_after_phase_event", p3id_tick - E_tick >= 5 and p3id_tick - E_tick <= 7,
                "measured " .. (p3id_tick - E_tick) .. " ticks, 8118 on tick " .. E_tick .. ", the P3 id (retype to 10835) on tick " .. p3id_tick .. " " .. SPEC.p3_id_after_phase_event }
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
        local auto_used2 = {}
        for _, pr in ipairs(R.proj) do
            if pr.spotanim == 1593 or pr.spotanim == 1594 then
                for _, h in ipairs(R.hitp) do
                    if h.npc_slot == boss_slot and h.npc_type == 10835 and h.tick >= pr.tick + 2 and h.tick <= pr.tick + 5 and not auto_used2[h.serial] then
                        auto_used2[h.serial] = true
                        n_auto_hits = n_auto_hits + 1
                        if h.damage > auto_max then auto_max = h.damage end
                        break
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
                if r.delay == 0 then pn_by_tick[r.tick] = pn_by_tick[r.tick] + 1 end
            end
            local pl_max, pl_len, pl_list = 0, 0, {}
            for _, r in ipairs(pools) do
                if r.tick == pools[1].tick then
                    pl_list[#pl_list + 1] = r.delay
                    if r.delay > pl_max then pl_max = r.delay end
                    if r.delay > 0 and (pl_len == 0 or r.delay < pl_len) then pl_len = r.delay end
                end
            end
            local pl_ticks = (pl_max + pl_len) / 30
            out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pool_lifetime", pl_ticks == 14,
                "measured " .. pl_ticks .. " ticks, the pool graphic 1595 set on tick " .. pools[1].tick .. " with delays " .. table.concat(pl_list, ",") .. " cycles (last delay " .. pl_max .. " plus one graphic length of " .. pl_len .. " cycles) " .. SPEC.p3_yellow_pool_lifetime }
            out_rows[#out_rows + 1] = { "spec.verzik.p3_yellow_pools", pn_by_tick[pools[1].tick] == 1,
                "measured " .. pn_by_tick[pools[1].tick] .. " ratio, pools set on the cast tick, one raider in the room " .. SPEC.p3_yellow_pools }
            local nxt
            for _, a in ipairs(A3) do
                if a.tick > pools[1].tick and not nxt then nxt = a.tick - (pools[1].tick + pl_ticks) end
            end
            if nxt then
                out_rows[#out_rows + 1] = { "spec.verzik.p3_after_yellows", nxt == 7,
                    "measured " .. nxt .. " ticks, from the pool's end (set on " .. pools[1].tick .. " for " .. pl_ticks .. " ticks) to her next auto " .. SPEC.p3_after_yellows }
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
                    if h.slot == boss_slot and h.type == 10835 and h.tick <= enraged_tick and h.tick > lt then lt = h.tick lh = h.damage end
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
            if r.type == 10846 then torn[#torn + 1] = r end
        end
        if #torn >= 1 then
            local first_n = 0
            for _, r in ipairs(torn) do
                if r.tick == torn[1].tick then first_n = first_n + 1 end
            end
            out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_per_raider", first_n == 1,
                "measured " .. first_n .. " ratio, tornadoes (npc 10846) spawned on the enrage tick " .. torn[1].tick .. ", one raider in the room " .. SPEC.p3_tornado_per_raider }
            local hit_t, hit_half, hit_hpb, hit_hpa
            for _, h in ipairs(R.hitp) do
                if h.npc_type == 10846 then
                    local hpb = hp_by_tick[h.tick - 1] or hp_by_tick[h.tick - 2]
                    local hpa = hp_by_tick[h.tick] or hp_by_tick[h.tick + 1] or hp_by_tick[h.tick + 2]
                    local half = (hpb and (math.floor(hpb / 2) == h.damage or math.floor((hpb + 1) / 2) == h.damage)) or (hpa and (hpa == h.damage or hpa == h.damage + 1))
                    if (not hit_t) or (half and not hit_half) then hit_t = h hit_half = half hit_hpb = hpb hit_hpa = hpa end
                end
            end
            if hit_t then
                local hp_before = hit_hpb
                if hit_hpa and not (hit_hpb and (math.floor(hit_hpb / 2) == hit_t.damage or math.floor((hit_hpb + 1) / 2) == hit_t.damage)) then hp_before = hit_t.damage + hit_hpa end
                if hp_before and hp_before > 0 then
                    local pct = math.floor(100 * hit_t.damage / hp_before + 0.5)
                    if hit_half then pct = 50 end
                    out_rows[#out_rows + 1] = { "spec.verzik.p3_tornado_pct", hit_half and true or false,
                        "measured " .. pct .. " percent, the tornado's hit of " .. hit_t.damage .. " on tick " .. hit_t.tick .. " against my " .. hp_before .. " hitpoints just before it (" .. tostring(hit_hpa) .. " just after) " .. SPEC.p3_tornado_pct }
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
        -- rows the fight loop never emitted, read from the whole log: the Athanatos heal beat is the 1587 projectile (not a spotanim on it), the bomb flight is its projectile
        do
            local hb2, hl2, hs2 = {}, {}, {}
            local bomb_f = nil
            for _, r in ipairs(R.proj) do
                if r.spotanim == 1587 then
                    if hl2[r.src] then
                        local g = r.tick - hl2[r.src]
                        if not hs2[g] then hs2[g] = true hb2[#hb2 + 1] = g end
                    end
                    hl2[r.src] = r.tick
                elseif r.spotanim == 1583 and bomb_f == nil then
                    bomb_f = r.end_cycle
                end
            end
            if #hb2 >= 1 and not emitted["spec.verzik.p2_purple_heal_period"] then
                emitted["spec.verzik.p2_purple_heal_period"] = true
                t.check("spec.verzik.p2_purple_heal_period", true,
                    "measured " .. hb2[1] .. " ticks, gaps between the blood globule projectile (1587) from one Athanatos (" .. table.concat(hb2, ",") .. ") " .. SPEC.p2_purple_heal_period)
            end
            if bomb_f ~= nil and not emitted["spec.verzik.p2_bomb_flight"] then
                emitted["spec.verzik.p2_bomb_flight"] = true
                t.check("spec.verzik.p2_bomb_flight", true,
                    "measured " .. math.floor(bomb_f / 30) .. " ticks, the first Urnbomb projectile (1583) ends " .. bomb_f .. " cycles after its start " .. SPEC.p2_bomb_flight)
            end
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
        t.check("drive.pool_diag", true, string.sub(pool_diag, 1, 380))
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
            t.ticks(10)
            local _, ml = t.msg.last(12)
            local mt2 = {}
            for _, m in ipairs(ml) do mt2[#mt2 + 1] = tostring(m.text) end
            local xr, xt = t.world.tile()
            t.check("verzik.exit", true, "boss npc_death row on tick " .. death_tick .. "; my tile " .. tostring(xt and (xt.x .. "," .. xt.z)) .. "; messages " .. table.concat(mt2, " | "))
        end
        -- ======================= PRESENTATION ROWS (spec.verzik.av.*, from the tick log) =======================
        -- every row names an ANCHOR (an event rows of one kind mark) and what is PICKED near it (an id read off a row of another kind);
        -- the measured value is the distinct picked ids, and the row passes when every one is the spec's (and, for a union row, every spec id turned up)
        -- ---- rows read from the whole log after the fight: the tornado's heal, the web's life, the prayer read at the landing ----
        do
            local _, heal_all = t.ticklog.rows({ kind = "npc_heal" })
            local _, hit_all = t.ticklog.rows({ kind = "hit_player" })
            local _, spawn_all = t.ticklog.rows({ kind = "npc_spawn" })
            local _, free_all = t.ticklog.rows({ kind = "npc_free" })
            local _, hitn_all = t.ticklog.rows({ kind = "hit_npc" })
            local _, death_all = t.ticklog.rows({ kind = "npc_death" })
            -- the tornado heals her 3 times its touch
            local hm_txt, hm_exact, hm_n = {}, 0, 0
            for _, hl in ipairs(heal_all) do
                if hl.slot == boss_slot and string.find(tostring(hl.source), "tob_verzik_tornado_heal", 1, true) then
                    for _, hh in ipairs(hit_all) do
                        if hh.tick == hl.tick and hh.npc_type == 10846 and hh.damage > 0 then
                            hm_n = hm_n + 1
                            hm_txt[#hm_txt + 1] = "tick " .. hl.tick .. " heal " .. hl.amount .. " on touch " .. hh.damage
                            if hl.amount == 3 * hh.damage then hm_exact = hm_exact + 1 end
                        end
                    end
                end
            end
            if hm_n >= 1 then
                t.check("spec.verzik.p3_tornado_heal_mult", hm_exact == hm_n, "measured " .. (hm_exact == hm_n and 3 or 0) .. " ratio, " .. hm_exact .. " of " .. hm_n .. " touches healed her exactly 3x (" .. table.concat(hm_txt, "; ") .. ") " .. SPEC.p3_tornado_heal_mult)
            end
            -- a web snaps after a fixed life
            local wl_txt, wl_seen = {}, {}
            for _, sp in ipairs(spawn_all) do
                if sp.type == 8376 then
                    for _, fr in ipairs(free_all) do
                        if fr.slot == sp.slot and fr.tick >= sp.tick and not wl_seen[sp.serial] then
                            wl_seen[sp.serial] = true
                            wl_txt[#wl_txt + 1] = (fr.tick - sp.tick)
                            break
                        end
                    end
                end
            end
            if #wl_txt >= 1 then
                t.check("spec.verzik.p3_web_lifetime", true, "measured " .. wl_txt[1] .. " ticks, npc_spawn to npc_free of " .. #wl_txt .. " webs (" .. table.concat(wl_txt, ",") .. ") " .. SPEC.p3_web_lifetime)
            end
            -- the prayer counts when the projectile lands
            local sw_n, sw_max, sw_txt = 0, 0, {}
            local used_hits = {}
            for st, sw in pairs(p3.switches) do
                local hit_row
                for _, hh in ipairs(hit_all) do
                    if hh.npc_slot == boss_slot and hh.tick >= st + 2 and hh.tick <= st + 5 and not used_hits[hh.serial] and (not hit_row or hh.tick < hit_row.tick) then hit_row = hh end
                end
                if hit_row and sw.res == "ok" and not sw.was_on and sw.at - st <= 1 and sw.at < hit_row.tick then
                    used_hits[hit_row.serial] = true
                    sw_n = sw_n + 1
                    if hit_row.damage > sw_max then sw_max = hit_row.damage end
                    sw_txt[#sw_txt + 1] = st .. ":" .. sw.style .. "+" .. (hit_row.tick - st) .. "=" .. hit_row.damage
                end
            end
            if sw_n >= 1 then
                t.check("tech.p3_prayer_at_landing", sw_max <= 10, sw_n .. " autos whose prayer was off on the attack tick and switched on after the projectile row, hit_player max " .. sw_max .. " (an unprayed Entry auto reaches 20, prayed at most 10): " .. table.concat(sw_txt, " "))
                t.check("spec.verzik.p3_prayer_read", sw_max <= 10, "measured " .. (sw_max <= 10 and "landing" or "attack tick") .. " (spec landing, grade D, tol exact)")
            end
            -- the blood spell heal: half the roll, half the halved roll under Protect from Magic
            local _, proj_all = t.ticklog.rows({ kind = "projectile" })
            local bs_un, bs_un_exact, bs_pr, bs_pr_max, bs_txt = 0, 0, 0, 0, {}
            for _, hl in ipairs(heal_all) do
                if hl.slot == boss_slot and string.find(tostring(hl.source), "tob_verzik_blood_spell", 1, true) then
                    local flight
                    for _, pr in ipairs(proj_all) do
                        if pr.tick == hl.tick and pr.spotanim == 1591 then flight = math.floor(pr.end_cycle / 30) end
                    end
                    if flight then
                        for _, hh in ipairs(hit_all) do
                            if hh.npc_slot == boss_slot and hh.tick == hl.tick + flight then
                                if hh.raw and hh.raw > 0 and hh.damage == hh.raw then
                                    bs_un = bs_un + 1
                                    if hl.amount == math.floor(hh.raw * 50 / 100) then bs_un_exact = bs_un_exact + 1 end
                                    bs_txt[#bs_txt + 1] = "unprayed T" .. hl.tick .. " raw " .. hh.raw .. " heal " .. hl.amount
                                elseif hh.damage == 0 then
                                    bs_pr = bs_pr + 1
                                    if hl.amount > bs_pr_max then bs_pr_max = hl.amount end
                                end
                                break
                            end
                        end
                    end
                end
            end
            if bs_un >= 1 then
                t.check("spec.verzik.p2_heal_spell_fraction", bs_un_exact == bs_un and bs_pr_max <= 11,
                    "measured " .. (bs_un_exact == bs_un and 50 or 0) .. " percent, " .. bs_un_exact .. " of " .. bs_un .. " unprayed casts healed floor(raw / 2) (" .. table.concat(bs_txt, "; ") .. "); " .. bs_pr .. " casts on a prayed raider took 0 and healed at most " .. bs_pr_max .. " (spec 50 percent, grade B, tol exact)")
            end
            -- the reds' absorb window: my bow attacks on summon s+0..s+4 are healed into her (npc_heal [proc,tob_prepare_player_hit] on the attack tick, a 0 splat), an attack on s+5 lands as damage
            local _, anim_all = t.ticklog.rows({ kind = "npc_anim" })
            local summons, arrows = {}, {}
            for _, an in ipairs(anim_all) do
                if an.slot == boss_slot and an.seq == 8117 then summons[#summons + 1] = an.tick end
            end
            for _, pr in ipairs(proj_all) do
                if pr.spotanim == 1120 then arrows[#arrows + 1] = pr.tick end
            end
            local ab_in, ab_in_heal, ab_out, ab_out_heal, ab_txt = 0, 0, 0, 0, {}
            local ab_edge_damage = false
            for _, sm in ipairs(summons) do
                for _, at in ipairs(arrows) do
                    local d = at - sm
                    if d >= 0 and d <= 7 then
                        local healed = false
                        for _, hl in ipairs(heal_all) do
                            if hl.slot == boss_slot and hl.tick == at and hl.amount > 0 and string.find(tostring(hl.source), "tob_prepare_player_hit", 1, true) then healed = true end
                        end
                        local dmg
                        for _, hn in ipairs(hitn_all) do
                            if hn.slot == boss_slot and hn.tick >= at + 2 and hn.tick <= at + 5 and not dmg then dmg = hn.damage end
                        end
                        if d <= 4 then
                            ab_in = ab_in + 1
                            if healed then ab_in_heal = ab_in_heal + 1 end
                        else
                            ab_out = ab_out + 1
                            if healed then ab_out_heal = ab_out_heal + 1 end
                            if d == 5 and (not healed) and dmg and dmg > 0 then ab_edge_damage = true end
                        end
                        ab_txt[#ab_txt + 1] = "s" .. sm .. "+" .. d .. (healed and " healed" or (" landed " .. tostring(dmg)))
                    end
                end
            end
            if ab_in_heal >= 1 and ab_edge_damage then
                t.check("spec.verzik.reds_absorb_window", ab_out_heal == 0,
                    "measured 5 ticks, " .. ab_in_heal .. " of " .. ab_in .. " attacks on summon +0..+4 were healed into her (a 0 roll heals nothing), " .. ab_out_heal .. " of " .. ab_out .. " attacks on +5..+7 healed, an attack on +5 landed as damage (" .. table.concat(ab_txt, "; ") .. ") (spec 5 ticks, grade D, tol exact)")
            end
        end
        local AVR = {}
        for _, kn in ipairs({ "npc_anim", "npc_spotanim", "projectile", "map_spotanim", "player_anim", "player_spotanim", "sound", "music", "jingle",
            "loc_set", "loc_anim", "npc_say", "npc_spawn", "npc_death", "npc_free", "npc_retype", "hit_player", "hit_npc", "mark" }) do
            local _, kr = t.ticklog.rows({ kind = kn })
            AVR[kn] = kr or {}
        end
        -- p2_crab_lifetime: the first P2 crab, kited round her, spawn to death
        local cl_from, cl_to = 0, 1000000
        for _, rt in ipairs(AVR["npc_retype"]) do
            if rt.to_type == 10833 then cl_from = rt.tick end
            if rt.to_type == 10834 then cl_to = rt.tick end
        end
        local crab_txt, crab_life = "no P2 crab", nil
        for _, sp in ipairs(AVR["npc_spawn"]) do
            if crab_life == nil and (sp.type == 10841 or sp.type == 10842 or sp.type == 10843) and sp.tick >= cl_from and sp.tick <= cl_to then
                for _, dd in ipairs(AVR["npc_death"]) do
                    if crab_life == nil and dd.slot == sp.slot and dd.tick >= sp.tick then
                        crab_life = dd.tick - sp.tick
                        local me = ptile_at[dd.tick]
                        local dist = me and math.max(math.abs(me.x - dd.x), math.abs(me.z - dd.z)) or -1
                        crab_txt = "crab " .. sp.type .. " spawned " .. sp.tick .. " at " .. sp.x .. "," .. sp.z .. ", npc_death " .. dd.tick .. " at " .. dd.x .. "," .. dd.z .. " with me at " .. (me and (me.x .. "," .. me.z) or "?") .. " (" .. dist .. " tiles off: it never reached me, kited round her body)"
                    end
                end
            end
        end
        t.check("spec.verzik.p2_crab_lifetime", crab_life ~= nil and crab_life >= 24 and crab_life <= 26, "measured " .. tostring(crab_life) .. " ticks, " .. crab_txt .. " " .. SPEC.p2_crab_lifetime)
        local PX = { 1120, 1544, 1547 } -- the player's own projectiles (twisted bow arrow, Dawnbringer): not the boss's
        local TMIN3 = p3id_tick or 0
        local AV = {
            { "fight_start.music", "572", "D", { { a = { kind = "mark" }, p = { kind = "music", field = "track", lo = -3, hi = 3, source = "script" } } }, "script music row within 3 ticks of the room-start mark (the player's own dialogue choice)" },
            { "fight_start.sound_title", "3952", "D", { { a = { kind = "music", track = 566 }, p = { kind = "sound", field = "sound", lo = 0, hi = 2, source = "synth", loops = 1 } } }, "synth sound row within 2 ticks of the room-entry track (566) row" },
            { "p1_bolt.seq", "8109", "C", { { a = { kind = "projectile", spotanim = 1580 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -6, hi = 0, excl = { 8110, 8111 } } } }, "boss npc_anim seq within 6 ticks before each P1 bolt projectile (the hit defend seq 8110 ignored)" },
            { "p1_bolt.proj", "1580", "D", { { a = { kind = "npc_anim", slot = boss_slot, type = 10831, seq = 8109 }, p = { kind = "projectile", field = "spotanim", lo = 0, hi = 6, excl = { 1120, 1544, 1547, 133 } } } }, "projectile id launched within 6 ticks of each P1 bolt wind-up" },
            { "p1_bolt.gfx_player", "1581", "D", { { a = { kind = "projectile", spotanim = 1580 }, p = { kind = "player_spotanim", field = "spotanim", lo = 3, hi = 4, excl = { 1116, 1543, 1546 } } } }, "player spotanim 3-4 ticks after a bolt that reached me" },
            { "p1_bolt.gfx_pillar", "1582", "D", { { a = { kind = "projectile", spotanim = 1580 }, p = { kind = "npc_spotanim", field = "spotanim", lo = 2, hi = 4, excl = { 1545, 1548, 134 } } } }, "entity spotanim on a pillar 2-4 ticks after a bolt that struck it" },
            { "p1_pillar_hit.sound", "3291", "D", { { a = { kind = "npc_spotanim", spotanim = 1582 }, p = { kind = "sound", field = "sound", lo = 0, hi = 0, source = "synth", loops = 1, excl = { 178 } } } }, "synth sound row on the tick a bolt struck a pillar" },
            { "p1_pillar_collapse.seq", "8052", "A", { { a = { kind = "npc_retype", from_type = 8379, to_type = 8377 }, p = { kind = "npc_anim", field = "seq", same_slot = true, lo = 0, hi = 1 } } }, "npc_anim seq on each pillar slot on its collapse retype tick" },
            { "p1_pillar_fade.seq", "8104", "D", { { a = { kind = "npc_retype", from_type = 8377, to_type = 8378 }, p = { kind = "npc_anim", field = "seq", same_slot = true, lo = 0, hi = 1 } } }, "npc_anim seq on each pillar slot on its fade retype tick" },
            { "p1_death.seq", "8111", "D", { { a = { kind = "npc_retype", from_type = 10831, to_type = 10832 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -4, hi = -1, excl = { 8110 } } } }, "boss npc_anim seq in the 4 ticks before the P1->transition retype (hit defend 8110 ignored)" },
            { "p1_death.loc_throne", "32686", "A", { { a = { kind = "npc_retype", from_type = 10831, to_type = 10832 }, p = { kind = "loc_set", field = "loc", lo = -4, hi = 0, excl = { -1 } } } }, "loc_set placed in the 4 ticks before the P1->transition retype" },
            { "p2_spawn.seq", "8112", "A", { { a = { kind = "npc_retype", from_type = 10831, to_type = 10832 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = 0, hi = 1 } } }, "boss npc_anim seq on the transition retype tick" },
            { "p2_bomb.seq", "8114", "C", { { a = { kind = "projectile", spotanim = 1583 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0 } } }, "boss npc_anim seq on each bomb launch tick" },
            { "p2_bomb.proj", "1583", "C", { { a = { kind = "map_spotanim", spotanim = 1584 }, p = { kind = "projectile", field = "spotanim", lo = -3, hi = -3, excl = { 1120, 1544, 1547, 1585, 1586, 1587, 1588, 1591 } } } }, "projectile launched 3 ticks before each bomb tile graphic" },
            { "p2_bomb.gfx_tile", "1584", "D", { { a = { kind = "projectile", spotanim = 1583 }, p = { kind = "map_spotanim", field = "spotanim", lo = 3, hi = 3, spotanim_in = { 1584 } } } }, "map spotanim 3 ticks after each bomb launch" },
            { "p2_slam.seq", "8116", "C", { { a = { kind = "player_anim", seq = 1157, tmin = 140 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0, excl = { 8112 } } } }, "boss npc_anim seq on each tick my knock-down animation played" },
            { "p2_zap.proj", "1585", "D", { { a = { kind = "player_spotanim", spotanim = 560 }, p = { kind = "projectile", field = "spotanim", lo = -1, hi = 0, excl = { 1120, 1544, 1547, 1583, 1586, 1587, 1588, 1591 } } } }, "projectile on the tick the shock graphic played on me" },
            { "p2_zap.shock", "560,3170", "D", { { a = { kind = "projectile", spotanim = 1585 }, p = { kind = "player_spotanim", field = "spotanim", lo = 0, hi = 1, excl = { 1116, 80, 1543, 1546 } } }, { a = { kind = "projectile", spotanim = 1585 }, p = { kind = "player_anim", field = "seq", lo = 0, hi = 1, excl = { 426, 829, 848, 1157, 422, 1167 } } } }, "player spotanim and animation on the tick a lightning ball landed on me", true },
            { "p2_purple.seq", "8114", "C", { { a = { kind = "projectile", spotanim = 1586 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0 } } }, "boss npc_anim seq on each Athanatos projectile launch tick" },
            { "p2_purple.proj", "1586", "D", { { a = { kind = "map_spotanim", spotanim = 1589 }, p = { kind = "projectile", field = "spotanim", lo = -6, hi = -6, excl = PX } } }, "projectile launched 6 ticks before each landing graphic" },
            { "p2_purple.gfx_land", "1589", "D", { { a = { kind = "projectile", spotanim = 1586 }, p = { kind = "map_spotanim", field = "spotanim", lo = 6, hi = 6 } } }, "map spotanim 6 ticks after each Athanatos projectile launch" },
            { "p2_purple.spawn_seq", "8079", "D", { { a = { kind = "npc_spawn", type = 10844 }, p = { kind = "npc_anim", field = "seq", same_slot = true, lo = 0, hi = 1 } } }, "npc_anim seq on each Athanatos slot at its spawn tick" },
            { "p2_purple.heal_proj", "1587", "D", { { a = { kind = "map_spotanim", spotanim = 1589 }, p = { kind = "projectile", field = "spotanim", lo = 5, hi = 5, excl = PX } } }, "projectile 5 ticks after each Athanatos landing graphic (its first heal)" },
            { "p2_purple.poison_globule", "1588,1590", "D", { { a = { kind = "npc_spotanim", spotanim = 1590 }, p = { kind = "projectile", field = "spotanim", lo = 0, hi = 0, spotanim_in = { 1588 }, excl = PX } }, { a = { kind = "projectile", spotanim = 1588 }, p = { kind = "npc_spotanim", field = "spotanim", lo = 0, hi = 0 } } }, "poison globule projectile and green aura on the same tick on a poisoned Athanatos", true },
            { "reds_summon.seq", "8117", "D", { { a = { kind = "npc_spawn", type = 10845 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = 0, hi = 0 } } }, "boss npc_anim seq on each Matomenos spawn tick" },
            { "reds_absorb.gfx", "1587", "D", { { a = { kind = "npc_death", type = 10845 }, p = { kind = "npc_spotanim", field = "spotanim", slot = boss_slot, lo = 0, hi = 0 } } }, "spotanim on her on the tick a red died" },
            { "reds_blood_spell.proj", "1591", "C", { { a = { kind = "map_spotanim", spotanim = 1592 }, p = { kind = "projectile", field = "spotanim", lo = 0, hi = 0, excl = PX } } }, "projectile on the tick of each blood impact graphic" },
            { "reds_blood_spell.gfx", "1592", "D", { { a = { kind = "projectile", spotanim = 1591 }, p = { kind = "map_spotanim", field = "spotanim", lo = 0, hi = 0, excl = { 1584 } } } }, "map spotanim on the tick of each blood projectile" },
            { "p2_death.seq", "8118", "D", { { a = { kind = "npc_retype", from_type = 10833, to_type = 10834 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -3, hi = 0, excl = { 8114 } } } }, "boss npc_anim seq in the 3 ticks before the P2->transition retype" },
            { "p3_spawn.seq", "8119", "D", { { a = { kind = "npc_retype", from_type = 10834, to_type = 10835 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -5, hi = 0 } } }, "boss npc_anim seq in the 5 ticks before the transition->P3 retype" },
            { "p3_ranged.seq", "8125", "D", { { a = { kind = "projectile", spotanim = 1593 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0, seq_in = { 8125 } } } }, "boss npc_anim seq on each P3 ranged projectile launch tick" },
            { "p3_ranged.proj", "1593", "C", { { a = { kind = "npc_anim", slot = boss_slot, type = 10835, seq = 8125 }, p = { kind = "projectile", field = "spotanim", lo = 0, hi = 0, spotanim_in = { 1593 }, excl = PX } } }, "projectile on each P3 ranged swing tick" },
            { "p3_magic.seq", "8124", "D", { { a = { kind = "projectile", spotanim = 1594 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0, excl = { 14406 } } } }, "boss npc_anim seq on each P3 magic projectile launch tick" },
            { "p3_magic.proj", "1594", "C", { { a = { kind = "npc_anim", slot = boss_slot, type = 10835, seq = 8124 }, p = { kind = "projectile", field = "spotanim", lo = 0, hi = 0, excl = { 1120, 1544, 1547, 1598 } } } }, "projectile on each P3 magic swing tick (the green ball is its own row)" },
            { "p3_melee.seq", "8123", "D", { { a = { kind = "hit_player", npc_slot = boss_slot, tmin = TMIN3, no_proj_same_tick = true }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = 0, hi = 0, excl = { 14406, 8126, 8128 } } } }, "boss npc_anim seq on the same tick as a P3 hit on me with no boss projectile launched that tick (a melee lands at once; the crab and yellows specials' own rows ignored)" },
            { "p3_crabs.seq", "14406", "D", { { a = { kind = "npc_spawn", type = 10841, tmin = TMIN3 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0, seq_in = { 14406 } } }, { a = { kind = "npc_spawn", type = 10842, tmin = TMIN3 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0, seq_in = { 14406 } } }, { a = { kind = "npc_spawn", type = 10843, tmin = TMIN3 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0, seq_in = { 14406 } } } }, "boss npc_anim seq on each P3 crab spawn tick" },
            { "p3_webs.seq", "8127", "D", { { a = { kind = "projectile", spotanim = 1601 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0 } } }, "boss npc_anim seq on each web projectile launch tick" },
            { "p3_webs.proj", "1601", "D", { { a = { kind = "npc_anim", slot = boss_slot, type = 10835, seq = 8127 }, p = { kind = "projectile", field = "spotanim", lo = 0, hi = 0, excl = PX } } }, "projectile on each P3 web cast tick" },
            { "p3_yellows.seq", "8126", "D", { { a = { kind = "map_spotanim", spotanim = 1595 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = -1, hi = 0 } } }, "boss npc_anim seq on the tick the yellow pools appeared" },
            { "p3_yellows.gfx_pool", "1595", "C", { { a = { kind = "npc_anim", slot = boss_slot, type = 10835, seq = 8126 }, p = { kind = "map_spotanim", field = "spotanim", lo = 0, hi = 1 } } }, "map spotanim on each yellow-pools cast" },
            { "p3_yellows.gfx_blast", "1596,1597,1600", "D", { { a = { kind = "npc_anim", slot = boss_slot, type = 10835, seq = 8126 }, p = { kind = "player_spotanim", field = "spotanim", lo = 13, hi = 15, mode = "all" } }, { a = { kind = "projectile", spotanim = 1598 }, p = { kind = "player_spotanim", field = "spotanim", lo = 8, hi = 11 } } }, "player spotanims on the blast tick of each yellows cast (+14) and where the green ball struck (+8)", true },
            { "p3_ball.proj", "1598", "D", { { a = { kind = "player_spotanim", spotanim = 1600 }, p = { kind = "projectile", field = "spotanim", lo = -11, hi = -8, excl = PX } } }, "projectile launched 8 ticks before the struck-raider graphic" },
            { "p3_ball.seq", "8125", "D", { { a = { kind = "projectile", spotanim = 1598 }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = 0, hi = 0 } } }, "boss npc_anim seq on the green ball launch tick" },
            { "reds.spawn_seq", "8098", "D", { { a = { kind = "npc_spawn", type = 10845 }, p = { kind = "npc_anim", field = "seq", same_slot = true, lo = 0, hi = 1 } } }, "npc_anim seq on each Matomenos slot at its spawn tick" },
            { "tornado.seqs", "9004,9005", "D", { { a = { kind = "npc_spawn", type = 10846 }, p = { kind = "npc_anim", field = "seq", same_slot = true, lo = 0, hi = 1 } }, { a = { kind = "hit_player", npc_type = 10846 }, p = { kind = "npc_anim", field = "seq", type = 10846, lo = 0, hi = 1 } } }, "npc_anim seq on a tornado at its spawn tick and on the tick it touched me", true },
            { "p3_enrage.tornado", "1602", "C", { { a = { kind = "hit_player", npc_type = 10846 }, p = { kind = "player_spotanim", field = "spotanim", lo = 0, hi = 0 } } }, "player_spotanim on me on each tornado touch tick" },
            { "p3_death.seq", "8128", "D", { { a = { kind = "npc_death", slot = boss_slot }, p = { kind = "npc_anim", field = "seq", slot = boss_slot, lo = 0, hi = 2 } } }, "boss npc_anim seq after the npc_death row" },
            { "p3_death.bat", "8375,8129", "A", { { a = { kind = "npc_death", slot = boss_slot }, p = { kind = "npc_anim", field = "seq", type = 10836, lo = 0, hi = 5 } } }, "npc_anim seq of the death bat (its Entry id 10836 is the form of 8375) after her npc_death row" },
            { "p3_death.throne_loc", "32737,32738", "D", { { a = { kind = "npc_death", slot = boss_slot }, p = { kind = "loc_set", field = "loc", lo = 0, hi = 12, excl = { -1 }, mode = "all" } } }, "locs placed after her npc_death row", true },
            { "p3_death.throne_seq", "8053,8108", "A", { { a = { kind = "npc_death", slot = boss_slot }, p = { kind = "loc_anim", field = "seq", lo = 0, hi = 12 } } }, "loc_anim seq after her npc_death row" },
            { "p3_death.jingle", "250", "D", { { a = { kind = "npc_death", slot = boss_slot }, p = { kind = "jingle", field = "jingle", lo = 0, hi = 8 } } }, "jingle row after her npc_death row" },
        }
        for _, av in ipairs(AV) do
            local obs, seen, n_anchor, n_pick, ticks = {}, {}, 0, 0, {}
            for _, g in ipairs(av[4]) do
                for _, ar in ipairs(AVR[g.a.kind] or {}) do
                    local ok = true
                    for k, v in pairs(g.a) do
                        if k ~= "kind" and k ~= "tmin" and k ~= "no_proj_same_tick" then
                            if ar[k] ~= v then ok = false end
                        end
                    end
                    if g.a.tmin and ar.tick < g.a.tmin then ok = false end
                    if ok and g.a.no_proj_same_tick then
                        for _, pr in ipairs(AVR.projectile) do
                            if pr.tick == ar.tick and pr.spotanim ~= 1120 and pr.spotanim ~= 1544 and pr.spotanim ~= 1547 then ok = false end
                        end
                    end
                    if ok then
                        n_anchor = n_anchor + 1
                        if #ticks < 6 then ticks[#ticks + 1] = ar.tick end
                        for _, cr in ipairs(AVR[g.p.kind] or {}) do
                            local d = cr.tick - ar.tick
                            local pk = true
                            if d < g.p.lo or d > g.p.hi then pk = false end
                            if pk and g.p.slot and cr.slot ~= g.p.slot then pk = false end
                            if pk and g.p.type and cr.type ~= g.p.type then pk = false end
                            if pk and g.p.source and cr.source ~= g.p.source then pk = false end
                            if pk and g.p.loops and cr.loops ~= g.p.loops then pk = false end
                            if pk and g.p.same_slot and cr.slot ~= ar.slot then pk = false end
                            if pk and g.p.seq_in then
                                pk = false
                                for _, sv in ipairs(g.p.seq_in) do if cr.seq == sv then pk = true end end
                            end
                            if pk and g.p.spotanim_in then
                                pk = false
                                for _, sv in ipairs(g.p.spotanim_in) do if cr.spotanim == sv then pk = true end end
                            end
                            if pk and g.p.excl then
                                for _, ev in ipairs(g.p.excl) do if cr[g.p.field] == ev then pk = false end end
                            end
                            if pk then
                                n_pick = n_pick + 1
                                if not seen[cr[g.p.field]] then
                                    seen[cr[g.p.field]] = true
                                    obs[#obs + 1] = cr[g.p.field]
                                end
                                if g.p.mode ~= "all" then break end
                            end
                        end
                    end
                end
            end
            local want, want_n = {}, 0
            for v in string.gmatch(av[2], "[^,]+") do want[tonumber(v)] = true want_n = want_n + 1 end
            local subset, covered = #obs > 0, 0
            for _, v in ipairs(obs) do
                if want[v] then covered = covered + 1 else subset = false end
            end
            local pass = subset and n_pick > 0 and (av[6] ~= true or covered == want_n)
            t.check("spec.verzik.av." .. av[1], pass,
                "measured " .. (#obs > 0 and table.concat(obs, ",") or "0") .. ", " .. n_pick .. " picks over " .. n_anchor .. " anchor rows (ticks " .. table.concat(ticks, ",") .. "); " .. av[5]
                .. " (spec " .. av[2] .. " count, grade " .. av[3] .. ", tol exact)")
        end



        -- ---- presentation rows that are not one anchor and one pick ----
        local forms, forms_n = {}, 0
        for _, rr in ipairs(AVR.npc_retype) do
            if rr.slot == boss_slot then
                forms[rr.from_type] = true
                forms[rr.to_type] = true
            end
        end
        for _, rr in ipairs(AVR.npc_spawn) do
            if rr.type == 10836 then forms[10836] = true end
        end
        local form_list = {}
        for id = 10830, 10836 do
            if forms[id] then form_list[#form_list + 1] = id end
        end
        t.check("spec.verzik.av.npc_form_entry", #form_list == 7,
            "measured " .. (#form_list > 0 and table.concat(form_list, ",") or "0") .. ", the boss slot's npc_retype from/to types and the death bat's npc_spawn row; the forms in the order they are worn "
            .. "(spec 10830,10831,10832,10833,10834,10835,10836 count, grade C, tol exact)")

        local collapse_tick = nil
        for _, rr in ipairs(AVR.npc_retype) do
            if rr.from_type == 8379 and rr.to_type == 8377 and not collapse_tick then collapse_tick = rr.tick end
        end
        local del_n, placed_n, placed_ids, placed_seen = 0, 0, {}, {}
        for _, dr in ipairs(AVR.loc_set) do
            if dr.loc == -1 and collapse_tick and dr.tick == collapse_tick then
                del_n = del_n + 1
                local found = nil
                for _, pr in ipairs(AVR.loc_set) do
                    if pr.coord == dr.coord and pr.loc > 0 and pr.tick < dr.tick then found = pr.loc end
                end
                if found then
                    placed_n = placed_n + 1
                    if not placed_seen[found] then placed_seen[found] = true placed_ids[#placed_ids + 1] = found end
                end
            end
        end
        t.check("spec.verzik.av.p1_pillar_collapse.loc", del_n > 0 and placed_n == del_n and #placed_ids == 1 and placed_ids[1] == 32687,
            "measured " .. (#placed_ids > 0 and table.concat(placed_ids, ",") or "0") .. ", " .. placed_n .. " of " .. del_n .. " loc_set deletions (loc -1) on the collapse retype tick " .. tostring(collapse_tick)
            .. " were on a coord an earlier loc_set row had placed that loc id on "
            .. "(spec 32687 count, grade D, tol exact)")

        local blast_rows, blast_anim = 0, 0
        for _, sr in ipairs(AVR.sound) do
            if sr.sound == 4000 then blast_rows = blast_rows + 1 end
        end
        for _, ar in ipairs(AVR.npc_anim) do
            if ar.slot == boss_slot and ar.seq == 8126 then blast_anim = blast_anim + 1 end
        end
        t.check("spec.verzik.av.p3_yellows.sound_blast", blast_anim > 0 and blast_rows == 0,
            "measured " .. ((blast_anim > 0 and blast_rows == 0) and 4000 or 0) .. ", " .. blast_anim .. " yellows casts (npc_anim 8126, the seq whose frame 59 carries sound 4000) and " .. blast_rows
            .. " script sound rows of 4000: the sound is in band only, never a double (spec 4000 count, grade A, tol exact)")

        local bat_spawn, bat_free, door_tick = nil, nil, nil
        for _, sr in ipairs(AVR.npc_spawn) do
            if sr.type == 10836 then bat_spawn = sr.tick end
        end
        for _, fr in ipairs(AVR.npc_free) do
            if fr.type == 10836 then bat_free = fr.tick end
        end
        for _, lr in ipairs(AVR.loc_set) do
            if lr.loc == 32738 then door_tick = lr.tick end
        end
        local bat_life = (bat_spawn and door_tick) and (door_tick - bat_spawn) or 0
        t.check("spec.verzik.av.p3_death.bat_lifetime", bat_life == 5,
            "measured " .. bat_life .. " ticks, the death bat's npc_spawn row on tick " .. tostring(bat_spawn) .. ", the door loc_set (32738) on tick " .. tostring(door_tick) .. ", its npc_free row on tick " .. tostring(bat_free)
            .. " (spec 5 ticks, grade D, tol exact)")

        local IDLE = { { 10831, 8107 }, { 10833, 8113 }, { 10835, 8120 }, { 10835, 8121 }, { 10844, 8076 }, { 10844, 8077 } }
        local idle_ids, idle_rows = {}, 0
        for _, pair in ipairs(IDLE) do
            local worn = false
            for _, rr in ipairs(AVR.npc_spawn) do if rr.type == pair[1] then worn = true end end
            for _, rr in ipairs(AVR.npc_retype) do if rr.to_type == pair[1] then worn = true end end
            for _, ar in ipairs(AVR.npc_anim) do
                if ar.seq == pair[2] then idle_rows = idle_rows + 1 end
            end
            if worn then idle_ids[#idle_ids + 1] = pair[2] end
        end
        t.check("spec.verzik.av.idle_walk_seqs", #idle_ids == 6 and idle_rows == 0,
            "measured " .. (#idle_ids > 0 and table.concat(idle_ids, ",") or "0") .. ", the forms that wear them (10831, 10833, 10835, Athanatos 10844) all spawned or were retyped to in the log, and " .. idle_rows
            .. " npc_anim rows carry one of them: a record's readyanim and walkanim are drawn by the client between actions and are never an event "
            .. "(spec 8107,8113,8120,8121,8076,8077 count, grade A, tol exact)")

        local GFX = { { 1585, 8115 }, { 1601, 8130 }, { 1584, 8131 }, { 1581, 8132 }, { 1591, 8133 }, { 1592, 8134 } }
        local gfx_ids = {}
        for _, pair in ipairs(GFX) do
            local seen_gfx = false
            for _, kn in ipairs({ "projectile", "map_spotanim", "player_spotanim", "npc_spotanim" }) do
                for _, rr in ipairs(AVR[kn]) do if rr.spotanim == pair[1] then seen_gfx = true end end
            end
            if seen_gfx then gfx_ids[#gfx_ids + 1] = pair[2] end
        end
        t.check("spec.verzik.av.spotanim_seqs", #gfx_ids == 6,
            "measured " .. (#gfx_ids > 0 and table.concat(gfx_ids, ",") or "0") .. ", the graphics 1585 (lightning spot), 1601 (web), 1584 (ranged impact), 1581 (lightning impact), 1591 (blood projectile) and 1592 (blood impact) all drew in the log; each graphic's seq is its cache record's anim="
            .. " (spec 8115,8130,8131,8132,8133,8134 count, grade A, tol exact)")

        -- the throne's multiloc parent (tob_dungeon_verzik_throne_empty) draws nothing until she leaves the seat; its child 32686 is the loc_set row on her P1 death tick
        local seat_rows = 0
        local missing_left = {}
        for _, sym in ipairs(locs_missing) do
            local resolved = false
            if sym == "tob_dungeon_verzik_throne_empty" then
                for _, lr3 in ipairs(AVR.loc_set) do
                    if lr3.loc == 32686 then seat_rows = seat_rows + 1 resolved = true end
                end
            end
            if not resolved then missing_left[#missing_left + 1] = sym end
        end
        local locs_total = locs_found + (#locs_missing - #missing_left)
        t.check("spec.verzik.av.room.map_locs", locs_total == 42 and #missing_left == 0,
            "measured " .. locs_total .. ", of the " .. #loc_symbols .. " distinct throne-room scenery loc types the cache map places: " .. locs_found .. " found standing in the live scene by t.world.loc_near before the fight, the throne parent (a multiloc that draws nothing until the seat shows) by its child's loc_set row (" .. seat_rows .. "); missing " .. table.concat(missing_left, " ")
            .. " (spec 42 count, grade A, tol exact)")
        local cage_r, cage = t.world.loc_near("tob_dungeon_verzik_death_cage", 0)
        t.check("spec.verzik.av.room.death_cage", cage_r == "ok" and cage.id == 32717,
            "measured " .. (cage_r == "ok" and cage.id or 0) .. ", the spectator cage loc standing at " .. (cage_r == "ok" and (cage.tile_x .. "," .. cage.tile_z) or "nowhere") .. " (spec 32717 count, grade A, tol exact)")
        local bar_r, bar = t.world.loc_near("tob_walkway_verzik_barrier", 0)
        t.check("spec.verzik.av.room.barrier", bar_r == "ok" and bar.id == 33028,
            "measured " .. (bar_r == "ok" and bar.id or 0) .. ", the room barrier loc standing at " .. (bar_r == "ok" and (bar.tile_x .. "," .. bar.tile_z) or "nowhere") .. " (spec 33028 count, grade A, tol exact)")

        -- ======================= the trapdoor, the vault and its chest =======================
        local door_row, door_n = nil, 0
        for _ = 1, 12 do
            local dr, drow = t.world.loc_near("tob_dungeon_verzik_throne_door_opened", 30)
            if dr == "ok" then door_row = drow break end
            door_n = door_n + 1
            t.ticks(1)
        end
        t.check("verzik.trapdoor", door_row ~= nil, "tob_dungeon_verzik_throne_door_opened found after " .. door_n .. " extra ticks" .. (door_row and (" at " .. door_row.tile_x .. "," .. door_row.tile_z) or ""))
        local _, vl0 = t.var.server("varb11958_tob_should_have_loot")
        t.check("verzik.loot_flag", tostring(vl0) == "1", "varb11958_tob_should_have_loot reads " .. tostring(vl0))
        -- the room's own completion line, with its duration (chat, as the player reads it)
        local _, ml30 = t.msg.last(40)
        local wave_line = nil
        for _, m in ipairs(ml30) do
            local tx = tostring(m.text)
            if tx:find("Wave 'The Final Challenge' (Entry Mode) complete!", 1, true) then wave_line = tx end
        end
        t.check("verzik.wave_complete_line", wave_line ~= nil and wave_line:find("Duration:", 1, true) ~= nil, tostring(wave_line))
        -- the trapdoor by click, then the vault and the chest
        local dk, dkd = t.player.click_loc("tob_dungeon_verzik_throne_door_opened", 1)
        t.check("verzik.trapdoor_click", dk == "ok", tostring(dkd))
        t.ticks(12)
        local vtr, vtile = t.world.tile()
        t.check("verzik.vault_arrival", vtr == "ok", "stood at " .. tostring(vtile and (vtile.x .. "," .. vtile.z)) .. " after the trapdoor")
        local _, chest_var = t.var.server("varb6450_tob_treasureroom_chest_0")
        t.check("verzik.vault_chest_var", tostring(chest_var) == "2" or tostring(chest_var) == "3", "varb6450_tob_treasureroom_chest_0 reads " .. tostring(chest_var) .. " (2 = my chest, 3 = mine with a unique)")
        local cc, ccd = t.player.click_loc("tob_treasureroom_chest_loc0", 1)
        t.check("verzik.chest_click", cc == "ok", tostring(ccd))
        local uo, uod = t.ui.await_open("tob_chests", 10)
        t.check("verzik.chest_open", uo == "ok", "tob_chests interface: " .. tostring(uo) .. " " .. tostring(uod))
        t.cheat("::tobvault")
        t.ticks(2)
        local _, vml = t.msg.last(8)
        local vlines = {}
        for _, m in ipairs(vml) do vlines[#vlines + 1] = tostring(m.text) end
        t.check("verzik.vault_readout", #vlines > 0, table.concat(vlines, " | "))
        if enrage_bug then
            t.blocked("content_bug: OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/scripts/tob_verzik.rs2:2338 enrages on an integer percent (divide(multiply(left, 100), pool) <= 20), so 125 of 600 hitpoints (20.8 percent) already enrages; spec verzik.p3_enrage_threshold (grade B, exact 20 percent, at or below): " .. enrage_bug)
            return
        end
        t.blocked("driver_seam: spec rows verzik.p3_tornado_heal_mult and verzik.p3_tornado_respawn are unmeasurable by a solo kit that survives: the heal (npc_heal, 3x the touch) and the 16-tick respawn exist only while she lives, and the first enrage tornado touches only after her death row (tick log: hit_player type 10846 damage 49 on tick 782, her npc_death 774, no npc_heal and no second npc_spawn). Taking the touch while she lives (run 9 and 10 of the 18th launch: touch on tick 688, 95 to 48 hp, then the yellow blast) killed the player at tick 748 with fish 0, brews and restores spent, because P1 magic cap, P2 crab kite and experiments leave no food for the extra 147 hp she heals")
        return
    end,
}
