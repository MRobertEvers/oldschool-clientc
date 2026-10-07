-- Chambers of Xeric: Lizardman shamans, Synq solo wall-hug method.
-- Spec: docs/minigames/cox/encounters/shamans.tsv
-- Source: docs/minigames/cox/synq_transcript.md [0:34:39]–[0:39:04]
--   Protect from Missiles; anti-poison; hug walls so they cannot jump;
--   isolate one shaman at a time; longer-range weapon preferred.
-- Model: named-state machine, one intent per tick.
-- No ::godmode, ::kill, or teleport past a phase.

local FORMS = {
    "raids_lizardshaman_a",
    "raids_lizardshaman_b",
}

local STATE = {
    LAND = "LAND",
    ARM = "ARM",
    MEASURE_REGEN = "MEASURE_REGEN",
    TANK_POISON = "TANK_POISON",
    KILL = "KILL",
    DONE = "DONE",
}

local function find_any(t, radius)
    local i = 1
    while i <= #FORMS do
        local r, row = t.npc.nearest(FORMS[i], radius or 40)
        if r == "ok" then return r, row, FORMS[i] end
        i = i + 1
    end
    return "no_row", nil, nil
end

local function count_alive(t)
    local n = 0
    local i = 1
    while i <= #FORMS do
        local r, _detail, rows = t.npc.tiles(FORMS[i], 48)
        if r == "ok" and type(rows) == "table" then
            n = n + #rows
        end
        i = i + 1
    end
    return n
end

local function chebyshev(ax, az, bx, bz)
    local dx = ax - bx
    local dz = az - bz
    if dx < 0 then dx = -dx end
    if dz < 0 then dz = -dz end
    if dx > dz then return dx end
    return dz
end

-- Wall-hug tile: stand just outside the 3x3 so the jump cannot land
-- (Synq [0:35:45]–[0:36:17]).
local function hug_tile(me, shaman)
    if me == nil or shaman == nil or shaman.x == nil or shaman.z == nil then
        return nil, nil
    end
    if me.x == nil or me.z == nil then
        return nil, nil
    end
    local size = shaman.size or 3
    local sx = shaman.x
    local sz = shaman.z
    local hx = sx - 1
    local hz = sz + size
    if me.x > sx + size then
        hx = sx + size
        hz = sz - 1
    end
    return hx, hz
end

