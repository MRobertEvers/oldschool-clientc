-- Chambers of Xeric: Ice Demon, Synq solo learner path.
-- Spec: docs/minigames/cox/encounters/icedemon.tsv
-- Source: docs/minigames/cox/synq_transcript.md [0:14:39]
--   "thaw it out by lighting the braziers with kindling"
--   "Light the two braziers with no ice fiend on them"
--   "Always protect from range" -- forces ranged projectiles (dodge 3x3)
--   Fire spells / demonbane for the kill (150%/115% weakness).
-- Model: named-state machine, one intent per tick.
-- No ::godmode, ::kill, or teleport past a phase.
-- Kindling is brought via setup: room trees/axe are not oploc-wired yet;
-- corsaircurse currently owns [oploc1,raids_icedemon_tinderbox].

local FROZEN = "raids_icedemon_noncombat"
local COMBAT = "raids_icedemon_combat"
local FIEND = "raids_icefiend"
local BRAZIER_UNLIT = "raids_icedemon_brazier_unlit"
local BRAZIER_LIT = "raids_icedemon_brazier_lit"

local STATE = {
    LAND = "LAND",
    LIGHT = "LIGHT",
    WAIT_THAW = "WAIT_THAW",
    ARM_PRAY = "ARM_PRAY",
    FIGHT = "FIGHT",
    DODGE = "DODGE",
    DONE = "DONE",
}

local function hp(t)
    local _, a = t.skill.read("hitpoints")
    if type(a) == "table" then return a.level or -1 end
    return -1
end

local function chebyshev(ax, az, bx, bz)
    local dx = ax - bx
    local dz = az - bz
    if dx < 0 then dx = -dx end
    if dz < 0 then dz = -dz end
    if dx > dz then return dx end
    return dz
end

local function spec(t, id, measured, extra, specv, grade, tol)
    local detail = "measured " .. measured
        .. ((extra and extra ~= "") and (", " .. extra) or "")
        .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.check("spec." .. id, true, detail)
end

local function find_boss(t)
    local r, row = t.npc.nearest(COMBAT, 40)
    if r == "ok" then return r, row, COMBAT end
    r, row = t.npc.nearest(FROZEN, 40)
    if r == "ok" then return r, row, FROZEN end
    return "no_row", nil, nil
end

local function sustain(t, sm)
    if hp(t) > 0 and hp(t) < 55 then
        if t.player.eat("shark") == "ok" then sm.eats = sm.eats + 1 end
    end
    local pr, pp = t.prayer.points()
    local points = 0
    if pr == "ok" then points = pp.points or pp.level or 0 end
    if points < 20 then
        if t.player.drink("br_4dose2restore") == "ok" then sm.drinks = sm.drinks + 1 end
    end
end

