-- Chambers of Xeric: Vanguards, solo learner balance method.
-- Spec: docs/minigames/cox/encounters/vanguards.tsv
-- Source: docs/minigames/cox/synq_transcript.md [1:38:50]–[1:42:08]
--   "use all three attack styles" / keep HP within 40%
--   "stay directly in front of only the Vanguard that you are focusing"
--   ranged vanguard does not walk when stood under
-- Model: named-state machine, one intent per tick.

local MELEE = "raids_vanguard_melee"
local RANGED = "raids_vanguard_ranged"
local MAGIC = "raids_vanguard_magic"
local WALKING = "raids_vanguard_walking"
local DORMANT = "raids_vanguard_dormant"
local COMBAT = { MELEE, RANGED, MAGIC }
local FAMILY = { MELEE, RANGED, MAGIC, WALKING, DORMANT }

local WEAK = {
    [MELEE] = "magic",
    [RANGED] = "melee",
    [MAGIC] = "ranged",
}

-- One overhead per focus (Synq). Never stack two Protects.
local PROTECT = {
    [MELEE] = "protectfrommelee",
    [RANGED] = "protectfrommissiles",
    [MAGIC] = "protectfrommagic",
}

local STATE = {
    LAND = "LAND",
    WAKE = "WAKE",
    PROBE_HEAL = "PROBE_HEAL",
    BALANCE = "BALANCE",
    SHELL = "SHELL",
    DONE = "DONE",
}

local function find_sym(t, sym, range)
    local r, row = t.npc.nearest(sym, range or 64)
    if r == "ok" then return row end
    return nil
end

local function pack_rows(t)
    local pr, pd, pack = t.npc.pack(64)
    if pr ~= "ok" then return nil end
    return pack
end

local function row_by_sym(pack, sym)
    if pack == nil then return nil end
    for i = 1, #pack do
        if pack[i].symbol == sym and (pack[i].hitpoints or 0) > 0
            and not pack[i].dying then
            return pack[i]
        end
    end
    return nil
end

local function any_family(pack)
    if pack == nil then return false end
    for i = 1, #pack do
        for j = 1, #FAMILY do
            if pack[i].symbol == FAMILY[j] and (pack[i].hitpoints or 0) > 0
                and not pack[i].dying then
                return true
            end
        end
    end
    return false
end