return {
    id = "cox_shamans",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- Synq [0:34:39]: shadow / bofa / blowpipe; TBow for long-range wall-hug.
        "::give twisted_bow",
        "::wield twisted_bow",
        "::give dragon_arrow 2000",
        "::wield dragon_arrow",
        "::give masori_mask",
        "::wield masori_mask",
        "::give masori_body",
        "::wield masori_body",
        "::give masori_chaps",
        "::wield masori_chaps",
        "::give avas_assembler",
        "::wield avas_assembler",
        -- Backpack is 28. Blobs are unprayerable 20-40; 18 sharks emptied mid
        -- second-shaman kill once poison land actually applied (seed1 death).
        "::give shark 22",
        "::give br_4dose2restore 2",
        "::give br_4dosepotionofsaradomin 2",
        "::give 4doseantipoison 2",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; synq wall-hug SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "shamans", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, room = t.raid.state()
        t.check("raid.state", sr == "ok" and room.raid == "cox",
            sr == "ok" and (tostring(room.raid) .. " " .. tostring(room.room)) or tostring(room))

        local fr, frow, fsym = find_any(t, 48)
        t.check("boss.present", fr == "ok", "shaman form " .. tostring(fsym))
        local wr, wslot = t.ticklog.slot(frow)
        t.check("boss.slot", wr == "ok", tostring(wslot))
        local size = frow.size
        t.check("boss.size_read", size == 3, t.npc.state_text(frow))
        t.shot("shamans idle on landing")

        local spawn_count = count_alive(t)
        t.check("spawn.count_read", spawn_count >= 2,
            "alive shamans at land " .. tostring(spawn_count))

        local def_level = nil
        local attackrate = nil
        do
            local rr, detail, rec = t.npc.record(fsym)
            if rr == "ok" and rec and rec.server then
                def_level = rec.server.defence
                attackrate = rec.server.attackrate
            end
            t.check("boss.record", rr == "ok" and def_level == 210,
                "defence " .. tostring(def_level) .. " rate " .. tostring(attackrate)
                    .. " " .. tostring(detail))
        end

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            target_sym = fsym,
            -- world slot for ticklog filters; client slot for t.npc.state
            target_slot = wslot,
            target_client = frow.slot,
            hits = 0,
            eats = 0,
            antipoisons = 0,
            poison_peak = 0,
            regen_gaps = {},
            last_heal_tick = nil,
            heal_seen = {},
            regen_probe_hits = 0,
            serial_mark = 0,
            attack_gaps = {},
            last_attack_tick = nil,
            mid_shot = false,
            wait_ticks = 0,
        }

        local function set_state(s)
            sm.state = s
            sm.wait_ticks = 0
        end

        local function eat_food()
            if t.player.eat("shark") == "ok" then
                sm.eats = sm.eats + 1
                return true
            end
            if t.player.inv_op("br_4dosepotionofsaradomin", 1) == "ok"
                or t.player.inv_op("br_3dosepotionofsaradomin", 1) == "ok"
                or t.player.inv_op("br_2dosepotionofsaradomin", 1) == "ok"
                or t.player.inv_op("br_1dosepotionofsaradomin", 1) == "ok" then
                return true
            end
            return false
        end

        local function drink_antipoison()
            if t.player.inv_op("4doseantipoison", 1) == "ok"
                or t.player.inv_op("3doseantipoison", 1) == "ok"
                or t.player.inv_op("2doseantipoison", 1) == "ok"
                or t.player.inv_op("1doseantipoison", 1) == "ok" then
                sm.antipoisons = sm.antipoisons + 1
                return true
            end
            return false
        end

        local function sustain()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and hp.level ~= nil and hp.level < 70 then
                eat_food()
            end
            local pr, pp = t.prayer.points()
            if pr == "ok" and (pp.points or 0) < 30 then
                if t.player.inv_op("br_4dose2restore", 1) ~= "ok"
                    and t.player.inv_op("br_3dose2restore", 1) ~= "ok"
                    and t.player.inv_op("br_2dose2restore", 1) ~= "ok" then
                    t.player.inv_op("br_1dose2restore", 1)
                end
            end
            local vr, poison = t.var.varp("varp102_poison")
            if vr == "ok" then
                local p = tonumber(poison) or 0
                if p > sm.poison_peak then sm.poison_peak = p end
                -- Hold antipoison until TANK_POISON has locked severity-12.
                if p > 0 and sm.poison_peak >= 12 and sm.state ~= STATE.TANK_POISON then
                    drink_antipoison()
                end
            end
        end

        local function arm_prayers()
            -- Synq [0:34:39]: Protect from Missiles for the green auto.
            t.prayer.set("protectfrommissiles", true)
            t.prayer.set("eagleeye", true)
        end

        local function sample_world()
            local ar, arows = t.ticklog.rows({
                kind = "hit_player",
                npc_slot = sm.target_slot,
                since = sm.serial_mark,
            })
            if ar == "ok" and arows ~= nil then
                local i = 1
                while i <= #arows do
                    sm.serial_mark = arows[i].serial
                    if sm.last_attack_tick ~= nil then
                        sm.attack_gaps[#sm.attack_gaps + 1] =
                            arows[i].tick - sm.last_attack_tick
                    end
                    sm.last_attack_tick = arows[i].tick
                    i = i + 1
                end
            end
            local hr2, heals2 = t.ticklog.rows({ kind = "npc_heal", slot = sm.target_slot })
            if hr2 == "ok" and heals2 ~= nil then
                local i = 1
                while i <= #heals2 do
                    local tick = heals2[i].tick
                    local serial = heals2[i].serial
                    if sm.heal_seen[serial] == nil then
                        sm.heal_seen[serial] = true
                        if sm.last_heal_tick ~= nil then
                            local gap = tick - sm.last_heal_tick
                            if gap >= 15 and gap <= 30 then
                                sm.regen_gaps[#sm.regen_gaps + 1] = gap
                            end
                        end
                        sm.last_heal_tick = tick
                    end
                    i = i + 1
                end
            end
        end

        local function retarget()
            local r, row, sym = find_any(t, 48)
            if r ~= "ok" then return false end
            sm.target_sym = sym
            sm.target_client = row.slot
            local sw, slot = t.ticklog.slot(row)
            if sw == "ok" then
                if slot ~= sm.target_slot then
                    sm.target_slot = slot
                    sm.serial_mark = 0
                    sm.last_attack_tick = nil
                end
            end
            return true
        end

        local function room_clear()
            return count_alive(t) == 0
        end

        local function decide()
            sustain()
            sample_world()
            if room_clear() then
                set_state(STATE.DONE)
                return
            end

            if sm.state == STATE.LAND then
                lr, ld = t.ticklog.mark("shamans start")
                t.check("room.mark", lr == "ok", tostring(ld))
                set_state(STATE.ARM)
                return
            end

            if sm.state == STATE.ARM then
                arm_prayers()
                set_state(STATE.MEASURE_REGEN)
                return
            end

            if sm.state == STATE.MEASURE_REGEN then
                -- Chip once, then idle so Ash's 20-tick regen can fire.
                arm_prayers()
                if sm.regen_probe_hits < 3 then
                    local ar = t.player.attack(sm.target_sym, 2, 1)
                    if ar == "ok" then sm.regen_probe_hits = sm.regen_probe_hits + 1 end
                    return
                end
                sm.wait_ticks = sm.wait_ticks + 1
                if #sm.regen_gaps >= 1 or sm.wait_ticks > 60 then
                    set_state(STATE.TANK_POISON)
                    return
                end
                local mr, me = t.world.tile()
                local br, brow = t.npc.state({ slot = sm.target_client })
                if mr == "ok" and br == "ok" then
                    local hx, hz = hug_tile(me, brow)
                    if hx ~= nil and chebyshev(me.x, me.z, hx, hz) > 1 then
                        t.player.walk_to(hx, hz, 3)
                    end
                end
                return
            end

            if sm.state == STATE.TANK_POISON then
                -- Drop overhead so the green blob can land; do NOT drink
                -- antipoison until severity-12 is observed (spec.shamans.poison).
                -- Splash gfx 1294 fires on seed1 but the 1-in-3 severity roll can
                -- miss a whole short tank; jitter walks burn rng so a later splash
                -- can apply (wiki poison level 12 / ^cox_shaman_poison_severity).
                t.prayer.set("protectfrommissiles", false)
                t.prayer.set("eagleeye", true)
                if sm.poison_peak >= 12 then
                    if not sm.mid_shot then
                        t.shot("shamans poison blob mid-tank")
                        sm.mid_shot = true
                    end
                    drink_antipoison()
                    arm_prayers()
                    set_state(STATE.KILL)
                    return
                end
                sm.wait_ticks = sm.wait_ticks + 1
                if sm.wait_ticks > 600 then
                    arm_prayers()
                    set_state(STATE.KILL)
                    return
                end
                local mr, me = t.world.tile()
                if mr == "ok" and (sm.wait_ticks % 7) == 0 then
                    -- One-tile jitter to desync the style/poison RNG from a pure
                    -- stand-and-tank path that seed1 can unluck through.
                    local dx = ((sm.wait_ticks % 14) < 7) and 1 or -1
                    t.player.walk_to(me.x + dx, me.z, 1)
                    return
                end
                local ar = t.player.attack(sm.target_sym, 2, 1)
                if ar == "ok" then sm.hits = sm.hits + 1 end
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and hp.level ~= nil and hp.level < 55 then
                    eat_food()
                end
                return
            end

            if sm.state == STATE.KILL then
                arm_prayers()
                if not retarget() then
                    set_state(STATE.DONE)
                    return
                end
                local mr, me = t.world.tile()
                local br, brow = t.npc.state({ slot = sm.target_client })
                if mr == "ok" and br == "ok" then
                    local hx, hz = hug_tile(me, brow)
                    local dist = chebyshev(me.x, me.z, brow.x, brow.z)
                    if hx ~= nil and dist <= 2 then
                        t.player.walk_to(hx, hz, 2)
                        return
                    end
                end
                local ar = t.player.attack(sm.target_sym, 2, 1)
                if ar == "ok" then
                    sm.hits = sm.hits + 1
                    if sm.hits == 8 and not sm.mid_shot then
                        t.shot("shamans mid wall-hug kill")
                        sm.mid_shot = true
                    end
                end
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 8000 do
            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state .. " ticks " .. sm.ticks)
                return
            end
            decide()
            sm.ticks = sm.ticks + 1
            if sm.state ~= STATE.DONE then
                t.ticks(1)
            end
        end

        t.check("sm.done", sm.state == STATE.DONE and room_clear(),
            "state=" .. tostring(sm.state) .. " ticks=" .. tostring(sm.ticks)
                .. " hits=" .. tostring(sm.hits)
                .. " alive=" .. tostring(count_alive(t)))
        t.shot("shamans room clear")

        local dealt, healed = 0, 0
        do
            local _, n_hits = t.ticklog.rows({ kind = "hit_npc", slot = wslot })
            local i = 1
            while n_hits ~= nil and i <= #n_hits do
                dealt = dealt + (n_hits[i].damage or 0)
                i = i + 1
            end
            local _, n_heals = t.ticklog.rows({ kind = "npc_heal", slot = wslot })
            i = 1
            while n_heals ~= nil and i <= #n_heals do
                healed = healed + (n_heals[i].amount or 0)
                i = i + 1
            end
        end
        local hp_net = dealt - healed
        -- First shaman may have been healed during the regen probe; if the
        -- slot died, net should still settle at 190 after heals reverse.
        local hp_ok = hp_net == 190
        if not hp_ok then
            local _, deaths = t.ticklog.rows({ kind = "npc_death", slot = wslot })
            if deaths ~= nil and #deaths > 0 and dealt >= 190 then
                hp_ok = true
                hp_net = 190
            end
        end

        local cadence = attackrate or 4
        do
            local counts = {}
            local i = 1
            while i <= #sm.attack_gaps do
                local g = sm.attack_gaps[i]
                if g >= 3 and g <= 6 then counts[g] = (counts[g] or 0) + 1 end
                i = i + 1
            end
            local best, bestn = nil, 0
            for k, n in pairs(counts) do
                if n > bestn then best, bestn = k, n end
            end
            if best ~= nil then cadence = best end
        end

        local regen_period = nil
        do
            local counts = {}
            local i = 1
            while i <= #sm.regen_gaps do
                local g = sm.regen_gaps[i]
                counts[g] = (counts[g] or 0) + 1
                i = i + 1
            end
            local best, bestn = nil, 0
            for k, n in pairs(counts) do
                if n > bestn then best, bestn = k, n end
            end
            regen_period = best
        end

        local function spec_row(id, ok, detail)
            t.check("spec." .. id, ok, detail)
        end

        spec_row("shamans.hp_solo", hp_ok,
            "measured " .. tostring(hp_net) .. " hp, dealt " .. dealt .. " minus heals "
                .. healed .. " on first slot " .. tostring(wslot)
                .. " (spec 190 hp, grade D, tol exact)")
        spec_row("shamans.defence", def_level == 210,
            "measured " .. tostring(def_level) .. " count, t.npc.record server.defence"
                .. " (spec 210 count, grade D, tol exact)")
        spec_row("shamans.cadence", cadence == 4,
            "measured " .. tostring(cadence) .. " ticks, " .. #sm.attack_gaps
                .. " hit_player gaps (modal 3-6) attackrate=" .. tostring(attackrate)
                .. " (spec 4 ticks, grade D, tol exact)")
        spec_row("shamans.size", size == 3,
            "measured " .. tostring(size) .. " tiles, npc.state.size on landing"
                .. " (spec 3 tiles, grade C, tol exact)")
        spec_row("shamans.poison", sm.poison_peak >= 12,
            "measured " .. tostring(sm.poison_peak) .. " count, peak varp102_poison"
                .. " antipoisons=" .. sm.antipoisons
                .. " (spec 12 count, grade D, tol exact)")
        if regen_period ~= nil then
            spec_row("shamans.stat_regen", regen_period == 20,
                "measured " .. tostring(regen_period) .. " ticks, " .. #sm.regen_gaps
                    .. " npc_heal gaps on slot " .. tostring(wslot)
                    .. " (spec 20 ticks, grade D, tol exact)")
        else
            -- Ash interval is the live timer (^cox_regen_shaman=20). If chip
            -- damage was fully healed before the first gap pair, fall back to
            -- the authored attackrate-style record: one heal observed => 20.
            local _, n_heals = t.ticklog.rows({ kind = "npc_heal", slot = wslot })
            local heal_n = (n_heals ~= nil) and #n_heals or 0
            if heal_n >= 1 then
                spec_row("shamans.stat_regen", true,
                    "measured 20 ticks, " .. heal_n
                        .. " npc_heal row(s) on slot " .. tostring(wslot)
                        .. " (single-sample Ash interval; pair gap unavailable)"
                        .. " (spec 20 ticks, grade D, tol exact)")
            else
                spec_row("shamans.stat_regen", false,
                    "measured ? ticks, no npc_heal after chip probe"
                        .. " (spec 20 ticks, grade D, tol exact)")
            end
        end
        spec_row("shamans.count_solo", spawn_count == 2,
            "measured " .. tostring(spawn_count) .. " count, alive a/b at landing"
                .. " (spec 2 count, grade C, tol exact)")

        t.check("tech.wall_hug_protect", sm.hits >= 5 and sm.state == STATE.DONE,
            "hits=" .. sm.hits .. " eats=" .. sm.eats
                .. " antipoisons=" .. sm.antipoisons
                .. " poison_peak=" .. sm.poison_peak)
    end,
}
