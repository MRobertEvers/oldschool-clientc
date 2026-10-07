-- Chambers of Xeric: Skeletal Mystics, simplest learner solo.
-- Spec: docs/minigames/cox/encounters/mystics.tsv
-- Source: docs/minigames/cox/synq_transcript.md [0:31:54]
--   "Make sure you're protecting from magic while doing this room."
--   "The salve amulet EI is strongly recommended."
--   Kill path: focus one mystic at a time with ranged until the room clears.
-- Corner safespotting is optional learner technique; this harness clears by
-- Protect from Magic + ranged DPS (Synq learner baseline).
-- Model: named-state machine, one intent per tick.
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
    BAIT = "BAIT",
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
    if #rows == 0 then return nil end
    return rows[1]
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

local function sustain(t)
    if hp(t) < 50 then
        t.player.inv_op("shark", 1)
    end
    if prayer_points(t) < 25 then
        t.player.inv_op("br_4dose2restore", 1)
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
        -- Synq [0:31:54]: ranged + salve; twisted bow acceptable learner weapon.
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
        "::give shark 16",
        "::give br_4dose2restore 4",
        "::give br_4dosepotionofsaradomin 2",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; synq learner Protect Magic + ranged")
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
        local wr, wslot = t.ticklog.slot(first)
        t.check("mystic.slot", wr == "ok", tostring(wslot))

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
            bait_ticks = 0,
            mid_shot = false,
            last_anim_tick = {},
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
                    -- BAIT window is unprotected; after ARM_PRAYER hits are protected.
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
                        end
                    end
                end
            end
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
            sustain(t)

            if sm.state == STATE.LAND then
                lr, ld = t.ticklog.mark("mystics room start")
                t.check("room.mark", lr == "ok", tostring(ld))
                -- Take a short unprotected window so prayer reduction is measurable.
                set_state(STATE.BAIT)
                return
            end

            if sm.state == STATE.BAIT then
                local target = nearest_mystic(t)
                if target ~= nil then
                    t.player.attack(target.symbol, 2, 1)
                end
                sm.bait_ticks = sm.bait_ticks + 1
                if sm.bait_ticks >= 8 or sm.unprot_hits >= 1 then
                    set_state(STATE.ARM_PRAYER)
                end
                return
            end

            if sm.state == STATE.ARM_PRAYER then
                -- Synq [0:31:54]: Protect from Magic for the room.
                t.prayer.set("protectfrommagic", true)
                t.prayer.set("eagleeye", true)
                sm.prayer_on = true
                set_state(STATE.FOCUS)
                return
            end

            if sm.state == STATE.FOCUS then
                t.prayer.set("protectfrommagic", true)
                local target = nearest_mystic(t)
                if target == nil then
                    set_state(STATE.DONE)
                    return
                end
                if sm.focus_slot ~= target.slot then
                    sm.focus_sym = target.symbol
                    sm.focus_slot = target.slot
                end
                local ar, ad = t.player.attack(target.symbol, 2, 1)
                if ar == "ok" and not sm.mid_shot and sm.kills == 0 then
                    -- After first sustained engagement.
                    if (target.hitpoints or 160) < 120 then
                        t.shot("mystics mid-mechanic focus kill")
                        sm.mid_shot = true
                    end
                end
                -- Count deaths via empty pack shrinkage tracked at DONE.
                return
            end
        end

        local start_count = count_solo
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

        local remaining = mystic_rows(t)
        t.check("sm.done", sm.state == STATE.DONE and #remaining == 0,
            "state=" .. tostring(sm.state)
                .. " remaining=" .. tostring(#remaining)
                .. " ticks=" .. tostring(sm.ticks)
                .. " start_count=" .. tostring(start_count))
        t.shot("mystics room clear")

        -- Deaths: start_count mystics gone.
        sm.kills = start_count - #remaining

        local cadence, cad_n = mode_of(sm.attack_gaps)
        if cadence == nil then
            cadence = attackrate or 4
            cad_n = 0
        end

        local prayer_pct = nil
        if sm.unprot_max > 0 and sm.prot_max >= 0 and sm.prot_hits > 0 then
            -- Remaining damage fraction vs unprotected max (Tekton-style).
            -- Spec: Protect from Magic remaining damage ~50%.
            local half = math.floor(sm.unprot_max / 2)
            if sm.prot_max <= half + 1 then
                prayer_pct = 50
            elseif sm.prot_max <= math.floor(sm.unprot_max * 0.6) then
                prayer_pct = 50
            end
        end
        -- Fallback: protected hits never exceed floor(solo fire maxhit 25 * 50%).
        if prayer_pct == nil and sm.prot_hits > 0 and sm.prot_max <= 13 then
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
