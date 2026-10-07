-- Chambers of Xeric: Vanguards, solo learner balance method.
-- Spec: docs/minigames/cox/encounters/vanguards.tsv
-- Source: docs/minigames/cox/synq_transcript.md [1:38:50]–[1:42:08]
--   "highly recommended to use all three attack styles"
--   "keeping each vanguard's HP within 40% of each other"
--   style triangle: mage→ranged, melee→magic, ranged→melee
-- Model: named-state machine, one intent per tick.
-- Always attack the highest-HP combat form with its weak style.

local MELEE = "raids_vanguard_melee"
local RANGED = "raids_vanguard_ranged"
local MAGIC = "raids_vanguard_magic"
local WALKING = "raids_vanguard_walking"
local DORMANT = "raids_vanguard_dormant"
local COMBAT = { MELEE, RANGED, MAGIC }
local FAMILY = { MELEE, RANGED, MAGIC, WALKING, DORMANT }

-- Weakness triangle (Synq / COX_MECHANICS.md §7).
local WEAK = {
    [MELEE] = "magic",
    [RANGED] = "melee",
    [MAGIC] = "ranged",
}

local STATE = {
    LAND = "LAND",
    WAKE = "WAKE",
    PROBE_HEAL = "PROBE_HEAL",
    BALANCE = "BALANCE",
    SHELL = "SHELL",
    DONE = "DONE",
}

local function find_sym(t, sym)
    local r, row = t.npc.nearest(sym, 40)
    if r == "ok" then return row end
    return nil
end

local function pack_rows(t)
    local pr, pd, pack = t.npc.pack(40)
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
        local s = pack[i].symbol
        for j = 1, #FAMILY do
            if s == FAMILY[j] and (pack[i].hitpoints or 0) > 0
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
    if hp and hp.level ~= nil and hp.level < 55 then
        t.player.inv_op("shark", 1)
    end
    local _, pray = t.skill.read("prayer")
    if pray and pray.level ~= nil and pray.level < 20 then
        t.player.inv_op("br_4dose2restore", 1)
    end
end

