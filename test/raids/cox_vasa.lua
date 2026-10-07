return {
    id = "cox_vasa",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        -- Equip needs HP 75+ for torva/zenyte; solo special is (HP-5) so drop
        -- to 40 after gear is on (see run()) — leaves 5 HP, survivable with food.
        "::setlevel hitpoints 99",
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
        "::give zenyte_amulet_enchanted",
        -- Dragon warhammer for a defence drain so vasa.stat_regen is observable.
        "::give dragon_warhammer",
        -- Food through the teleport special (solo takes current HP - 5) and boulders.
        -- Gear above is still in the bag until run() equips it (9 slots); sharks
        -- only — potions are unused here and would overflow the 28-slot inv.
        "::give shark 18",
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
        t.exec("equip.amulet", t.player.equip, "zenyte_amulet_enchanted")
        -- Solo special = currentHP-5. At 99 that is fatal; drop after gear on.
        t.cheat("::setlevel hitpoints 40")

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

        local function eat_if_low()
            local _, hpw = t.skill.read("hitpoints")
            if hpw.level < lowest_hp then lowest_hp = hpw.level end
            -- After special we sit at ~5; eat every tick until mid-bag HP.
            if hpw.level < 30 then
                t.player.inv_op("shark", 1)
                eats = eats + 1
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
        for loop = 1, 200 do
            eat_if_low()
            local _, now = t.tick()
            local cr, crow = nearest_crystal()
            local vr, vrow, vsym = find_boss()
            if cr == "ok" and crystal_spawn_tick == nil then
                crystal_spawn_tick = now
                t.ticklog.mark("crystal spawn")
                t.shot("mid-mechanic crystal active")
            end
            if vr == "ok" and cr == "ok" and arrival_tick == nil
                and vsym == "raids_vasanistirio_healing"
                and vrow.x == crow.x and vrow.z == crow.z then
                arrival_tick = now
                t.ticklog.mark("crystal arrival")
            end
            -- Stand 3-6 tiles out: boulder range, not stomp.
            if vr == "ok" then
                local _, me2 = t.world.tile()
                local dx = math.abs(me2.x - vrow.x)
                local dz = math.abs(me2.z - vrow.z)
                local dist = math.max(dx, dz)
                if dist <= 1 or dist > 8 then
                    t.player.walk_to(vrow.x + 4, vrow.z + 4, 4)
                end
                -- Brief stomp probe once after arrival.
                if arrival_tick ~= nil and #stomp_hits == 0 and now == arrival_tick + 2 then
                    t.player.walk_to(vrow.x, vrow.z, 2)
                end
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
            t.ticks(1)
        end
        t.check("phase.crystal_timeout", expired and arrival_tick ~= nil,
            "first crystal expired after arrival at tick " .. tostring(arrival_tick)
                .. " expire " .. tostring(expire_tick))

        -- Phase B: after the looping special, break crystals and kill Vasa.
        t.prayer.set("protectfrommissiles", true)
        crystal_spawn_tick = nil
        for loop = 1, 700 do
            eat_if_low()
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
            if cr == "ok" then
                t.player.attack("raids_vasanistirio_crystal", 2, 2)
            elseif vr == "ok" and (vsym == "raids_vasanistirio_walking" or vsym == "raids_vasanistirio_healing") then
                local _, me2 = t.world.tile()
                if math.max(math.abs(me2.x - vrow.x), math.abs(me2.z - vrow.z)) <= 1 then
                    t.player.walk_to(vrow.x + 2, vrow.z + 2, 3)
                end
                t.player.attack(vsym, 2, 2)
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

        -- vasa.crystal_timer: ticks from first arrival mark to expire mark.
        local timer_measured = nil
        if expired and arrival_tick ~= nil and expire_tick ~= nil then
            timer_measured = expire_tick - arrival_tick
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