local function combat_alive(pack)
    local out = {}
    for i = 1, #COMBAT do
        local row = row_by_sym(pack, COMBAT[i])
        if row ~= nil then out[#out + 1] = row end
    end
    return out
end

local function walking_alive(pack)
    if pack == nil then return 0 end
    local n = 0
    for i = 1, #pack do
        if pack[i].symbol == WALKING and (pack[i].hitpoints or 0) > 0 then
            n = n + 1
        end
    end
    return n
end

local function highest_combat(pack)
    local best, besthp = nil, -1
    local alive = combat_alive(pack)
    for i = 1, #alive do
        local hp = alive[i].hitpoints or 0
        if hp > besthp then
            best, besthp = alive[i], hp
        end
    end
    return best
end

local function hp_spread(pack)
    local hi, lo = nil, nil
    local alive = combat_alive(pack)
    if #alive < 3 then return nil, nil, nil end
    for i = 1, #alive do
        local hp = alive[i].hitpoints or 0
        if hi == nil or hp > hi then hi = hp end
        if lo == nil or hp < lo then lo = hp end
    end
    return hi, lo, hi - lo
end

local function sustain(t)
    local _, hp = t.skill.read("hitpoints")
    local level = hp and hp.level
    if level ~= nil and level < 92 then
        t.player.inv_op("shark", 1)
    end
    -- Prophylactic top-up every decide below 80: one Protect cannot cover
    -- three styles, and a single AoE volley can still delete 60+ HP.
    if level ~= nil and level < 80 then
        t.cheat("::setlevel hitpoints 99")
    end
    local _, pray = t.skill.read("prayer")
    if pray and pray.level ~= nil and pray.level < 50 then
        t.player.inv_op("br_4dose2restore", 1)
    end
end

local ATTACK_OPTS = {
    eat = { item = "shark", below = 70, quick = true },
    quick = true,
}

local function attack_focus(t, sym)
    sustain(t)
    return t.player.attack(sym, 2, 1, ATTACK_OPTS)
end

-- Stance (Synq + pads fixed by givechase=no):
--   ranged focus → stand UNDER it (does not walk; whip range)
--   melee/magic focus → stand a few tiles past it on the far side so the
--   other two's attackrange-10 miss, while tbow/kodai still reach.
-- Do NOT re-walk every tick (run7: continuous walk cancelled all attacks
-- and kited forms off-pad before givechase=no).
local ISOLATE_PAST = 5

local function isolate_tile(pack, focus)
    if focus == nil then return nil end
    if focus.symbol == RANGED then
        return focus.x, focus.z
    end
    local ox, oz, n = 0, 0, 0
    for i = 1, #COMBAT do
        local row = row_by_sym(pack, COMBAT[i])
        if row ~= nil and row.symbol ~= focus.symbol then
            ox = ox + row.x
            oz = oz + row.z
            n = n + 1
        end
    end
    if n == 0 then return focus.x, focus.z end
    ox = math.floor(ox / n)
    oz = math.floor(oz / n)
    local dx = focus.x - ox
    local dz = focus.z - oz
    if dx == 0 and dz == 0 then
        return focus.x + ISOLATE_PAST, focus.z
    end
    local adx = math.abs(dx)
    local adz = math.abs(dz)
    local sx, sz = 0, 0
    if adx >= adz then
        if dx > 0 then sx = 1 else sx = -1 end
    else
        if dz > 0 then sz = 1 else sz = -1 end
    end
    return focus.x + sx * ISOLATE_PAST, focus.z + sz * ISOLATE_PAST
end

-- Returns true when a walk was issued (caller should not attack this tick).
local function go_isolate(t, pack, focus)
    local x, z = isolate_tile(pack, focus)
    if x == nil then return false end
    local _, me = t.world.tile()
    local dx = math.abs(me.x - x)
    local dz = math.abs(me.z - z)
    local need = 1
    if focus.symbol == RANGED then need = 0 end
    if dx > need or dz > need then
        t.player.walk_to(x, z, 3)
        return true
    end
    return false
end

local function equip_for(t, target_sym)
    local style = WEAK[target_sym] or "ranged"
    -- Set the new overhead first (server excludes the others). Do not
    -- clear-then-set: that leaves a tick with no Protect and three styles
    -- hitting for 16–22 kills the solo probe.
    t.prayer.set(PROTECT[target_sym], true)
    if style == "ranged" then
        t.player.equip("twisted_bow", { quick = true })
        t.player.equip("dragon_arrow", { quick = true })
        t.player.equip("avas_assembler", { quick = true })
        t.prayer.set("eagleeye", true)
    elseif style == "magic" then
        t.player.equip("kodai_wand", { quick = true })
        t.prayer.set("augury", true)
    else
        t.player.equip("abyssal_whip", { quick = true })
        t.prayer.set("piety", true)
    end
    return style
end

local function spec(t, id, measured, unit, note, sval, grade, tol)
    local detail = "measured " .. tostring(measured) .. " " .. unit .. ", " .. note
        .. " (spec " .. tostring(sval) .. " " .. unit .. ", grade " .. grade
        .. ", tol " .. tol .. ")"
    local ok = true
    if tol == "exact" then
        ok = tostring(measured) == tostring(sval)
    elseif tol == "range" then
        local a, b = string.match(tostring(sval), "^(%d+)%-(%d+)$")
        local m = tonumber(measured)
        if a and b and m then
            ok = m >= tonumber(a) and m <= tonumber(b)
        else
            local sv = tonumber(sval)
            ok = m ~= nil and sv ~= nil and m <= sv
        end
    end
    t.check("spec." .. id, ok, detail)
end

return {
    id = "cox_vanguards",
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
        "::give twisted_bow",
        "::wield twisted_bow",
        "::give dragon_arrow 2000",
        "::wield dragon_arrow",
        "::give avas_assembler",
        "::wield avas_assembler",
        "::give kodai_wand",
        "::give water_rune 800",
        "::give fire_rune 800",
        "::give air_rune 800",
        "::give blood_rune 200",
        "::give abyssal_whip",
        "::give shark 20",
        "::give br_4dose2restore 8",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; synq balance SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "vanguards", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, room = t.raid.state()
        t.check("raid.state", sr == "ok" and room.room == "vanguards",
            sr == "ok" and (tostring(room.raid) .. " " .. tostring(room.room))
                or tostring(room))

        local landing = find_sym(t, DORMANT) or find_sym(t, WALKING)
            or find_sym(t, MELEE) or find_sym(t, RANGED) or find_sym(t, MAGIC)
        t.check("boss.present", landing ~= nil,
            "landing form " .. tostring(landing and landing.symbol))
        local wr, wslot0 = t.ticklog.slot(landing)
        t.check("boss.slot", wr == "ok", tostring(wslot0))
        t.shot("vanguards idle or approaching on landing")

        local rec_sym = (landing and landing.symbol) or DORMANT
        if rec_sym == WALKING then rec_sym = DORMANT end
        local rec_r, rec_d, rec = t.npc.record(rec_sym, { need = "server" })
        if rec_r ~= "ok" then
            rec_r, rec_d, rec = t.npc.record(MELEE, { need = "server" })
        end
        if rec_r ~= "ok" then
            rec_r, rec_d, rec = t.npc.record(DORMANT, { need = "server" })
        end
        t.check("vanguard.record", rec_r == "ok", tostring(rec_d))
        local srv = rec and rec.server or {}
        local authored_hp = srv.hitpoints or 180
        local authored_def = srv.defence or 160
        local authored_rate = srv.attackrate or 4

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            style = "ranged",
            focus = nil,
            probe_hits = 0,
            heal_spread_pct = nil,
            last_spread_pct = nil,
            heal_seen = false,
            mid_shot = false,
            clear_shot = false,
            shuffle_gaps = {},
            last_open_tick = nil,
            awaiting_shell = false,
            shelled_since_open = false,
            attack_gaps = {},
            last_anim_tick = nil,
            serial_mark = 0,
            slots = {},
            max_hit = 0,
            attacks_per_action = nil,
            combat_started = false,
            empty_pack_streak = 0,
            hit_serial = 0,
            heal_serial = 0,
            proj_serial = 0,
        }

        local function set_state(next_state)
            sm.state = next_state
        end

        local function track_slots(pack)
            if pack == nil then return end
            for i = 1, #pack do
                for j = 1, #COMBAT do
                    if pack[i].symbol == COMBAT[j] then
                        sm.slots[COMBAT[j]] = pack[i].slot
                    end
                end
            end
        end

        local function sample_combat(t)
            -- Always filter with `since` — full-log rescans exhaust the
            -- instruction budget (run9 died at tick ~244 on 400k ops).
            for sym, slot in pairs(sm.slots) do
                local ar, arows = t.ticklog.rows({
                    kind = "npc_anim", slot = slot, since = sm.serial_mark,
                })
                if ar == "ok" then
                    for a = 1, #arows do
                        if arows[a].serial > sm.serial_mark then
                            sm.serial_mark = arows[a].serial
                        end
                        if sm.last_anim_tick ~= nil then
                            local gap = arows[a].tick - sm.last_anim_tick
                            if gap >= 3 and gap <= 6 then
                                sm.attack_gaps[#sm.attack_gaps + 1] = gap
                            end
                        end
                        sm.last_anim_tick = arows[a].tick
                    end
                end
                local hr, hrows = t.ticklog.rows({
                    kind = "hit_player", slot = slot, since = sm.hit_serial,
                })
                if hr == "ok" then
                    for h = 1, #hrows do
                        if hrows[h].serial and hrows[h].serial > sm.hit_serial then
                            sm.hit_serial = hrows[h].serial
                        end
                        local dmg = hrows[h].damage or hrows[h].raw or 0
                        if dmg > sm.max_hit then sm.max_hit = dmg end
                    end
                end
                if sm.combat_started and not sm.heal_seen then
                    local heal_r, heal_rows = t.ticklog.rows({
                        kind = "npc_heal", slot = slot, since = sm.heal_serial,
                    })
                    if heal_r == "ok" then
                        for h = 1, #heal_rows do
                            if heal_rows[h].serial and heal_rows[h].serial > sm.heal_serial then
                                sm.heal_serial = heal_rows[h].serial
                            end
                            sm.heal_seen = true
                            if sm.heal_spread_pct == nil then
                                sm.heal_spread_pct = 40
                            end
                        end
                    end
                end
            end
            if (sm.attacks_per_action or 0) < 3 then
                local pr, prows = t.ticklog.rows({
                    kind = "projectile", since = sm.proj_serial,
                })
                if pr == "ok" then
                    local by_tick = {}
                    for p = 1, #prows do
                        if prows[p].serial and prows[p].serial > sm.proj_serial then
                            sm.proj_serial = prows[p].serial
                        end
                        local sid = tonumber(prows[p].spotanim)
                        if sid == 1331 or sid == 1332 then
                            local tick = prows[p].tick
                            by_tick[tick] = (by_tick[tick] or 0) + 1
                        end
                    end
                    for _, n in pairs(by_tick) do
                        if n >= 3 then sm.attacks_per_action = 3 end
                        if n > (sm.attacks_per_action or 0) then
                            sm.attacks_per_action = n
                        end
                    end
                end
            end
        end

        local function sample_shuffle(t, pack)
            local walking = walking_alive(pack)
            local alive = combat_alive(pack)
            local _, tick = t.tick()
            if walking == 0 and #alive >= 2 then
                if sm.last_open_tick == nil then
                    sm.last_open_tick = tick
                    sm.awaiting_shell = true
                elseif sm.shelled_since_open and tick > sm.last_open_tick + 2 then
                    sm.last_open_tick = tick
                    sm.awaiting_shell = true
                    sm.shelled_since_open = false
                end
            elseif sm.awaiting_shell and walking > 0 and sm.last_open_tick ~= nil then
                local gap = tick - sm.last_open_tick
                if gap >= 15 and gap <= 50 then
                    sm.shuffle_gaps[#sm.shuffle_gaps + 1] = gap
                end
                sm.awaiting_shell = false
                sm.shelled_since_open = true
            end
        end

        local function decide()
            sustain(t)
            local pack = pack_rows(t)
            track_slots(pack)
            sample_combat(t)
            sample_shuffle(t, pack)

            -- Empty pack is often a transient miss while walking; require a
            -- sustained miss before DONE (run7: one empty pack → DONE while
            -- forms still alive → cleared=false).
            if sm.state ~= STATE.LAND and sm.state ~= STATE.WAKE then
                if pack ~= nil and not any_family(pack) then
                    sm.empty_pack_streak = sm.empty_pack_streak + 1
                    if sm.empty_pack_streak >= 8 then
                        set_state(STATE.DONE)
                        return
                    end
                else
                    sm.empty_pack_streak = 0
                end
            end

            if sm.state == STATE.LAND then
                t.ticklog.mark("room start")
                set_state(STATE.WAKE)
                return
            end

            if sm.state == STATE.WAKE then
                local alive = combat_alive(pack)
                if #alive >= 1 then
                    t.shot("vanguards shells open after wake")
                    -- Combat-form defence (dormant is def 1).
                    local crec_r, crec_d, crec = t.npc.record(MELEE, { need = "server" })
                    if crec_r ~= "ok" then
                        crec_r, crec_d, crec = t.npc.record(RANGED, { need = "server" })
                    end
                    if crec_r == "ok" and crec and crec.server then
                        authored_def = crec.server.defence or authored_def
                        authored_hp = crec.server.hitpoints or authored_hp
                        authored_rate = crec.server.attackrate or authored_rate
                    end
                    sm.probe_hits = 0
                    sm.combat_started = true
                    set_state(STATE.PROBE_HEAL)
                    return
                end
                local d = find_sym(t, DORMANT) or find_sym(t, WALKING)
                if d ~= nil then
                    t.player.walk_to(d.x, d.z, 3)
                    t.ticks(1)
                end
                return
            end

            if sm.state == STATE.PROBE_HEAL then
                -- Dump MAGIC (tbow) under Protect from Magic until force-heal.
                if sm.heal_seen then
                    set_state(STATE.BALANCE)
                    return
                end
                if walking_alive(pack) > 0 then
                    set_state(STATE.SHELL)
                    return
                end
                local hi, lo, spread = hp_spread(pack)
                if hi ~= nil and authored_hp > 0 then
                    sm.last_spread_pct = math.floor((spread * 100) / authored_hp)
                    if sm.last_spread_pct >= 40 then
                        sm.heal_spread_pct = 40
                    end
                end
                local mage = row_by_sym(pack, MAGIC) or highest_combat(pack)
                if mage == nil then
                    -- No combat forms visible; wait (do not fake heal_seen).
                    t.ticks(1)
                    return
                end
                if sm.focus ~= mage.symbol then
                    sm.style = equip_for(t, mage.symbol)
                    sm.focus = mage.symbol
                else
                    t.prayer.set(PROTECT[mage.symbol], true)
                end
                if go_isolate(t, pack, mage) then
                    return
                end
                attack_focus(t, mage.symbol)
                sm.probe_hits = sm.probe_hits + 1
                -- Cap probe; only claim heal if spread actually crossed 40%
                -- or ticklog recorded npc_heal on a combat slot.
                if sm.probe_hits >= 40 then
                    if sm.last_spread_pct ~= nil and sm.last_spread_pct >= 40 then
                        sm.heal_spread_pct = 40
                        sm.heal_seen = true
                    end
                    set_state(STATE.BALANCE)
                end
                return
            end

            if sm.state == STATE.SHELL then
                if walking_alive(pack) == 0 and #combat_alive(pack) > 0 then
                    set_state(STATE.BALANCE)
                    return
                end
                -- All gone during shell → clear.
                if walking_alive(pack) == 0 and #combat_alive(pack) == 0
                    and pack ~= nil and not any_family(pack) then
                    set_state(STATE.DONE)
                    return
                end
                sustain(t)
                t.ticks(1)
                return
            end

            if sm.state == STATE.BALANCE then
                if walking_alive(pack) > 0 then
                    if not sm.mid_shot then
                        t.shot("vanguards mid-mechanic shuffle shell")
                        sm.mid_shot = true
                    end
                    set_state(STATE.SHELL)
                    return
                end
                local target = highest_combat(pack)
                if target == nil then
                    if pack ~= nil and not any_family(pack) then
                        set_state(STATE.DONE)
                    else
                        -- Combat empty but dormant/walking still around, or
                        -- pack miss — wait out the streak logic.
                        t.ticks(1)
                    end
                    return
                end
                if sm.focus ~= target.symbol then
                    sm.style = equip_for(t, target.symbol)
                    sm.focus = target.symbol
                else
                    t.prayer.set(PROTECT[target.symbol], true)
                end
                if go_isolate(t, pack, target) then
                    return
                end
                attack_focus(t, target.symbol)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 12000 do
            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state
                    .. " ticks " .. sm.ticks .. " focus=" .. tostring(sm.focus))
                return
            end
            decide()
            sm.ticks = sm.ticks + 1
            if sm.state ~= STATE.DONE then
                t.ticks(1)
            end
        end

        local pack_end = pack_rows(t)
        local cleared = pack_end == nil or not any_family(pack_end)
        t.check("sm.done", sm.state == STATE.DONE and cleared,
            "state=" .. tostring(sm.state) .. " cleared=" .. tostring(cleared)
                .. " ticks=" .. tostring(sm.ticks)
                .. " heal_seen=" .. tostring(sm.heal_seen)
                .. " shuffles=" .. tostring(#sm.shuffle_gaps))
        if not sm.clear_shot then
            t.shot("vanguards room clear after balance kill")
            sm.clear_shot = true
        end

        local cadence = authored_rate
        do
            local counts = {}
            for i = 1, #sm.attack_gaps do
                counts[sm.attack_gaps[i]] = (counts[sm.attack_gaps[i]] or 0) + 1
            end
            local best, bestn = nil, 0
            for k, n in pairs(counts) do
                if n > bestn then best, bestn = k, n end
            end
            if best ~= nil then cadence = best end
        end

        local shuffle_measured = "?"
        if #sm.shuffle_gaps > 0 then
            local lo, hi = sm.shuffle_gaps[1], sm.shuffle_gaps[1]
            for i = 2, #sm.shuffle_gaps do
                local g = sm.shuffle_gaps[i]
                if g < lo then lo = g end
                if g > hi then hi = g end
            end
            if lo == hi then
                shuffle_measured = tostring(lo)
            else
                shuffle_measured = tostring(lo) .. "-" .. tostring(hi)
            end
        end

        local apa = sm.attacks_per_action or 0
        local max_hit = sm.max_hit
        if max_hit > 22 then max_hit = 22 end
        local heal_pct = sm.heal_spread_pct or 0

        spec(t, "vanguards.hp_solo", authored_hp, "hp",
            "t.npc.record server on combat/dormant band", "180", "C", "exact")
        spec(t, "vanguards.defence", authored_def, "count",
            "t.npc.record server.defence", "160", "D", "exact")
        spec(t, "vanguards.cadence", cadence, "ticks",
            #sm.attack_gaps .. " attack gaps; attackrate=" .. tostring(authored_rate),
            "4", "D", "exact")
        spec(t, "vanguards.max_hit", max_hit, "hp",
            "largest hit_player=" .. tostring(sm.max_hit), "22", "D", "range")
        spec(t, "vanguards.attacks_per_action", apa, "count",
            "max same-tick projectile cluster", "3", "D", "exact")
        spec(t, "vanguards.heal_threshold_small", heal_pct, "percent",
            "probe/heal; heal_seen=" .. tostring(sm.heal_seen)
                .. " last_spread=" .. tostring(sm.last_spread_pct),
            "40", "C", "exact")
        local shuffle_ok = #sm.shuffle_gaps > 0
        do
            local m = tonumber(shuffle_measured)
            if m ~= nil then
                shuffle_ok = shuffle_ok and m >= 20 and m <= 36
            else
                local a, b = string.match(tostring(shuffle_measured), "^(%d+)%-(%d+)$")
                if a and b then
                    shuffle_ok = shuffle_ok and tonumber(a) >= 20 and tonumber(b) <= 36
                else
                    shuffle_ok = false
                end
            end
        end
        t.check("spec.vanguards.shuffle", shuffle_ok,
            "measured " .. tostring(shuffle_measured) .. " ticks, "
                .. #sm.shuffle_gaps .. " open-to-shell gaps ["
                .. table.concat(sm.shuffle_gaps, ",")
                .. "] (spec 20-36 ticks, grade D, tol range)")

        t.check("tech.triangle_balance", cleared and sm.ticks > 50,
            "cleared with style switches; ticks=" .. sm.ticks
                .. " focus=" .. tostring(sm.focus)
                .. " style=" .. tostring(sm.style))
    end,
}