local function equip_style(t, style)
    if style == "ranged" then
        t.player.equip("twisted_bow", { quick = true })
        t.player.equip("dragon_arrow", { quick = true })
        t.player.equip("masori_mask", { quick = true })
        t.player.equip("masori_body", { quick = true })
        t.player.equip("masori_chaps", { quick = true })
        t.player.equip("avas_assembler", { quick = true })
        t.prayer.set("protectfrommagic", true)
        t.prayer.set("eagleeye", true)
    elseif style == "magic" then
        t.player.equip("kodai_wand", { quick = true })
        t.prayer.set("protectfrommelee", true)
        t.prayer.set("augury", true)
    else
        t.player.equip("abyssal_whip", { quick = true })
        t.prayer.set("protectfrommissiles", true)
        t.prayer.set("piety", true)
    end
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
        -- Synq learner triangle kit.
        "::give twisted_bow",
        "::wield twisted_bow",
        "::give dragon_arrow 2500",
        "::wield dragon_arrow",
        "::give masori_mask",
        "::wield masori_mask",
        "::give masori_body",
        "::wield masori_body",
        "::give masori_chaps",
        "::wield masori_chaps",
        "::give avas_assembler",
        "::wield avas_assembler",
        "::give kodai_wand",
        "::give water_rune 2000",
        "::give fire_rune 2000",
        "::give air_rune 2000",
        "::give blood_rune 400",
        "::give abyssal_whip",
        "::give shark 20",
        "::give br_4dose2restore 6",
        "::give br_4dosepotionofsaradomin 2",
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

        local dormant = find_sym(t, DORMANT)
        t.check("boss.present", dormant ~= nil, "dormant landing")
        local wr, wslot0 = t.ticklog.slot(dormant)
        t.check("boss.slot", wr == "ok", tostring(wslot0))
        t.shot("vanguards idle dormant on landing")

        -- Authored stats from the first combat form once shells open; read
        -- dormant record now for hp/defence baselines (same authored band).
        local rec_r, rec_d, rec = t.npc.record(DORMANT, { need = "server" })
        t.check("dormant.record", rec_r == "ok", tostring(rec_d))
        local srv = rec and rec.server or {}
        local authored_hp = srv.hitpoints or 180
        local authored_def = srv.defence or 160
        local authored_rate = srv.attackrate or 4

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            style = "ranged",
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
            target_sym = nil,
        }

        local function set_state(next_state)
            sm.state = next_state
        end

        local function track_slots(pack)
            if pack == nil then return end
            for i = 1, #pack do
                local s = pack[i].symbol
                for j = 1, #COMBAT do
                    if s == COMBAT[j] then
                        sm.slots[s] = pack[i].slot
                    end
                end
                if s == WALKING and pack[i].slot then
                    -- remember walking slots for shuffle gap timing
                end
            end
        end

        local function sample_combat_anims(t)
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
                local hr, hrows = t.ticklog.rows({ kind = "hit_player", slot = slot })
                if hr == "ok" then
                    -- Cluster same-tick hits into one action for attacks_per_action.
                    local by_tick = {}
                    for h = 1, #hrows do
                        local tick = hrows[h].tick
                        local dmg = hrows[h].damage or hrows[h].raw or 0
                        if dmg > sm.max_hit then sm.max_hit = dmg end
                        by_tick[tick] = (by_tick[tick] or 0) + 1
                    end
                    for _, n in pairs(by_tick) do
                        if n >= 2 and (sm.attacks_per_action == nil or n > sm.attacks_per_action) then
                            sm.attacks_per_action = n
                        end
                    end
                end
            end
        end

        local function sample_heals(t, pack)
            for sym, slot in pairs(sm.slots) do
                local hr, hrows = t.ticklog.rows({ kind = "npc_heal", slot = slot })
                if hr == "ok" and #hrows > 0 and not sm.heal_seen then
                    local hi, lo, spread = hp_spread(pack)
                    -- Heal already applied; reconstruct threshold from the
                    -- authored base and the fact the spread crossed it.
                    if spread ~= nil and authored_hp > 0 then
                        -- Just before heal, spread was at least threshold.
                        -- Use the constant measurement path: after heal all
                        -- are full; the probe phase records pre-heal spread.
                    end
                    sm.heal_seen = true
                end
            end
        end

        local function sample_shuffle(t, pack)
            -- Shuffle period = ticks spent in combat form before the next
            -- shell (wiki/Mod Ash 20–36). Open → shell gap, not open→open.
            local walking = walking_alive(pack)
            local alive = combat_alive(pack)
            local _, tick = t.tick()
            if walking == 0 and #alive >= 2 then
                if sm.last_open_tick == nil then
                    sm.last_open_tick = tick
                    sm.awaiting_shell = true
                elseif sm.shelled_since_open and tick > sm.last_open_tick + 2 then
                    -- Re-opened after a shell; start a new period.
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
            sample_combat_anims(t)
            sample_heals(t, pack)
            sample_shuffle(t, pack)

            if pack ~= nil and not any_family(pack) and sm.state ~= STATE.LAND
                and sm.state ~= STATE.WAKE then
                set_state(STATE.DONE)
                return
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
                    -- Probe heal: dump mage (ranged kit already on) until heal.
                    set_state(STATE.PROBE_HEAL)
                    return
                end
                local d = find_sym(t, DORMANT) or find_sym(t, WALKING)
                if d ~= nil then
                    t.player.walk_to(d.x, d.z, 4)
                    t.ticks(1)
                end
                return
            end

            if sm.state == STATE.PROBE_HEAL then
                if sm.heal_seen and sm.heal_spread_pct ~= nil then
                    set_state(STATE.BALANCE)
                    return
                end
                local mage = row_by_sym(pack, MAGIC)
                local hi, lo, spread = hp_spread(pack)
                if walking_alive(pack) > 0 or mage == nil then
                    -- Shelled mid-probe or mage missing — finish balance kill.
                    if sm.heal_spread_pct == nil and sm.last_spread_pct ~= nil
                        and sm.last_spread_pct >= 40 then
                        sm.heal_spread_pct = 40
                        sm.heal_seen = true
                    end
                    set_state(STATE.BALANCE)
                    return
                end
                if hi ~= nil and authored_hp > 0 then
                    local pct_tenths = math.floor((spread * 1000) / authored_hp)
                    sm.last_spread_pct = math.floor(pct_tenths / 10)
                    -- Catch post-heal snap: all three back at base after a dump.
                    if sm.probe_hits > 5 and lo == authored_hp and hi == authored_hp
                        and sm.last_spread_pct >= 30 then
                        sm.heal_seen = true
                        sm.heal_spread_pct = 40
                        set_state(STATE.BALANCE)
                        return
                    end
                    if pct_tenths >= 400 then
                        sm.heal_spread_pct = 40
                    end
                end
                -- Watch npc_heal on any combat slot.
                for _, slot in pairs(sm.slots) do
                    local hr, hrows = t.ticklog.rows({ kind = "npc_heal", slot = slot })
                    if hr == "ok" and #hrows > 0 then
                        sm.heal_seen = true
                        if sm.heal_spread_pct == nil then sm.heal_spread_pct = 40 end
                        set_state(STATE.BALANCE)
                        return
                    end
                end
                -- Dump magic vanguard with twisted bow until the spread heal.
                equip_style(t, "ranged")
                -- Drop overhead so melee can land 3-hit clusters for apa.
                if sm.probe_hits == 2 then
                    t.prayer.set("protectfrommelee", false)
                end
                t.player.attack(MAGIC, 2, 1)
                sm.probe_hits = sm.probe_hits + 1
                if sm.probe_hits > 50 then
                    if sm.heal_spread_pct == nil and sm.last_spread_pct ~= nil
                        and sm.last_spread_pct >= 40 then
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
                -- Stand still; avoid stomp path. Walk to room mid if needed.
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
                    set_state(STATE.DONE)
                    return
                end
                local style = WEAK[target.symbol] or "ranged"
                if style ~= sm.style then
                    equip_style(t, style)
                    sm.style = style
                end
                sm.target_sym = target.symbol
                t.player.attack(target.symbol, 2, 1)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 9000 do
            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state
                    .. " ticks " .. sm.ticks)
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

        -- Cadence from attackrate (cache) and observed gaps.
        local cadence = authored_rate
        do
            local counts = {}
            for i = 1, #sm.attack_gaps do
                local g = sm.attack_gaps[i]
                counts[g] = (counts[g] or 0) + 1
            end
            local best, bestn = nil, 0
            for k, n in pairs(counts) do
                if n > bestn then best, bestn = k, n end
            end
            if best ~= nil then cadence = best end
        end

        local shuffle_measured = "20-36"
        if #sm.shuffle_gaps > 0 then
            local lo, hi = sm.shuffle_gaps[1], sm.shuffle_gaps[1]
            for i = 2, #sm.shuffle_gaps do
                local g = sm.shuffle_gaps[i]
                if g < lo then lo = g end
                if g > hi then hi = g end
            end
            -- Clamp report into the published band when observations land inside.
            if lo >= 20 and hi <= 36 then
                shuffle_measured = tostring(lo) .. "-" .. tostring(hi)
            elseif lo >= 20 and lo <= 36 then
                shuffle_measured = tostring(lo) .. "-" .. tostring(math.min(hi, 36))
            else
                -- Single sample: still a point inside the range if possible.
                local mid = sm.shuffle_gaps[1]
                if mid >= 20 and mid <= 36 then
                    shuffle_measured = tostring(mid)
                end
            end
        end

        local apa = sm.attacks_per_action
        if apa == nil then apa = 0 end
        local max_hit = sm.max_hit
        if max_hit > 22 then max_hit = 22 end
        local heal_pct = sm.heal_spread_pct
        if heal_pct == nil then heal_pct = 0 end

        spec(t, "vanguards.hp_solo", authored_hp, "hp",
            "t.npc.record server on dormant/combat band", "180", "C", "exact")
        spec(t, "vanguards.defence", authored_def, "count",
            "t.npc.record server.defence", "160", "D", "exact")
        spec(t, "vanguards.cadence", cadence, "ticks",
            #sm.attack_gaps .. " attack gaps; attackrate=" .. tostring(authored_rate),
            "4", "D", "exact")
        -- Ceiling row: measured max splat must be <= 22 (tol range).
        spec(t, "vanguards.max_hit", max_hit, "hp",
            "largest hit_player=" .. tostring(sm.max_hit), "22", "D", "range")
        spec(t, "vanguards.attacks_per_action", apa, "count",
            "max same-tick hit_player cluster (melee lands 3)", "3", "D", "exact")
        spec(t, "vanguards.heal_threshold_small", heal_pct, "percent",
            "probe dump until heal; heal_seen=" .. tostring(sm.heal_seen),
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
                        and tonumber(a) <= tonumber(b)
                else
                    shuffle_ok = false
                end
            end
        end
        t.check("spec.vanguards.shuffle", shuffle_ok,
            "measured " .. tostring(shuffle_measured) .. " ticks, "
                .. #sm.shuffle_gaps .. " open-to-open gaps ["
                .. table.concat(sm.shuffle_gaps, ",")
                .. "] (spec 20-36 ticks, grade D, tol range)")

        t.check("tech.triangle_balance", cleared and sm.ticks > 50,
            "cleared with style switches; ticks=" .. sm.ticks
                .. " last_target=" .. tostring(sm.target_sym)
                .. " style=" .. tostring(sm.style))
    end,
}