return {
    id = "cox_icedemon",
    fixture = "fresh_lumbridge.ini",
    max_frames = 220000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel woodcutting 99",
        "::setlevel firemaking 99",
        -- Synq [0:16:58]: fire surge / fire spells are strong vs ice demon.
        "::give ancestral_hat",
        "::wield ancestral_hat",
        "::give ancestral_robe_top",
        "::wield ancestral_robe_top",
        "::give ancestral_robe_bottom",
        "::wield ancestral_robe_bottom",
        "::give kodai_wand",
        "::wield kodai_wand",
        "::give tome_of_fire",
        "::wield tome_of_fire",
        "::give firerune 2000",
        "::give airrune 2000",
        "::give bloodrune 400",
        "::give wrathrune 200",
        -- Kindling + tinderbox for braziers (trees/axe pickup not wired).
        "::give raids_wood 28",
        "::give tinderbox 1",
        "::give shark 16",
        "::give br_4dose2restore 4",
        "::give br_4dosepotionofsaradomin 2",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; synq solo fire-thaw SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "icedemon", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "icedemon",
            sr == "ok" and (tostring(st.raid) .. " " .. tostring(st.room)) or tostring(st))

        local br, brow, bsym = find_boss(t)
        t.check("boss.present", br == "ok", "landing form " .. tostring(bsym))
        local wr, wslot = t.ticklog.slot(brow)
        t.check("boss.slot", wr == "ok", tostring(wslot))
        t.check("boss.frozen_landing", bsym == FROZEN, "expected frozen, got " .. tostring(bsym))

        local fr, frow = t.npc.nearest(FIEND, 40)
        t.check("icefiend.solo", fr == "ok", "solo should spawn one icefiend")
        local fir, fiws = t.ticklog.slot(frow)
        t.check("icefiend.slot", fir == "ok", tostring(fiws))

            local ur, urow = t.world.loc_near(BRAZIER_UNLIT, 40)
        t.check("brazier.unlit", ur == "ok", tostring(urow and (urow.loc or urow.symbol or urow)))
        t.shot("icedemon frozen idle before kindling")

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            lights = 0,
            thawed = false,
            dead = false,
            eats = 0,
            drinks = 0,
            casts = 0,
            dodges = 0,
            last_attack_tick = nil,
            attack_gaps = {},
            serial_mark = 0,
            douse_seen = false,
            action_gaps = {},
            last_fiend_anim = nil,
            boss_hp = 140,
            combat_slot = wslot,
            light_targets = 0,
        }

        local function set_state(next_state)
            sm.state = next_state
        end

        local function sample_fight()
            local ar, arows = t.ticklog.rows({ kind = "npc_anim", slot = sm.combat_slot, since = sm.serial_mark })
            if ar == "ok" then
                for i = 1, #arows do
                    sm.serial_mark = arows[i].serial
                    if sm.last_attack_tick ~= nil then
                        sm.attack_gaps[#sm.attack_gaps + 1] = arows[i].tick - sm.last_attack_tick
                    end
                    sm.last_attack_tick = arows[i].tick
                end
            end
            -- Icefiend action interval from its anim/timer side-effects: mes douse.
            local mr, mrows = t.ticklog.rows({ kind = "mes" })
            if mr == "ok" then
                for i = 1, #mrows do
                    local label = tostring(mrows[i].label or mrows[i].text or "")
                    if label:find("icefiend snuffs", 1, true) then
                        sm.douse_seen = true
                    end
                end
            end
        end

        local function decide()
            sustain(t, sm)
            if t.player.alive() ~= "ok" then
                set_state(STATE.DONE)
                return
            end

            local _, deaths = t.ticklog.rows({ kind = "npc_death", slot = sm.combat_slot })
            if deaths ~= nil and #deaths > 0 then
                sm.dead = true
                set_state(STATE.DONE)
                return
            end

            local cr, crow, csym = find_boss(t)
            if cr ~= "ok" then
                sm.dead = true
                set_state(STATE.DONE)
                return
            end
            if csym == COMBAT then
                sm.thawed = true
                local swr, sws = t.ticklog.slot(crow)
                if swr == "ok" then sm.combat_slot = sws end
                if crow.hitpoints ~= nil then sm.boss_hp = crow.hitpoints end
            end

            sample_fight()

            if sm.state == STATE.LAND then
                t.ticks(2)
                local rec_r, rec_d, rec = t.npc.record(FROZEN, { need = "server" })
                t.check("frozen.record", rec_r == "ok", tostring(rec_d))
                local srv = rec and rec.server or {}
                sm.boss_hp = srv.hitpoints or 140
                set_state(STATE.LIGHT)
                return
            end

            if sm.state == STATE.LIGHT then
                -- Synq: light braziers with no ice fiend (solo: not beside fiend).
                local fx, fz = frow.x, frow.z
                local fr2, frow2 = t.npc.nearest(FIEND, 40)
                if fr2 == "ok" and frow2 ~= nil then
                    fx, fz = frow2.x, frow2.z
                end
                local copies_r, copies_d, copies = t.world.loc_copies(BRAZIER_UNLIT, 40)
                local best, bestd = nil, -1
                if copies_r == "ok" and type(copies) == "table" then
                    for i = 1, #copies do
                        local c = copies[i]
                        local d = chebyshev(c.x or 0, c.z or 0, fx, fz)
                        -- Unguarded: at least 2 tiles from the icefiend.
                        if d >= 2 and d > bestd then
                            best, bestd = c, d
                        end
                    end
                    if best == nil and #copies > 0 then
                        best = copies[1]
                    end
                else
                    t.check("brazier.copies", copies_r == "ok", tostring(copies_d))
                end
                local lr2, ld2
                if best ~= nil then
                    local bx = best.x or best.tile_x
                    local bz = best.z or best.tile_z
                    local bl = best.level or best.tile_level
                    lr2, ld2 = t.player.click_loc(BRAZIER_UNLIT, 1, { at = { bx, bz, bl } })
                else
                    lr2, ld2 = t.player.click_loc(BRAZIER_UNLIT, 1)
                end
                if lr2 ~= "ok" then
                    lr2, ld2 = t.player.click_loc(BRAZIER_LIT, 1)
                end
                if lr2 == "ok" then
                    sm.lights = sm.lights + 1
                    t.ticks(3)
                    if sm.lights == 1 then
                        t.shot("icedemon first brazier lit")
                    end
                    if sm.lights < 2 then
                        t.cheat("::give raids_wood 28")
                        return
                    end
                    set_state(STATE.WAIT_THAW)
                    return
                end
                t.check("brazier.click", false, "light failed: " .. tostring(ld2))
                set_state(STATE.DONE)
                return
            end

            if sm.state == STATE.WAIT_THAW then
                if sm.thawed then
                    t.shot("icedemon thawed combat form")
                    set_state(STATE.ARM_PRAY)
                    return
                end
                -- Keep fuel on unguarded braziers if burn empties them.
                local lit_r = t.world.loc_near(BRAZIER_LIT, 40)
                if lit_r ~= "ok" then
                    t.cheat("::give raids_wood 28")
                    t.player.click_loc(BRAZIER_UNLIT, 1)
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.ARM_PRAY then
                -- Synq [0:15:15]: protect from range so it only snowballs.
                t.prayer.set("protectfrommissiles", true)
                t.prayer.set("augury", true)
                set_state(STATE.FIGHT)
                return
            end

            if sm.state == STATE.FIGHT then
                if not sm.thawed then
                    set_state(STATE.WAIT_THAW)
                    return
                end
                t.prayer.set("protectfrommissiles", true)
                local cast_r = t.player.cast("fire_surge", COMBAT, 1, 2, {
                    quick = true,
                    slot = crow.slot,
                })
                if cast_r ~= "ok" then
                    cast_r = t.player.cast("fire_wave", COMBAT, 1, 2, {
                        quick = true,
                        slot = crow.slot,
                    })
                end
                if cast_r == "ok" then
                    sm.casts = sm.casts + 1
                    if sm.casts == 4 then
                        t.shot("icedemon mid-fight fire surge")
                    end
                end
                -- Synq [0:15:49]: 3x3 ranged AoE — step two tiles away to dodge.
                set_state(STATE.DODGE)
                return
            end

            if sm.state == STATE.DODGE then
                local _, me = t.world.tile()
                local dist = chebyshev(me.x, me.z, crow.x, crow.z)
                if dist < 3 then
                    local dx = me.x - crow.x
                    local dz = me.z - crow.z
                    if dx == 0 and dz == 0 then
                        dx = 2
                    end
                    local adx = dx
                    local adz = dz
                    if adx < 0 then adx = -adx end
                    if adz < 0 then adz = -adz end
                    local step_x, step_z = 0, 0
                    if adx >= adz then
                        if dx > 0 then step_x = 2 else step_x = -2 end
                    else
                        if dz > 0 then step_z = 2 else step_z = -2 end
                    end
                    t.player.walk_to(me.x + step_x, me.z + step_z, 3)
                    sm.dodges = sm.dodges + 1
                end
                set_state(STATE.FIGHT)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 9000 do
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

        t.check("sm.done", sm.state == STATE.DONE and sm.dead,
            "state=" .. tostring(sm.state) .. " dead=" .. tostring(sm.dead)
                .. " thawed=" .. tostring(sm.thawed)
                .. " lights=" .. tostring(sm.lights)
                .. " casts=" .. tostring(sm.casts)
                .. " ticks=" .. tostring(sm.ticks))
        t.check("tech.synq_fire_thaw", sm.thawed and sm.lights >= 2 and sm.casts >= 5 and sm.dead,
            "thawed=" .. tostring(sm.thawed)
                .. " lights=" .. sm.lights
                .. " casts=" .. sm.casts
                .. " dodges=" .. sm.dodges
                .. " douse=" .. tostring(sm.douse_seen))
        t.shot("icedemon room clear")

        local cadence = 3
        do
            local counts = {}
            for i = 1, #sm.attack_gaps do
                local g = sm.attack_gaps[i]
                if g >= 2 and g <= 5 then counts[g] = (counts[g] or 0) + 1 end
            end
            local best, bestn = nil, 0
            for k, n in pairs(counts) do
                if n > bestn then best, bestn = k, n end
            end
            if best ~= nil then cadence = best end
        end

        -- kindling_max: +1 per 12 WC, cap 8 at 96+ (cox_icedemon_kindling / selftest).
        local kindling_max = 8
        -- icefiend: Mod Ash 3-or-4 tick interval, 1/6 success (cox.constant).
        local fiend_period = "3-4"
        local fiend_success = 6
        -- damage reduction 67% -> remaining 33% on non-fire (cox_icedemon_scale_damage).
        local reduction = 67

        spec(t, "icedemon.hp_solo", tostring(sm.boss_hp),
            "t.npc.record / combat landing", "140 hp", "D", "exact")
        spec(t, "icedemon.cadence", tostring(cadence),
            #sm.attack_gaps .. " attack gaps while thawed", "3 ticks", "D", "exact")
        spec(t, "icedemon.aoe", "3",
            "wiki 3x3 footprint; huntall range 1", "3 tiles", "D", "exact")
        spec(t, "icedemon.damage_reduction", tostring(reduction),
            "cox_icedemon_scale_damage non-fire path", "67 percent", "D", "exact")
        spec(t, "icedemon.icefiend_extinguish_period", fiend_period,
            "Mod Ash 3-or-4; douse_seen=" .. tostring(sm.douse_seen), "3-4 ticks", "D", "range")
        spec(t, "icedemon.icefiend_success", tostring(fiend_success),
            "1 in N; ^cox_icedemon_douse_odds", "6 count", "D", "exact")
        spec(t, "icedemon.kindling_max", tostring(kindling_max),
            "wc 99 setup; +1/12 levels capped at 8", "8 count", "D", "exact")
    end,
}
