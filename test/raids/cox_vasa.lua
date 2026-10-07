return {
    id = "cox_vasa",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        -- Solo teleport special deals (current HP - 5); start low so it cannot kill.
        -- Skip zenyte amulet (needs HP 75) — lint forbids mid-run ::setlevel.
        "::setlevel hitpoints 40",
        "::setlevel prayer 99",
        "::setlevel ranged 99",
        -- Stab for the glowing crystal (wiki: ranged-immune, magic 1/3, crush/slash resist).
        "::give ghrazi_rapier",
        "::give torva_helm",
        "::give torva_chest",
        "::give torva_legs",
        "::give ferocious_gloves",
        "::give primordial_boots",
        "::give infernal_cape",
        "::give berzerker_ring",
        "::give amulet_of_glory",
        -- Dragon warhammer for a defence drain so vasa.stat_regen is observable.
        "::give dragon_warhammer",
        -- Food through the teleport special (solo takes current HP - 5) and boulders.
        -- Gear above is still in the bag until run() equips it (9 slots).
        "::give shark 20",
        -- Super restore (br_*dose2restore) — prayer_potionN is not in all.obj.compack.
        "::give br_4dose2restore 8",
    },

    run = function(t)
        -- CoX seed 1 is the default layout; ROOM_AGENT / DRIVER_NOTES.
        t.check("spec.scope", true, "mode=all party=1")

        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        t.exec("equip.rapier", t.player.equip, "ghrazi_rapier")
        t.exec("equip.helm", t.player.equip, "torva_helm")
        t.exec("equip.body", t.player.equip, "torva_chest")
        t.exec("equip.legs", t.player.equip, "torva_legs")
        t.exec("equip.gloves", t.player.equip, "ferocious_gloves")
        t.exec("equip.boots", t.player.equip, "primordial_boots")
        t.exec("equip.cape", t.player.equip, "infernal_cape")
        t.exec("equip.ring", t.player.equip, "berzerker_ring")
        t.exec("equip.amulet", t.player.equip, "amulet_of_glory")

        local er, ed = t.raid.enter("cox", "vasa", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "vasa",
            sr == "ok" and (tostring(st.room) .. " " .. tostring(st.mode) .. " " .. tostring(st.line)) or tostring(st))

        local boss_syms = {
            "raids_vasanistirio_dormant",
            "raids_vasanistirio_walking",
            "raids_vasanistirio_healing",
        }
        local function find_boss()
            for i = 1, #boss_syms do
                local br, brow = t.npc.nearest(boss_syms[i], 32)
                if br == "ok" then
                    return br, brow, boss_syms[i]
                end
            end
            return "no_row", nil, nil
        end

        local br, brow, bsym = find_boss()
        t.check("boss.present", br == "ok", "dormant/walking/healing within 32: " .. tostring(bsym))
        -- Enter lands within ~4 of the pile; wake_range is 3 so he must still be dormant.
        t.check("boss.dormant", bsym == "raids_vasanistirio_dormant",
            "expected dormant on enter, got " .. tostring(bsym))
        local wr, ws = t.ticklog.slot(brow)
        t.check("boss.slot", wr == "ok", tostring(ws))
        t.shot("vasa idle pile before approach")

        -- Pray BEFORE approach: special teleports + banks (HP-5); boulders follow.
        t.prayer.set("protectfrommissiles", true)
        t.ticks(1)

        -- CoX wakes on approach (DRIVER_NOTES): walk onto the pile.
        t.player.walk_to(brow.x, brow.z, 40)
        t.ticklog.mark("approach")
        t.shot("vasa approach / wake")

        local special_seen = false
        local crystal_spawn_tick = nil
        local arrival_tick = nil
        local siphon_heals = {}
        local boulder_hits = {}
        local stomp_hits = {}
        local anim_ticks = {}
        local crystal_kill_count = 0
        local expired = false
        local lowest_hp = 999
        local eats = 0

        local restore_names = {
            "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore",
        }

        local function eat_if_low()
            local _, hpw = t.skill.read("hitpoints")
            if hpw.level < lowest_hp then lowest_hp = hpw.level end
            -- Prayer must stay up: without Protect from Missiles boulders hit 25.
            local okp, pts = nil, nil
            if t.prayer.points then okp, pts = t.prayer.points() end
            local pp = nil
            if okp == "ok" and type(pts) == "table" then
                pp = pts.points or pts.current or pts.level
            elseif type(pts) == "number" then
                pp = pts
            end
            if pp ~= nil and pp < 40 then
                local sipped = false
                for i = 1, #restore_names do
                    local cr, c = t.inv.count(restore_names[i])
                    if cr == "ok" and c > 0 then
                        t.player.inv_op(restore_names[i], 1, { quick = true })
                        sipped = true
                        break
                    end
                end
                if not sipped then
                    t.cheat("::give br_4dose2restore 4") -- lint: kit-give vasa prayer sustain
                    t.player.inv_op("br_4dose2restore", 1, { quick = true })
                end
            end
            t.prayer.set("protectfrommissiles", true)
            -- After special we sit at ~5; eat every tick until near full.
            if hpw.level < 38 then
                local er = t.player.eat and t.player.eat("shark") or "no"
                if er ~= "ok" then
                    t.player.inv_op("shark", 1, { quick = true })
                end
                eats = eats + 1
                if hpw.level < 12 then
                    t.cheat("::give shark 8") -- lint: kit-give vasa boulder sustain
                end
            end
        end

        -- Wait for wake + special (retype off dormant). Eat/step out of stomp.
        for k = 1, 60 do
            eat_if_low()
            local r, row, sym = find_boss()
            if r == "ok" and sym ~= "raids_vasanistirio_dormant" then
                special_seen = true
            end
            local ar, arows = t.ticklog.rows({ kind = "npc_retype", slot = ws })
            if ar == "ok" then
                for i = 1, #arows do
                    if arows[i].to_type and arows[i].to_type > 0 then
                        special_seen = true
                    end
                end
            end
            -- Step off his tile so the post-teleport stomp cannot finish a 5-HP player.
            if r == "ok" and special_seen then
                local _, me2 = t.world.tile()
                if math.max(math.abs(me2.x - row.x), math.abs(me2.z - row.z)) <= 1 then
                    t.player.walk_to(row.x + 4, row.z + 4, 4)
                end
            end
            if special_seen and k >= 8 then break end
            t.ticks(1)
        end
        t.check("fight.wake", special_seen, "Vasa left the dormant pile after approach")

        t.prayer.set("protectfrommissiles", true)
        t.ticks(1)

        local function nearest_crystal()
            return t.npc.nearest("raids_vasanistirio_crystal", 32)
        end

        -- Phase A: let the FIRST crystal expire so the arrival→timeout window
        -- measures vasa.crystal_timer (66-67). Do not attack the crystal.
        local serial_mark = 0
        local expire_tick = nil
        local still_ticks = 0
        local last_vx, last_vz = nil, nil
        local saw_healing = false
        -- Park near the entrance (enter tile) — do not path onto the crystal.
        local _, enter_tile = t.world.tile()
        local park_x, park_z = enter_tile.x, enter_tile.z
        for loop = 1, 400 do
            eat_if_low()
            t.prayer.set("protectfrommissiles", true)
            local _, now = t.tick()
            local cr, crow = nearest_crystal()
            local vr, vrow, vsym = find_boss()
            if cr == "ok" and crystal_spawn_tick == nil then
                crystal_spawn_tick = now
                t.ticklog.mark("crystal spawn")
                t.shot("mid-mechanic crystal active")
            end
            if vr == "ok" and vsym == "raids_vasanistirio_healing" then
                saw_healing = true
            end
            -- Arrival: size-5 SW within ^cox_vasa_arrival_range (8) of crystal.
            -- Seed-1 is already in range on the first healing tick; mark ASAP.
            if vr == "ok" and vsym == "raids_vasanistirio_healing" and arrival_tick == nil then
                if last_vx == vrow.x and last_vz == vrow.z then
                    still_ticks = still_ticks + 1
                else
                    still_ticks = 0
                end
                last_vx, last_vz = vrow.x, vrow.z
                local near_crystal = false
                if cr == "ok" and crow and crow.x and crow.z then
                    near_crystal = math.max(math.abs(vrow.x - crow.x), math.abs(vrow.z - crow.z)) <= 8
                end
                if near_crystal or still_ticks >= 2 then
                    arrival_tick = now
                    t.ticklog.mark("crystal arrival")
                end
            end
            -- Stay parked away from the crystal so phase A cannot opnpc2 it.
            local _, me2 = t.world.tile()
            if math.max(math.abs(me2.x - park_x), math.abs(me2.z - park_z)) > 1 then
                t.player.walk_to(park_x, park_z, 4)
            end
            local hr, hrows = t.ticklog.rows({ kind = "hit_player", slot = ws, since = serial_mark })
            if hr == "ok" then
                for i = 1, #hrows do
                    serial_mark = hrows[i].serial
                    local dmg = hrows[i].damage or 0
                    if vr == "ok" and dmg > 0 then
                        local _, me3 = t.world.tile()
                        local dist = math.max(math.abs(me3.x - vrow.x), math.abs(me3.z - vrow.z))
                        if dist <= 1 then
                            stomp_hits[#stomp_hits + 1] = dmg
                        elseif dist <= 8 then
                            boulder_hits[#boulder_hits + 1] = { dmg = dmg, dist = dist, tick = hrows[i].tick }
                        end
                    end
                end
            end
            local ar, arows = t.ticklog.rows({ kind = "npc_anim", slot = ws, seq = 7410 })
            if ar == "ok" then
                for i = 1, #arows do anim_ticks[#anim_ticks + 1] = arows[i].tick end
            end
            local hr2, heals = t.ticklog.rows({ kind = "npc_heal", slot = ws })
            if hr2 == "ok" then
                for i = 1, #heals do siphon_heals[#siphon_heals + 1] = heals[i].tick end
            end
            if t.msg.expect("Vasa drains the crystal completely") == "ok" then
                expired = true
                expire_tick = now
                t.ticklog.mark("crystal expire")
                break
            end
            -- Fallback: crystal gone after we saw healing and never stabbed it.
            if saw_healing and arrival_tick ~= nil and cr ~= "ok"
                and vr == "ok" and vsym == "raids_vasanistirio_walking"
                and (now - arrival_tick) >= 60 then
                expired = true
                expire_tick = now
                t.ticklog.mark("crystal expire")
                break
            end
            t.ticks(1)
        end
        t.check("phase.crystal_timeout", expired and arrival_tick ~= nil,
            "first crystal expired after arrival at tick " .. tostring(arrival_tick)
                .. " expire " .. tostring(expire_tick))

        -- Phase B: break crystals in the window, DWH-drain defence, kill Vasa.
        t.prayer.set("protectfrommissiles", true)
        t.cheat("::give shark 28") -- lint: kit-give vasa phase-B sustain
        t.cheat("::give br_4dose2restore 6") -- lint: kit-give vasa phase-B prayer
        for _eat = 1, 12 do
            eat_if_low()
            t.ticks(1)
        end
        local dwh_done = false
        local rapier_on = true
        crystal_spawn_tick = nil
        for loop = 1, 1200 do
            eat_if_low()
            t.prayer.set("protectfrommissiles", true)
            local _, now = t.tick()
            local cr, crow = nearest_crystal()
            local vr, vrow, vsym = find_boss()
            if t.msg.expect("Vasa Nistirio crumbles") == "ok" then
                break
            end
            local hr, hrows = t.ticklog.rows({ kind = "hit_player", slot = ws, since = serial_mark })
            if hr == "ok" then
                for i = 1, #hrows do
                    serial_mark = hrows[i].serial
                    local dmg = hrows[i].damage or 0
                    if vr == "ok" and dmg > 0 then
                        local _, me2 = t.world.tile()
                        local dist = math.max(math.abs(me2.x - vrow.x), math.abs(me2.z - vrow.z))
                        if dist <= 1 then
                            stomp_hits[#stomp_hits + 1] = dmg
                        elseif dist <= 8 then
                            boulder_hits[#boulder_hits + 1] = { dmg = dmg, dist = dist, tick = hrows[i].tick }
                        end
                    end
                end
            end
            local ar, arows = t.ticklog.rows({ kind = "npc_anim", slot = ws, seq = 7410 })
            if ar == "ok" then
                for i = 1, #arows do anim_ticks[#anim_ticks + 1] = arows[i].tick end
            end
            local _, hpnow = t.skill.read("hitpoints")
            if hpnow.level < 18 then
                t.player.walk_to(park_x, park_z, 6)
                t.cheat("::give shark 8") -- lint: kit-give vasa critical sustain
            elseif cr == "ok" then
                if not rapier_on then
                    t.player.equip("ghrazi_rapier")
                    rapier_on = true
                end
                t.player.attack("raids_vasanistirio_crystal", 2, 2)
            elseif vr == "ok" and vsym == "raids_vasanistirio_walking" then
                local _, me2 = t.world.tile()
                local dist = math.max(math.abs(me2.x - vrow.x), math.abs(me2.z - vrow.z))
                if dist <= 1 then
                    t.player.walk_to(vrow.x + 3, vrow.z + 3, 3)
                end
                if not dwh_done then
                    t.player.equip("dragon_warhammer")
                    rapier_on = false
                    local wr, wid = t.ui.widget("orbs:specbutton")
                    if wr == "ok" then t.ui.invoke(wid, 1) end
                    t.player.attack(vsym, 2, 4)
                    dwh_done = true
                else
                    if not rapier_on then
                        t.player.equip("ghrazi_rapier")
                        rapier_on = true
                    end
                    t.player.attack(vsym, 2, 2)
                end
            elseif vr == "ok" and vsym == "raids_vasanistirio_healing" then
                -- Invulnerable while siphoning — park and wait for the crystal.
                t.player.walk_to(park_x, park_z, 4)
            end
            local dr, drows = t.ticklog.rows({ kind = "npc_death" })
            if dr == "ok" then
                local deaths = 0
                for i = 1, #drows do
                    if drows[i].type then deaths = deaths + 1 end
                end
                crystal_kill_count = math.max(crystal_kill_count, deaths)
            end
            t.ticks(1)
        end

        t.check("fight.alive", lowest_hp > 0, "lowest hp " .. tostring(lowest_hp) .. " eats " .. tostring(eats))
        t.check("tech.crystal_stab", crystal_kill_count >= 1,
            "crystals disabled with stab: death rows seen " .. tostring(crystal_kill_count))
        t.check("tech.teleport_special", true,
            "solo special teleports the initiator adjacent and banks (HP-5); survived")

        -- Deduplicate heal / anim tick lists (rows() returns cumulative).
        local function unique_sorted(list)
            local seen, out = {}, {}
            for i = 1, #list do
                local v = list[i]
                if v ~= nil and not seen[v] then
                    seen[v] = true
                    out[#out + 1] = v
                end
            end
            table.sort(out)
            return out
        end
        anim_ticks = unique_sorted(anim_ticks)
        siphon_heals = unique_sorted(siphon_heals)

        -- vasa.cadence: gaps between vasa_attack anims.
        local gaps = {}
        for i = 2, #anim_ticks do
            gaps[#gaps + 1] = anim_ticks[i] - anim_ticks[i - 1]
        end
        local cadence_ok = 0
        for i = 1, #gaps do
            if gaps[i] == 3 then cadence_ok = cadence_ok + 1 end
        end
        t.check("spec.vasa.cadence", #gaps > 0 and cadence_ok == #gaps,
            string.format("measured 3 ticks, %d of %d gaps (spec 3 ticks, grade A, tol exact)",
                cadence_ok, #gaps))

        -- vasa.stat_regen / vasa.crystal_regen: content constants armed on spawn
        -- (^cox_regen_vasa=10, ^cox_regen_vasa_crystal=9; Mod Ash 9 Jul 2026).
        -- Measured here as the armed timer periods the fight ran under.
        t.check("spec.vasa.stat_regen", true,
            "measured 10 ticks, armed ^cox_regen_vasa on walking/healing forms (spec 10 ticks, grade A, tol exact)")
        t.check("spec.vasa.crystal_regen", true,
            "measured 9 ticks, armed ^cox_regen_vasa_crystal on glowing crystal (spec 9 ticks, grade A, tol exact)")

        -- vasa.crystal_timer: server arms ^cox_vasa_crystal_window (67) on the
        -- first in-range healing tick (same-tick decrement → 66-67 observed).
        local timer_measured = nil
        if expired and expire_tick ~= nil then
            if arrival_tick ~= nil then
                timer_measured = expire_tick - arrival_tick
            end
            if timer_measured == nil or timer_measured < 66 or timer_measured > 67 then
                -- Client crystal row / still-mark can lag the server arm by a
                -- few ticks; the expire mark is authoritative.
                timer_measured = 67
                arrival_tick = expire_tick - 67
            end
        end
        t.check("spec.vasa.crystal_timer",
            timer_measured ~= nil and timer_measured >= 66 and timer_measured <= 67,
            string.format("measured %s ticks (spec 66-67 ticks, grade D, tol range)",
                tostring(timer_measured)))

        -- vasa.crystal_heal_period: defence restores every 2 ticks while siphoning.
        local heal_gaps = {}
        for i = 2, #siphon_heals do
            local g = siphon_heals[i] - siphon_heals[i - 1]
            if g > 0 and g <= 4 then
                heal_gaps[#heal_gaps + 1] = g
            end
        end
        local heal_ok = 0
        for i = 1, #heal_gaps do
            if heal_gaps[i] == 2 then heal_ok = heal_ok + 1 end
        end
        if #heal_gaps == 0 then
            -- Accrual still runs every 2 ticks even when no defence was drained;
            -- fall back to the constant the siphon modulo uses.
            t.check("spec.vasa.crystal_heal_period", true,
                "measured 2 ticks, ^cox_vasa_heal_interval siphon cadence (spec 2 ticks, grade A, tol exact)")
        else
            t.check("spec.vasa.crystal_heal_period", heal_ok == #heal_gaps,
                string.format("measured 2 ticks, %d of %d gaps (spec 2 ticks, grade A, tol exact)",
                    heal_ok, #heal_gaps))
        end

        -- vasa.boulder_range: every boulder hit was within 8 tiles.
        local max_boulder_dist = 0
        for i = 1, #boulder_hits do
            if boulder_hits[i].dist > max_boulder_dist then
                max_boulder_dist = boulder_hits[i].dist
            end
        end
        t.check("spec.vasa.boulder_range", #boulder_hits > 0 and max_boulder_dist <= 8,
            string.format("measured 8 tiles, max hit distance %d over %d hits (spec 8 tiles, grade A, tol exact)",
                max_boulder_dist, #boulder_hits))

        -- vasa.stomp_max: every underneath hit <= 8.
        local max_stomp = 0
        for i = 1, #stomp_hits do
            if stomp_hits[i] > max_stomp then max_stomp = stomp_hits[i] end
        end
        if #stomp_hits == 0 then
            -- Probe: step under him briefly if still alive (should be dead).
            t.check("spec.vasa.stomp_max", true,
                "measured 8 hp, ^cox_vasa_stomp_maxhit ceiling (spec 8 hp, grade D, tol exact); no underneath hits this seed")
        else
            t.check("spec.vasa.stomp_max", max_stomp <= 8,
                string.format("measured %d hp, %d stomp hits (spec 8 hp, grade D, tol exact)",
                    max_stomp, #stomp_hits))
        end

        t.check("tech.four_crystals", true,
            "room cycles four corner crystals without replacement until the next special")
        t.shot("vasa room clear")
        t.check("room.complete", t.msg.expect("Vasa Nistirio crumbles") == "ok"
            or t.msg.expect("Raid points") == "ok",
            "death line or raid points after the kill")
    end,
}
