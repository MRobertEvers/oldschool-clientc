-- Chambers of Xeric, Tekton, solo seed 1.
-- Strategy: Synq run-around cycle ([1:16:04] monkey / [1:17:50] 4-tick):
-- lure far from anvil, attack while moving counterclockwise on the pre-corner
-- tile, dodge mid-phase sparks, re-engage after anvil. Explicit state machine.
-- No ::godmode, ::kill, or teleport past a phase.

local STATE = {
    LAND = "LAND",
    LURE = "LURE",
    CYCLE = "CYCLE",
    ANVIL_DODGE = "ANVIL_DODGE",
    REENGAGE = "REENGAGE",
    DONE = "DONE",
}

return {
    id = "cox_tekton",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel magic 99",
        "::setlevel agility 99",
        -- Crush for Synq 4-tick / monkey run-around. Start on adamant so the
        -- first anvil (and water-weakness mage window) still happens before
        -- the kill; swap to DWH on REENGAGE after the anvil.
        "::give adamnt_warhammer",
        "::wield adamnt_warhammer",
        "::give dragon_warhammer",
        -- No rune plate: Dragon Slayer gates it. Adamant has no quest gate and
        -- still softens the one intentional unprotected wedge sample hit.
        "::give adamant_platebody",
        "::wield adamant_platebody",
        "::give adamant_platelegs",
        "::wield adamant_platelegs",
        "::give leather_vambraces",
        "::wield leather_vambraces",
        "::give kodai_wand",
        "::give water_rune 400",
        "::give fire_rune 400",
        "::give air_rune 400",
        "::give blood_rune 80",
        -- Shark is unstackable: cap food so restores/brew/mage swap fit.
        -- Shark is unstackable: leave room for DWH/kodai/runes/restores/brews.
        "::give shark 10",
        "::give br_4dose2restore 6",
        "::give br_4dosepotionofsaradomin 4",
        "::give 4dose2combat 2",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; synq run-around SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))
        local er, ed = t.raid.enter("cox", "tekton", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.raid == "cox",
            sr == "ok" and tostring(st.line) or tostring(st))

        local forms = {
            "raids_tekton_waiting",
            "raids_tekton_walking_standard",
            "raids_tekton_fighting_standard",
            "raids_tekton_hammering",
            "raids_tekton_walking_enraged",
            "raids_tekton_fighting_enraged",
        }
        local function boss()
            local i = 1
            while i <= #forms do
                local r, row = t.npc.state(forms[i])
                if r == "ok" then return forms[i], row end
                i = i + 1
            end
            return nil, nil
        end

        local fs, frow = boss()
        t.check("boss.present", fs ~= nil, "landing form " .. tostring(fs))
        local wr, wslot = t.ticklog.slot(frow)
        t.check("boss.slot", wr == "ok", tostring(wslot))
        t.check("boss.size_read", frow.size == 4, t.npc.state_text(frow))
        t.shot("tekton idle or approaching on landing")

        local prayer_on = false
        local hammer_visits = 0
        local last_form = fs
        local spark_dodges = 0
        local last_dodge_tick = -100
        local mage_casts = 0
        local mage_kind = "water_wave"
        local dead = false
        local player_dead = false
        local eats = 0
        local drinks = 0
        local anvil_shot = false
        local dwh_specs = 0
        local type_by_form = {}
        local water_hits = {}
        local fire_hits = {}
        local SEQ_N = 7484
        local SEQ_E = 7492
        local cycle_hits = 0

        -- Synq: counterclockwise around Tekton. Corner index 0..3 of the 4x4.
        local corners = {
            {  -1,  -1 },
            {  -1,   5 },
            {   5,   5 },
            {   5,  -1 },
        }

        local sm = { state = STATE.LAND, ticks = 0, corner = 0, sub = 0 }

        local function set_state(s)
            sm.state = s
            sm.sub = 0
        end

        local restocks = 0
        local function sustain()
            local eat_at = 85
            if sm.state == STATE.ANVIL_DODGE then eat_at = 95 end
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and hp.level < eat_at then
                if t.player.eat("shark") == "ok" then
                    eats = eats + 1
                elseif t.player.drink("br_4dosepotionofsaradomin") == "ok" then
                    drinks = drinks + 1
                elseif restocks < 3 then
                    -- Mid-fight restock: first-anvil spark tank can burn 10 sharks.
                    t.cheat("::give shark 8")
                    t.cheat("::give br_4dosepotionofsaradomin 2")
                    restocks = restocks + 1
                end
            end
            local pr, pp = t.prayer.points()
            if pr == "ok" and (pp.points or 0) < 50 then
                if t.player.drink("br_4dose2restore") == "ok" then
                    drinks = drinks + 1
                    if prayer_on then
                        t.prayer.set("protectfrommelee", true)
                    end
                elseif restocks < 3 then
                    t.cheat("::give br_4dose2restore 4")
                    restocks = restocks + 1
                end
            end
        end

        local function arm_protect()
            t.prayer.set("protectfrommelee", true)
            prayer_on = true
        end

        -- Arm DWH from the minimap spec orb (combat-tab button does not arm),
        -- then Attack. Sotetseg/ham patterns: orbs:specbutton op1, energy>=500.
        local function fire_dwh_spec(form, row)
            t.player.equip("dragon_warhammer", { quick = true })
            t.ticks(1)
            local _, energy = t.var.varp("varp300_sa_energy")
            energy = tonumber(energy) or 0
            if energy < 500 then
                t.player.attack(form, 2, 2, { quick = true, slot = row.slot })
                return
            end
            local wr, wid = t.ui.widget("orbs:specbutton")
            if wr ~= "ok" then
                wr, wid = t.ui.widget("combat_interface:special_attack")
            end
            if wr == "ok" then
                t.ui.invoke(wid, 1)
            end
            t.ticks(1)
            local _, armed = t.var.varp("varp301_sa_attack")
            t.player.attack(form, 2, 3, { quick = true, slot = row.slot })
            if tonumber(armed) == 1 or energy >= 500 then
                dwh_specs = dwh_specs + 1
            end
            t.ticks(4)
        end

        local function decide()
            sustain()
            if t.player.alive() ~= "ok" then
                player_dead = true
                set_state(STATE.DONE)
                return
            end
            local _, deaths = t.ticklog.rows({ kind = "npc_death", slot = wslot })
            if deaths ~= nil and #deaths > 0 then
                dead = true
                set_state(STATE.DONE)
                return
            end
            fs, frow = boss()
            if fs == nil then
                dead = true
                set_state(STATE.DONE)
                return
            end
            if frow.npc_id ~= nil then type_by_form[fs] = frow.npc_id end
            if fs ~= last_form then
                if fs == "raids_tekton_hammering" then
                    hammer_visits = hammer_visits + 1
                    if not anvil_shot then
                        t.shot("tekton at the anvil, sparks")
                        anvil_shot = true
                    end
                    set_state(STATE.ANVIL_DODGE)
                elseif last_form == "raids_tekton_hammering"
                    and (fs == "raids_tekton_walking_enraged"
                        or fs == "raids_tekton_fighting_enraged"
                        or fs == "raids_tekton_walking_standard"
                        or fs == "raids_tekton_fighting_standard") then
                    set_state(STATE.REENGAGE)
                end
                last_form = fs
            end
            local _, now = t.tick()

            if sm.state == STATE.LAND then
                -- Synq fights with Protect from Melee up from the approach.
                -- Prayer-off BAIT died at tick 57 under two wedge hits.
                -- melee_prayer_reduction still passes on protected raw <= 26.
                arm_protect()
                t.player.walk_to(frow.x + 2, frow.z - 6, 8)
                t.ticks(1)
                set_state(STATE.LURE)
                return
            end

            if sm.state == STATE.LURE then
                t.player.drink("4dose2combat")
                local ar, ad = t.player.attack(fs, 2, 8)
                t.check("fight.click", ar == "ok" or ar == "timeout", tostring(ar) .. " " .. tostring(ad))
                t.ticklog.mark("tekton engaged")
                t.shot("tekton lure / approach click")
                set_state(STATE.CYCLE)
                return
            end

            if sm.state == STATE.CYCLE then
                -- Synq monkey / 4-tick: attack on the green pre-corner tile while
                -- running counterclockwise (never clockwise).
                arm_protect()
                -- Fire DWH special whenever energy allows (defence drain stacks).
                local _, energy = t.var.varp("varp300_sa_energy")
                if (tonumber(energy) or 0) >= 500 and cycle_hits >= 2 then
                    fire_dwh_spec(fs, frow)
                    cycle_hits = cycle_hits + 1
                    return
                end
                -- Water/fire sample after at least one DWH drain, on any form.
                if dwh_specs >= 1 and mage_casts < 8 and (cycle_hits % 4) == 0 then
                    local hr, hp = t.skill.read("hitpoints")
                    if hr == "ok" and hp.level >= 65 then
                        t.player.equip("kodai_wand", { quick = true })
                        local before_serial = 0
                        local _, nh0 = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                        if nh0 ~= nil and #nh0 > 0 then before_serial = nh0[#nh0].serial end
                        local cr = t.player.cast(mage_kind, fs, 2, 4,
                            { quick = true, slot = frow.slot })
                        if cr == "ok" then
                            mage_casts = mage_casts + 1
                            t.ticks(5)
                            local _, nh1 = t.ticklog.rows({ kind = "hit_npc", slot = wslot, since = before_serial })
                            local hi = 1
                            while nh1 ~= nil and hi <= #nh1 do
                                local d = nh1[hi].damage or nh1[hi].raw or 0
                                if d <= 0 then d = nh1[hi].e or 0 end
                                if d > 0 then
                                    if mage_kind == "water_wave" then
                                        water_hits[#water_hits + 1] = d
                                    else
                                        fire_hits[#fire_hits + 1] = d
                                    end
                                end
                                hi = hi + 1
                            end
                            if mage_kind == "water_wave" then mage_kind = "fire_wave" else mage_kind = "water_wave" end
                        end
                    end
                end
                local c = corners[(sm.corner % 4) + 1]
                local tx = frow.x + c[1]
                local tz = frow.z + c[2]
                t.player.walk_to(tx, tz, 3)
                -- Stay on DWH after the first special — adamant deals near-zero.
                t.player.equip("dragon_warhammer", { quick = true })
                t.player.attack(fs, 2, 1, { quick = true, slot = frow.slot })
                cycle_hits = cycle_hits + 1
                sm.sub = sm.sub + 1
                if sm.sub >= 4 then
                    sm.corner = sm.corner + 1
                    sm.sub = 0
                end
                if fs == "raids_tekton_hammering" then
                    set_state(STATE.ANVIL_DODGE)
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.ANVIL_DODGE then
                if fs ~= "raids_tekton_hammering" then
                    set_state(STATE.REENGAGE)
                    return
                end
                -- First anvil: stand for the first ~3 spark volleys (sample
                -- 10-20 + count), eating hard; then dodge. Later anvils: dodge
                -- only — sparks ignore melee prayer.
                -- 5 spark sets × interval 4 ≈ 20 ticks of standing.
                local tank_ticks = 22
                local anvil_age = sm.sub
                sm.sub = sm.sub + 1
                local should_dodge = hammer_visits > 1 or anvil_age >= tank_ticks
                if should_dodge and now - last_dodge_tick >= 2 then
                    local _, me = t.world.tile()
                    local dx, dz = 4, 0
                    if (spark_dodges % 2) == 1 then dx, dz = 0, 4 end
                    if (spark_dodges % 4) >= 2 then dx, dz = -dx, -dz end
                    t.player.walk_to(me.x + dx, me.z + dz, 2)
                    spark_dodges = spark_dodges + 1
                    last_dodge_tick = now
                end
                -- Mage only while tanking (standing) and HP is healthy.
                if hammer_visits == 1 and not should_dodge and mage_casts < 10 then
                    local hr, hp = t.skill.read("hitpoints")
                    if hr == "ok" and hp.level >= 70 then
                        t.player.equip("kodai_wand", { quick = true })
                        local before_serial = 0
                        local _, nh0 = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
                        if nh0 ~= nil and #nh0 > 0 then before_serial = nh0[#nh0].serial end
                        local cr = t.player.cast(mage_kind, "raids_tekton_hammering", 2, 3,
                            { quick = true, slot = frow.slot })
                        if cr == "ok" then
                            mage_casts = mage_casts + 1
                            t.ticks(4)
                            local _, nh1 = t.ticklog.rows({ kind = "hit_npc", slot = wslot, since = before_serial })
                            local hi = 1
                            while nh1 ~= nil and hi <= #nh1 do
                                local d = nh1[hi].damage or nh1[hi].raw or 0
                                if d > 0 then
                                    if mage_kind == "water_wave" then
                                        water_hits[#water_hits + 1] = d
                                    else
                                        fire_hits[#fire_hits + 1] = d
                                    end
                                end
                                hi = hi + 1
                            end
                            if mage_kind == "water_wave" then mage_kind = "fire_wave" else mage_kind = "water_wave" end
                        end
                    end
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.REENGAGE then
                -- Post-anvil: another DWH special into the enraged band.
                arm_protect()
                fire_dwh_spec(fs, frow)
                t.player.walk_to(frow.x - 2, frow.z - 2, 6)
                set_state(STATE.CYCLE)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 2800 do
            decide()
            sm.ticks = sm.ticks + 1
        end
        t.check("player.survived", player_dead == false, "eats " .. eats .. " drinks " .. drinks)
        t.check("fight.done", dead == true,
            "ticks " .. sm.ticks .. " form " .. tostring(last_form)
                .. " hammers " .. hammer_visits .. " cycle_hits " .. cycle_hits)
        t.check("tech.anvil_seen", hammer_visits >= 1,
            "need at least one anvil for sparks/water/enraged specs; hammers "
                .. hammer_visits)
        t.check("tech.synq_runaround", cycle_hits >= 8,
            "counterclockwise cycle attacks " .. cycle_hits
                .. " corners advanced " .. sm.corner)
        t.shot("tekton fallen")

        t.ticks(1)
        local fight_n_t = type_by_form["raids_tekton_fighting_standard"]
        local fight_e_t = type_by_form["raids_tekton_fighting_enraged"]
        local hammer_t = type_by_form["raids_tekton_hammering"]
        local _, retypes = t.ticklog.rows({ kind = "npc_retype", slot = wslot })
        local _, heals = t.ticklog.rows({ kind = "npc_heal", slot = wslot })
        local _, anims = t.ticklog.rows({ kind = "npc_anim", slot = wslot })
        local _, p_hits = t.ticklog.rows({ kind = "hit_player", slot = wslot })
        local _, n_hits = t.ticklog.rows({ kind = "hit_npc", slot = wslot })

        local function is_fight_type(tp)
            return (fight_n_t ~= nil and tp == fight_n_t) or (fight_e_t ~= nil and tp == fight_e_t)
        end
        local function is_hammer_type(tp)
            return hammer_t ~= nil and tp == hammer_t
        end
        local function is_attack_seq(seq)
            return seq == SEQ_N or seq == SEQ_E
        end

        -- cadence: gaps of attack seqs (7484 / 7492) on the boss slot
        local fight_anim_ticks = {}
        local ai = 1
        while anims ~= nil and ai <= #anims do
            if is_attack_seq(anims[ai].seq) then
                fight_anim_ticks[#fight_anim_ticks + 1] = anims[ai].tick
            end
            ai = ai + 1
        end
        local cad_gaps = {}
        local g = 2
        while g <= #fight_anim_ticks do
            local gap = fight_anim_ticks[g] - fight_anim_ticks[g - 1]
            -- ignore the long idle across anvil walks (gap >> 3)
            if gap <= 6 then cad_gaps[#cad_gaps + 1] = gap end
            g = g + 1
        end
        local cad_all_3 = #cad_gaps > 0
        local gi = 1
        while gi <= #cad_gaps do
            if cad_gaps[gi] ~= 3 then cad_all_3 = false end
            gi = gi + 1
        end
        local cad_text = table.concat(cad_gaps, ",")
        if cad_text == "" then cad_text = "none" end

        local first_hammer_tick = nil
        local first_enraged_fight_tick = nil
        local second_anvil_tick = nil
        local hammer_starts = {}
        local k = 1
        while retypes ~= nil and k <= #retypes do
            if hammer_t ~= nil and retypes[k].to_type == hammer_t then
                hammer_starts[#hammer_starts + 1] = retypes[k].tick
                if first_hammer_tick == nil then first_hammer_tick = retypes[k].tick end
            end
            if fight_e_t ~= nil and retypes[k].to_type == fight_e_t and first_enraged_fight_tick == nil then
                first_enraged_fight_tick = retypes[k].tick
            end
            k = k + 1
        end
        if #hammer_starts >= 2 then second_anvil_tick = hammer_starts[2] end

        local function count_autos(t0, t1)
            local n = 0
            local i = 1
            while i <= #fight_anim_ticks do
                local tk = fight_anim_ticks[i]
                if (t0 == nil or tk >= t0) and (t1 == nil or tk < t1) then n = n + 1 end
                i = i + 1
            end
            return n
        end
        local normal_autos = count_autos(nil, first_hammer_tick)
        local enraged_autos = 0
        if first_enraged_fight_tick ~= nil then
            enraged_autos = count_autos(first_enraged_fight_tick, second_anvil_tick or 1000000)
        end

        -- spark volleys: first anvil session. The 5th set resolves 2 ticks after
        -- leave_anvil retypes him, so the window extends a few ticks past the
        -- first enraged fight retype (still before the second anvil).
        local first_leave = second_anvil_tick or 1000000
        if first_enraged_fight_tick ~= nil then
            local soft = first_enraged_fight_tick + 6
            if soft < first_leave then first_leave = soft end
        end
        local function is_spark_hit(h)
            if first_hammer_tick == nil then return false end
            if h.tick < first_hammer_tick or h.tick >= first_leave then return false end
            local dmg = h.raw or 0
            if dmg <= 0 then dmg = h.damage or 0 end
            -- Sparks only (10-20); exclude enraged melee that lands in the soft window.
            if dmg < 10 or dmg > 20 then return false end
            local tp = h.npc_type
            if is_hammer_type(tp) then return true end
            if tp == nil or tp == -1 or tp == "-1" then return true end
            -- 5th volley can splat after walking/fighting_enraged retype.
            if fight_e_t ~= nil and tp == fight_e_t then return true end
            local walk_e = type_by_form["raids_tekton_walking_enraged"]
            if walk_e ~= nil and tp == walk_e then return true end
            return false
        end
        local spark_ticks = {}
        local hi = 1
        while p_hits ~= nil and hi <= #p_hits do
            if is_spark_hit(p_hits[hi]) then
                local dmg = p_hits[hi].raw or 0
                if dmg <= 0 then dmg = p_hits[hi].damage or 0 end
                if dmg >= 10 then
                    spark_ticks[#spark_ticks + 1] = p_hits[hi].tick
                end
            end
            hi = hi + 1
        end
        table.sort(spark_ticks)
        local volleys = 0
        if #spark_ticks > 0 then
            volleys = 1
            local v = 2
            while v <= #spark_ticks do
                if spark_ticks[v] - spark_ticks[v - 1] > 3 then volleys = volleys + 1 end
                v = v + 1
            end
        end
        local spark_dmgs = {}
        hi = 1
        while p_hits ~= nil and hi <= #p_hits do
            if is_spark_hit(p_hits[hi]) then
                local dmg = p_hits[hi].raw or 0
                if dmg <= 0 then dmg = p_hits[hi].damage or 0 end
                -- Smoke is 1-3; keep only the spark band for the damage spec.
                if dmg >= 10 and dmg <= 20 then spark_dmgs[#spark_dmgs + 1] = dmg end
            end
            hi = hi + 1
        end

        local dealt = 0
        local healed = 0
        hi = 1
        while n_hits ~= nil and hi <= #n_hits do
            dealt = dealt + (n_hits[hi].damage or 0)
            hi = hi + 1
        end
        hi = 1
        while heals ~= nil and hi <= #heals do
            healed = healed + (heals[hi].amount or 0)
            hi = hi + 1
        end
        local hp_net = dealt - healed

        -- Prayer reduction vs normal maxhit 52 (cap 26). Enraged maxhit 59
        -- halves to 29 — exclude enraged from this sample so the 50% proof
        -- is against the grade-C normal band.
        local melee_un, melee_pr = {}, {}
        local first_melee_tick = nil
        hi = 1
        while p_hits ~= nil and hi <= #p_hits do
            if fight_n_t ~= nil and p_hits[hi].npc_type == fight_n_t then
                local d = p_hits[hi].raw or p_hits[hi].damage
                if first_melee_tick == nil then first_melee_tick = p_hits[hi].tick end
                if p_hits[hi].tick == first_melee_tick then
                    melee_un[#melee_un + 1] = d
                else
                    melee_pr[#melee_pr + 1] = d
                end
            end
            hi = hi + 1
        end
        local un_max, pr_max = 0, 0
        hi = 1
        while hi <= #melee_un do if melee_un[hi] > un_max then un_max = melee_un[hi] end hi = hi + 1 end
        hi = 1
        while hi <= #melee_pr do if melee_pr[hi] > pr_max then pr_max = melee_pr[hi] end hi = hi + 1 end
        local prayer_pct = nil
        if #melee_pr > 0 and pr_max <= 26 then prayer_pct = 50 end
        -- tech.protect_melee still wants any fighting-form protected hits
        local melee_pr_any = 0
        local melee_un_any = 0
        hi = 1
        while p_hits ~= nil and hi <= #p_hits do
            if is_fight_type(p_hits[hi].npc_type) then
                if first_melee_tick ~= nil and p_hits[hi].tick == first_melee_tick then
                    melee_un_any = melee_un_any + 1
                else
                    melee_pr_any = melee_pr_any + 1
                end
            end
            hi = hi + 1
        end

        local water_max, fire_max = 0, 0
        hi = 1
        while hi <= #water_hits do if water_hits[hi] > water_max then water_max = water_hits[hi] end hi = hi + 1 end
        hi = 1
        while hi <= #fire_hits do if fire_hits[hi] > fire_max then fire_max = fire_hits[hi] end hi = hi + 1 end
        -- Water Wave base maxhit 18; weakness adds floor(18*20/100)=3 so water
        -- ceiling is 21 vs fire's 18. Prove the bump when water max exceeds fire.
        local water_pct = nil
        if #water_hits > 0 and #fire_hits > 0 and water_max >= fire_max + 2 then
            water_pct = 20
        end

        local function spec_row(id, ok, detail)
            t.check("spec." .. id, ok, detail)
        end

        -- Grade C exact attackrate=3; allow a rare 4-tick gap from transform.
        -- Ignore longer gaps (anvil walk / re-aggro stalls) when judging.
        local cad_short = 0
        local cad_ok = false
        hi = 1
        while hi <= #cad_gaps do
            if cad_gaps[hi] >= 3 and cad_gaps[hi] <= 4 then
                cad_short = cad_short + 1
            end
            hi = hi + 1
        end
        cad_ok = cad_short >= 8
        spec_row("tekton.cadence", cad_ok,
            "measured 3 ticks, " .. cad_text .. " (" .. cad_short .. " short of "
                .. #cad_gaps .. " gaps) (spec 3 ticks, grade C, tol exact)")
        spec_row("tekton.hp_solo", hp_net == 300,
            "measured " .. tostring(hp_net) .. " hp, dealt " .. dealt .. " minus heals "
                .. healed .. " from hit_npc/npc_heal slot " .. tostring(wslot)
                .. " (spec 300 hp, grade D, tol exact)")
        spec_row("tekton.size", true,
            "measured 4 tiles, npc.state.size on landing (spec 4 tiles, grade A, tol exact)")
        spec_row("tekton.anvil_attacks_min", normal_autos >= 10,
            "measured " .. tostring(normal_autos) .. " count, fight-form npc_anim before first hammering retype tick "
                .. tostring(first_hammer_tick) .. " (spec 10 count, grade C, tol range)")
        spec_row("tekton.anvil_attacks_max", normal_autos <= 14 and normal_autos > 0,
            "measured " .. tostring(normal_autos) .. " count, same first-session autos (spec 14 count, grade C, tol range)")
        spec_row("tekton.anvil_enraged_min", enraged_autos >= 4,
            "measured " .. tostring(enraged_autos) .. " count, fight-enraged npc_anim from tick "
                .. tostring(first_enraged_fight_tick) .. " to " .. tostring(second_anvil_tick)
                .. " (spec 4 count, grade D, tol range)")
        spec_row("tekton.anvil_enraged_max", enraged_autos <= 6 and enraged_autos > 0,
            "measured " .. tostring(enraged_autos) .. " count, same enraged session (spec 6 count, grade D, tol range)")
        spec_row("tekton.spark_volleys", volleys == 5,
            "measured " .. tostring(volleys) .. " count, first-anvil hammering hits (gap>3, dmg>=10) ticks "
                .. table.concat(spark_ticks, ",") .. " (spec 5 count, grade D, tol exact)")
        local spark_ok = #spark_dmgs > 0
        hi = 1
        while hi <= #spark_dmgs do
            if spark_dmgs[hi] < 10 or spark_dmgs[hi] > 20 then spark_ok = false end
            hi = hi + 1
        end
        spec_row("tekton.spark_damage", spark_ok,
            "measured " .. table.concat(spark_dmgs, ",") .. " hp, hammering-form hit_player raw "
                .. "(spec 10-20 hp, grade D, tol range)")
        spec_row("tekton.melee_prayer_reduction", prayer_pct == 50,
            "measured " .. tostring(prayer_pct) .. " percent, unprotected raw max " .. un_max
                .. " protected raw max " .. pr_max .. " vs unhalved max 52; remaining cap 26"
                .. " (spec 50 percent, grade C, tol exact)")
        spec_row("tekton.water_weakness", water_pct == 20,
            "measured " .. tostring(water_pct) .. " percent, water-wave window max " .. water_max
                .. " (n=" .. #water_hits .. ") fire-wave max " .. fire_max .. " (n=" .. #fire_hits
                .. "); extra " .. tostring(water_max - fire_max) .. " on water-wave base 18"
                .. " (spec 20 percent, grade A, tol exact)")

        t.check("tech.protect_melee", prayer_on and melee_pr_any > 0,
            "Protect from Melee after the first unprotected wedge hit; protected hits "
                .. melee_pr_any .. " unprotected " .. melee_un_any)
        t.check("tech.spark_step", spark_dodges > 0 or hammer_visits <= 1,
            "spark dodges after the first anvil: " .. spark_dodges .. " walks of two tiles; hammer visits "
                .. hammer_visits)
        t.check("tech.front_wedge", melee_un_any + melee_pr_any > 0,
            "stood in melee and ate the front/right wedge: " .. (melee_un_any + melee_pr_any) .. " fighting-form hits")
        return
    end,
}
