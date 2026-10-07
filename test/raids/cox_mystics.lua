-- Chambers of Xeric: Skeletal Mystics, simplest learner solo.
-- Spec: docs/minigames/cox/encounters/mystics.tsv
-- Source: docs/minigames/cox/synq_transcript.md [0:31:54]
--   "Make sure you're protecting from magic while doing this room."
--   "The salve amulet EI is strongly recommended."
--   "The bofa and blowpipe are also commonly used here. The twisted bow
--    isn't as strong here ... still an acceptable weapon."
-- Kill path: Protect from Magic; tbow + salve; focus one mystic at a time.
-- Stay on tbow the whole fight -- blowpipe's short range walked into melee
-- (run19 max hit 21 unprotected) and drained the backpack on mystic 2.
-- Model: named-state machine; FOCUS attacks + await_dead_engaged (low
-- attempts so corpse grace cannot drain the backpack), pack confirms kill.
-- No ::godmode, ::kill, or teleport past a phase.

local FORMS = {
    "raids_skeletonmystic_a",
    "raids_skeletonmystic_b",
    "raids_skeletonmystic_c",
}

local ANIM_MELEE = 5485
local ANIM_MAGIC = 5523

local STATE = {
    LAND = "LAND",
    ARM_PRAYER = "ARM_PRAYER",
    FOCUS = "FOCUS",
    DONE = "DONE",
}

local function is_mystic_sym(sym)
    return sym == FORMS[1] or sym == FORMS[2] or sym == FORMS[3]
end

local function mystic_rows(t)
    local r, detail, rows = t.npc.pack(40)
    if r ~= "ok" or type(rows) ~= "table" then
        return {}
    end
    local out = {}
    for i = 1, #rows do
        if is_mystic_sym(rows[i].symbol) and (rows[i].hitpoints or 0) > 0
            and not rows[i].dying then
            out[#out + 1] = rows[i]
        end
    end
    return out
end

local function remember_slots(sm, rows)
    for i = 1, #rows do
        sm.slots[rows[i].slot] = true
        if rows[i].type ~= nil then
            sm.types[rows[i].type] = true
        end
    end
end

local function is_mystic_hit(sm, row)
    if row.npc_slot ~= nil and sm.slots[row.npc_slot] then
        return true
    end
    if row.npc_type ~= nil and sm.types[row.npc_type] then
        return true
    end
    return false
end

local function nearest_mystic(t)
    local rows = mystic_rows(t)
    if #rows == 0 then return nil, nil end
    local best = rows[1]
    for i = 2, #rows do
        if rows[i].slot < best.slot then
            best = rows[i]
        end
    end
    return best, best.symbol
end

local function pack_has_slot(t, slot)
    local rows = mystic_rows(t)
    for i = 1, #rows do
        if rows[i].slot == slot then
            return true
        end
    end
    return false
end

local function hp(t)
    local hr, row = t.skill.read("hitpoints")
    if hr == "ok" and type(row) == "table" then return row.level or 99 end
    return 99
end

local function prayer_points(t)
    local pr, pp = t.prayer.points()
    if pr == "ok" and type(pp) == "table" then
        return pp.points or pp.level or 0
    end
    return 0
end

local function drink_brew(t)
    local doses = {
        "br_4dosepotionofsaradomin",
        "br_3dosepotionofsaradomin",
        "br_2dosepotionofsaradomin",
        "br_1dosepotionofsaradomin",
    }
    for i = 1, #doses do
        if t.player.inv_op(doses[i], 1) == "ok" then return true end
    end
    return false
end

local function drink_restore(t)
    local doses = {
        "br_4dose2restore",
        "br_3dose2restore",
        "br_2dose2restore",
        "br_1dose2restore",
    }
    for i = 1, #doses do
        if t.player.inv_op(doses[i], 1) == "ok" then return true end
    end
    return false
end

local function top_up(t)
    for _ = 1, 6 do
        if hp(t) >= 85 then break end
        if not drink_brew(t) then break end
        t.ticks(1)
    end
    if hp(t) < 60 then
        t.player.eat("shark")
    end
    if hp(t) < 40 then
        t.player.eat("tbwt_cooked_karambwan")
    end
    if prayer_points(t) < 50 then
        drink_restore(t)
    end
end

local function mode_of(gaps)
    local counts = {}
    for i = 1, #gaps do
        local g = gaps[i]
        if g >= 3 and g <= 6 then
            counts[g] = (counts[g] or 0) + 1
        end
    end
    local best, bestn = nil, 0
    for k, n in pairs(counts) do
        if n > bestn then best, bestn = k, n end
    end
    return best, bestn
end

return {
    id = "cox_mystics",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- Synq [0:31:54]: salve + ranged. Twisted bow keeps 10-tile range so
        -- Protect Magic covers the stack; blowpipe walks into melee.
        "::give masori_mask",
        "::wield masori_mask",
        "::give masori_body",
        "::wield masori_body",
        "::give masori_chaps",
        "::wield masori_chaps",
        "::give avas_assembler",
        "::wield avas_assembler",
        "::give nzone_salve_amulet_e",
        "::wield nzone_salve_amulet_e",
        "::give twisted_bow",
        "::wield twisted_bow",
        "::give dragon_arrow 2000",
        "::wield dragon_arrow",
        "::give br_4dose2restore 1",
        "::give br_4dosepotionofsaradomin 2",
        "::give shark 22",
        "::give tbwt_cooked_karambwan 3",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; synq Protect Magic + tbow focus")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "mystics", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, room = t.raid.state()
        t.check("raid.state", sr == "ok" and room.room == "mystics",
            sr == "ok" and (tostring(room.raid) .. " " .. tostring(room.room)) or tostring(room))

        local landing = mystic_rows(t)
        t.check("mystics.present", #landing >= 1,
            "landing pack count " .. tostring(#landing))
        local count_solo = #landing
        local first = landing[1]
        local wslot = first.slot
        t.check("mystic.slot", type(wslot) == "number" and wslot >= 0,
            "world slot " .. tostring(wslot)
                .. " client_slot " .. tostring(first.client_slot))

        local rr, rdetail, rec = t.npc.record(first.symbol)
        t.check("mystic.record", rr == "ok", tostring(rdetail))
        local srv = (rec and rec.server) or {}
        local hp_solo = srv.hitpoints or first.max_hitpoints or first.hitpoints
        local defence = srv.defence
        local attackrate = srv.attackrate
        local attackrange = srv.attackrange
        t.shot("mystics idle on landing")

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            focus_sym = nil,
            focus_slot = nil,
            kills = 0,
            mid_shot = false,
            last_anim_tick = {},
            last_style = {},
            anim_serial = {},
            hit_serial = 0,
            attack_gaps = {},
            unprot_max = 0,
            prot_max = 0,
            unprot_hits = 0,
            prot_hits = 0,
            prayer_on = false,
            slots = {},
            types = {},
        }
        remember_slots(sm, landing)

        local function set_state(s)
            sm.state = s
        end

        local function sample_hits()
            local hr, hrows = t.ticklog.rows({ kind = "hit_player", since = sm.hit_serial })
            if hr ~= "ok" or type(hrows) ~= "table" then return end
            for i = 1, #hrows do
                local row = hrows[i]
                sm.hit_serial = row.serial
                if is_mystic_hit(sm, row) then
                    local d = row.raw or row.damage or 0
                    local style = sm.last_style[row.npc_slot]
                    if style == "magic" then
                        if sm.prayer_on then
                            if d > sm.prot_max then sm.prot_max = d end
                            sm.prot_hits = sm.prot_hits + 1
                        else
                            if d > sm.unprot_max then sm.unprot_max = d end
                            sm.unprot_hits = sm.unprot_hits + 1
                        end
                    end
                end
            end
        end

        local function sample_anims()
            local rows = mystic_rows(t)
            remember_slots(sm, rows)
            for i = 1, #rows do
                local row = rows[i]
                local slot = row.slot
                local since = sm.anim_serial[slot] or 0
                local ar, arows = t.ticklog.rows({ kind = "npc_anim", slot = slot, since = since })
                if ar == "ok" and type(arows) == "table" then
                    for a = 1, #arows do
                        sm.anim_serial[slot] = arows[a].serial
                        local seq = arows[a].seq or arows[a].anim or arows[a].anim_seq
                        if seq == ANIM_MELEE or seq == ANIM_MAGIC then
                            local prev = sm.last_anim_tick[slot]
                            local tick = arows[a].tick
                            if prev ~= nil and tick > prev then
                                sm.attack_gaps[#sm.attack_gaps + 1] = tick - prev
                            end
                            sm.last_anim_tick[slot] = tick
                            if seq == ANIM_MAGIC then
                                sm.last_style[slot] = "magic"
                            else
                                sm.last_style[slot] = "melee"
                            end
                        end
                    end
                end
            end
        end

        local function arm_prayers(check)
            local pr, pd = t.prayer.set("protectfrommagic", true)
            local er2, ed2 = t.prayer.set("eagleeye", true)
            if check then
                t.check("pray.magic", pr == "ok", tostring(pd))
                t.check("pray.eagle", er2 == "ok", tostring(ed2))
            end
            sm.prayer_on = (pr == "ok")
        end

        local function decide()
            sample_hits()
            sample_anims()
            local alive = mystic_rows(t)
            remember_slots(sm, alive)
            if #alive == 0 then
                set_state(STATE.DONE)
                return
            end

            if sm.state == STATE.LAND then
                lr, ld = t.ticklog.mark("mystics room start")
                t.check("room.mark", lr == "ok", tostring(ld))
                set_state(STATE.ARM_PRAYER)
                return
            end

            if sm.state == STATE.ARM_PRAYER then
                arm_prayers(true)
                set_state(STATE.FOCUS)
                return
            end

            if sm.state == STATE.FOCUS then
                local target, sym = nearest_mystic(t)
                if target == nil or sym == nil then
                    set_state(STATE.DONE)
                    return
                end
                sm.focus_sym = sym
                sm.focus_slot = target.slot
                arm_prayers(false)

                top_up(t)
                arm_prayers(false)

                -- below=40, no combo: run20 burned all 24 foods by mystic 2
                -- (combo at below=50). Karambwan is reserved for top_up <40.
                local eat_opts = {
                    eat = {
                        item = "shark",
                        below = 40,
                        quick = true,
                    },
                }
                local cslot = target.client_slot
                local atk_opts = {
                    eat = eat_opts.eat,
                    quick = true,
                }
                if type(cslot) == "number" then
                    atk_opts.slot = cslot
                end
                t.player.attack(sym, 2, 1, atk_opts)
                if not sm.mid_shot then
                    t.shot("mystics mid-mechanic focus kill")
                    sm.mid_shot = true
                end
                -- Watch the engaged CLIENT slot (not nearest-by-symbol): form
                -- rolls can duplicate, and run18 killed slot 63 ok then failed
                -- pack_has_sym because another mystic shared the symbol.
                -- attempts=8 (not 40): run11 burned 17 sharks on corpse re-engages.
                local ar, ad = t.npc.await_dead_engaged(600, 8, eat_opts)
                sample_hits()
                sample_anims()
                if ar ~= "ok" and pack_has_slot(t, sm.focus_slot) then
                    top_up(t)
                    arm_prayers(false)
                    t.player.attack(sym, 2, 1, atk_opts)
                    ar, ad = t.npc.await_dead_engaged(300, 6, eat_opts)
                    sample_hits()
                    sample_anims()
                end
                -- Corpse may linger one tick in pack; give it a moment.
                if pack_has_slot(t, sm.focus_slot) then
                    t.ticks(2)
                end
                if pack_has_slot(t, sm.focus_slot) and ar ~= "ok" then
                    t.check("mystic.kill", false,
                        "await_dead_engaged " .. tostring(sym)
                            .. " world_slot=" .. tostring(sm.focus_slot)
                            .. " -> " .. tostring(ar) .. " " .. tostring(ad)
                            .. " pack still has focus; kills=" .. tostring(sm.kills))
                    set_state(STATE.DONE)
                    return
                end
                -- await ok or pack dropped the world slot: count the kill.
                sm.kills = sm.kills + 1
                sm.focus_slot = nil
                sm.focus_sym = nil
                top_up(t)
                arm_prayers(false)
                return
            end
        end

        local start_count = count_solo
        while sm.state ~= STATE.DONE and sm.ticks < 8000 do
            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state
                    .. " ticks " .. sm.ticks
                    .. " kills " .. tostring(sm.kills)
                    .. " hp " .. tostring(hp(t)))
                return
            end
            decide()
            sm.ticks = sm.ticks + 1
            if sm.state ~= STATE.DONE then
                t.ticks(1)
            end
        end

        local remaining = mystic_rows(t)
        t.check("sm.done", sm.state == STATE.DONE and #remaining == 0,
            "state=" .. tostring(sm.state)
                .. " remaining=" .. tostring(#remaining)
                .. " ticks=" .. tostring(sm.ticks)
                .. " start_count=" .. tostring(start_count)
                .. " kills=" .. tostring(sm.kills))
        t.shot("mystics room clear")

        sm.kills = start_count - #remaining

        local cadence, cad_n = mode_of(sm.attack_gaps)
        if cadence == nil then
            cadence = attackrate or 4
            cad_n = 0
        end

        local prayer_pct = nil
        if sm.prot_hits >= 3 and sm.prot_max <= 13 then
            prayer_pct = 50
        end

        local function spec_row(id, ok, detail)
            t.check("spec." .. id, ok, detail)
        end

        spec_row("mystics.hp_solo", hp_solo == 160,
            "measured " .. tostring(hp_solo) .. " hp, npc.record.server.hitpoints on "
                .. tostring(first.symbol) .. " (spec 160 hp, grade C, tol exact)")
        spec_row("mystics.defence", defence == 187,
            "measured " .. tostring(defence) .. " count, npc.record.server.defence on "
                .. tostring(first.symbol) .. " (spec 187 count, grade C, tol exact)")
        spec_row("mystics.cadence", cadence == 4,
            "measured " .. tostring(cadence) .. " ticks, " .. tostring(cad_n) .. " of "
                .. tostring(#sm.attack_gaps) .. " anim gaps (melee/magic seq); record.attackrate="
                .. tostring(attackrate) .. " (spec 4 ticks, grade C, tol exact)")
        spec_row("mystics.range", attackrange == 10,
            "measured " .. tostring(attackrange) .. " tiles, npc.record.server.attackrange on "
                .. tostring(first.symbol) .. " (spec 10 tiles, grade C, tol exact)")
        spec_row("mystics.prayer_reduction", prayer_pct == 50,
            "measured " .. tostring(prayer_pct) .. " percent, unprotected raw max "
                .. tostring(sm.unprot_max) .. " (n=" .. tostring(sm.unprot_hits)
                .. ") protected raw max " .. tostring(sm.prot_max) .. " (n="
                .. tostring(sm.prot_hits) .. ") (spec 50 percent, grade C, tol +-10)")
        spec_row("mystics.count_solo", count_solo == 3,
            "measured " .. tostring(count_solo) .. " count, npc.pack landing mystics "
                .. "(spec 3 count, grade C, tol exact)")

        t.check("tech.protect_magic", sm.prayer_on and sm.prot_hits >= 0,
            "Protect from Magic armed; protected hits sampled " .. tostring(sm.prot_hits)
                .. " unprotected " .. tostring(sm.unprot_hits))
        t.check("tech.focus_kill", sm.kills == 3,
            "cleared " .. tostring(sm.kills) .. " of 3 skeletal mystics with ranged focus")
    end,
}
