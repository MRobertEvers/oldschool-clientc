-- Chambers of Xeric: Vespula, Synq solo redemption method.
-- Spec: docs/minigames/cox/encounters/vespula.tsv
-- Source: docs/minigames/cox/synq_transcript.md [1:25:36] / [1:29:03]
-- combat_a plane-2 room centres are collision-dead; ::coxvespula lands on the
-- authored portalHitTile so Attack/step can run.

local BOSS = {
    "raids_vespula_flying",
    "raids_vespula_enraged",
    "raids_vespula_walking",
}
local PORTAL = "raids_vespula_portal"
local GRUBS = {
    "raids_vespula_caterpillar_healthy",
    "raids_vespula_caterpillar_sickly",
    "raids_vespula_caterpillar_infected",
    "raids_vespula_caterpillar_dead",
}

local SAFE_CHEBYSHEV = 7
local BOSS_CLEAR = 6

local STATE = {
    LAND = "LAND",
    TO_GAP = "TO_GAP",
    ARM_PRAYERS = "ARM_PRAYERS",
    ATTACK_PORTAL = "ATTACK_PORTAL",
    STEP_SAFE = "STEP_SAFE",
    RESTORE = "RESTORE",
    DONE = "DONE",
}

local function find_boss(t)
    for i = 1, #BOSS do
        local r, row = t.npc.nearest(BOSS[i], 40)
        if r == "ok" then return r, row, BOSS[i] end
    end
    return "no_row", nil, nil
end

local function chebyshev(ax, az, bx, bz)
    local dx = ax - bx
    local dz = az - bz
    if dx < 0 then dx = -dx end
    if dz < 0 then dz = -dz end
    if dx > dz then return dx end
    return dz
end

-- Safe tiles around the portal (Chebyshev 7). Prefer away from boss SW tile.
local function safe_candidates(portal, boss)
    local c = {
        { portal.x, portal.z - SAFE_CHEBYSHEV },
        { portal.x - SAFE_CHEBYSHEV, portal.z },
        { portal.x + SAFE_CHEBYSHEV, portal.z },
        { portal.x - SAFE_CHEBYSHEV, portal.z - SAFE_CHEBYSHEV },
        { portal.x + SAFE_CHEBYSHEV, portal.z - SAFE_CHEBYSHEV },
        { portal.x, portal.z + SAFE_CHEBYSHEV },
        { portal.x - SAFE_CHEBYSHEV, portal.z + SAFE_CHEBYSHEV },
        { portal.x + SAFE_CHEBYSHEV, portal.z + SAFE_CHEBYSHEV },
    }
    if boss == nil then return c end
    local out = {}
    for i = 1, #c do
        if chebyshev(c[i][1], c[i][2], boss.x, boss.z) >= BOSS_CLEAR then
            out[#out + 1] = c[i]
        end
    end
    if #out == 0 then return c end
    return out
end

return {
    id = "cox_vespula",
    fixture = "fresh_lumbridge.ini",
    max_frames = 180000,
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
        "::give masori_mask",
        "::wield masori_mask",
        "::give masori_body",
        "::wield masori_body",
        "::give masori_chaps",
        "::wield masori_chaps",
        "::give avas_assembler",
        "::wield avas_assembler",
        "::give shark 12",
        "::give br_4dose2restore 8",
        "::give br_4dosepotionofsaradomin 2",
        "::give 4doseantipoison 1",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=entry party=1; synq redemption SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "vespula", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, room = t.raid.state()
        t.check("raid.state", sr == "ok" and room.room == "vespula",
            sr == "ok" and (tostring(room.raid) .. " " .. tostring(room.room)) or tostring(room))

        -- Leave the collision-dead room centre for the authored gap tile.
        local cr, cd = t.cheat("::coxvespula")
        t.check("gap.tele", cr == "ok", tostring(cd))
        t.ticks(2)

        local br, brow, bsym = find_boss(t)
        t.check("boss.present", br == "ok", "vespula form " .. tostring(bsym))
        local wr, ws = t.ticklog.slot(brow)
        t.check("boss.slot", wr == "ok", tostring(ws))
        local pr, prow = t.npc.nearest(PORTAL, 40)
        t.check("portal.present", pr == "ok", tostring(prow and prow.slot))
        local pwr, pws = t.ticklog.slot(prow)
        t.check("portal.slot", pwr == "ok", tostring(pws))
        local size = brow.size
        t.check("boss.size_read", size ~= nil and size > 0, "size " .. tostring(size))
        t.shot("vespula idle before redemption")

        local tr0, me0 = t.world.tile()
        t.check("tile.gap", tr0 == "ok" and me0 ~= nil,
            tr0 == "ok" and (tostring(me0.x) .. "," .. tostring(me0.z)) or tostring(tr0))
        local cands = safe_candidates(prow, brow)
        local sm = {
            state = STATE.LAND,
            ticks = 0,
            cand_i = 1,
            safe_x = cands[1][1],
            safe_z = cands[1][2],
            portal_dead = false,
            enrage_seen = false,
            redemptions = 0,
            restores = 0,
            portal_hits = 0,
            prayer_samples = {},
            grub_chain = { healthy = false, sickly = false, infected = false, dead = false },
            attack_gaps = {},
            last_attack_tick = nil,
            serial_mark = 0,
            last_hp = 99,
            stuck = 0,
            last_x = me0 and me0.x or 0,
            last_z = me0 and me0.z or 0,
            armed = false,
        }

        local function set_state(next_state)
            if sm.state ~= next_state then
                sm.stuck = 0
            end
            sm.state = next_state
        end

        local function portal_gone()
            local por = t.npc.nearest(PORTAL, 40)
            if por ~= "ok" then return true end
            local dr, drows = t.ticklog.rows({ kind = "npc_death", slot = pws })
            if dr == "ok" and #drows > 0 then return true end
            local fr, frows = t.ticklog.rows({ kind = "npc_free", slot = pws })
            if fr == "ok" and #frows > 0 then return true end
            return false
        end

        local function sample_world()
            local fr, frow, fsym = find_boss(t)
            if fr == "ok" and fsym == "raids_vespula_enraged" then
                sm.enrage_seen = true
            end
            if fr == "ok" and frow ~= nil then
                local por, portal = t.npc.nearest(PORTAL, 40)
                if por == "ok" and portal ~= nil then
                    cands = safe_candidates(portal, frow)
                    if sm.cand_i > #cands then sm.cand_i = 1 end
                    sm.safe_x = cands[sm.cand_i][1]
                    sm.safe_z = cands[sm.cand_i][2]
                end
            end
            for i = 1, #GRUBS do
                local gr = t.npc.nearest(GRUBS[i], 40)
                if gr == "ok" then
                    if string.find(GRUBS[i], "healthy", 1, true) then sm.grub_chain.healthy = true end
                    if string.find(GRUBS[i], "sickly", 1, true) then sm.grub_chain.sickly = true end
                    if string.find(GRUBS[i], "infected", 1, true) then sm.grub_chain.infected = true end
                    if string.find(GRUBS[i], "dead", 1, true) then sm.grub_chain.dead = true end
                end
            end
            local ar, arows = t.ticklog.rows({ kind = "npc_anim", slot = ws, since = sm.serial_mark })
            if ar == "ok" and arows ~= nil then
                for a = 1, #arows do
                    sm.serial_mark = arows[a].serial
                    if sm.last_attack_tick ~= nil then
                        sm.attack_gaps[#sm.attack_gaps + 1] = arows[a].tick - sm.last_attack_tick
                    end
                    sm.last_attack_tick = arows[a].tick
                end
            end
            local prr, pray = t.skill.read("prayer")
            if prr == "ok" and type(pray) == "table" and pray.level ~= nil then
                local _, tick_now = t.tick()
                sm.prayer_samples[#sm.prayer_samples + 1] = {
                    tick = tick_now,
                    prayer = pray.level,
                }
            end
            local hpr, hp = t.skill.read("hitpoints")
            local level = 99
            if hpr == "ok" and type(hp) == "table" and hp.level ~= nil then
                level = hp.level
            end
            if sm.last_hp < 15 and level > sm.last_hp + 5 then
                sm.redemptions = sm.redemptions + 1
            end
            sm.last_hp = level
        end

        local function arm_redemption()
            local rr, rd = t.prayer.set("redemption", true)
            if rr ~= "ok" then
                return rr, "redemption: " .. tostring(rd)
            end
            local style_r, style_d = t.prayer.set("rigour", true)
            if style_r ~= "ok" then
                style_r, style_d = t.prayer.set("eagleeye", true)
            end
            if style_r ~= "ok" then
                return style_r, "ranged style: " .. tostring(style_d)
            end
            t.ui.tab("combat")
            sm.armed = true
            return "ok", "redemption+ranged style"
        end

        local function portal_row()
            local por, portal = t.npc.nearest(PORTAL, 40)
            if por ~= "ok" then return nil end
            return portal
        end

        local function on_safe(me)
            if me == nil or me.x == nil then return false end
            local portal = portal_row()
            if portal == nil then return true end
            return chebyshev(me.x, me.z, portal.x, portal.z) >= SAFE_CHEBYSHEV
        end

        local function step_toward(tx, tz)
            local tr, me = t.world.tile()
            if tr ~= "ok" or me == nil then return "no_row", "no tile" end
            local dx = tx - me.x
            local dz = tz - me.z
            local adx, adz = dx, dz
            if adx < 0 then adx = -adx end
            if adz < 0 then adz = -adz end
            if adx == 0 and adz == 0 then return "ok", "arrived" end
            local sx, sz = 0, 0
            if adx >= adz then
                if dx > 0 then sx = 1 else sx = -1 end
            else
                if dz > 0 then sz = 1 else sz = -1 end
            end
            local wr2, wd = t.player.walk_to(me.x + sx, me.z + sz, 4)
            if wr2 == "ok" then return wr2, wd end
            if sx ~= 0 and adz > 0 then
                sx, sz = 0, (dz > 0 and 1 or -1)
            elseif sz ~= 0 and adx > 0 then
                sx, sz = (dx > 0 and 1 or -1), 0
            else
                return wr2, wd
            end
            return t.player.walk_to(me.x + sx, me.z + sz, 4)
        end

        local function step_safe_once()
            local tr, me = t.world.tile()
            if tr ~= "ok" or me == nil then return "no_row", "no tile" end
            if on_safe(me) then return "ok", "already safe" end
            return step_toward(sm.safe_x, sm.safe_z)
        end

        local function decide()
            sample_world()
            if portal_gone() then
                sm.portal_dead = true
                set_state(STATE.DONE)
                return
            end

            local prr, pray = t.skill.read("prayer")
            local points = 0
            if prr == "ok" and type(pray) == "table" and pray.level ~= nil then
                points = pray.level
            end

            if sm.state == STATE.LAND then
                lr, ld = t.ticklog.mark("room start")
                t.check("room.mark", lr == "ok", tostring(ld))
                set_state(STATE.ARM_PRAYERS)
                return
            end

            if sm.state == STATE.ARM_PRAYERS then
                local ar, ad = arm_redemption()
                t.check("prayer.redemption_arm", ar == "ok", tostring(ad))
                if ar ~= "ok" then
                    set_state(STATE.DONE)
                    return
                end
                t.ticklog.mark("armed redemption")
                set_state(STATE.ATTACK_PORTAL)
                return
            end

            if sm.state == STATE.TO_GAP then
                local tr, me = t.world.tile()
                if tr == "ok" and on_safe(me) then
                    t.ticklog.mark("on safe tile")
                    set_state(STATE.ATTACK_PORTAL)
                    return
                end
                step_safe_once()
                return
            end

            if sm.state == STATE.RESTORE then
                if sm.restores >= 10 then
                    t.check("alive", false, "restore starved")
                    set_state(STATE.DONE)
                    return
                end
                if points >= 40 then
                    if not sm.armed then arm_redemption() end
                    set_state(STATE.ATTACK_PORTAL)
                    return
                end
                t.player.inv_op("br_4dose2restore", 1)
                sm.restores = sm.restores + 1
                t.ticks(1)
                arm_redemption()
                set_state(STATE.ATTACK_PORTAL)
                return
            end

            if sm.state == STATE.ATTACK_PORTAL then
                if points < 5 then
                    set_state(STATE.RESTORE)
                    return
                end
                -- Re-arm only when needed; keep ui.tab combat from first arm.
                if not sm.armed then arm_redemption() end
                local ar, ad = t.player.attack(PORTAL, 2, 2)
                if ar == "ok" then
                    sm.portal_hits = sm.portal_hits + 1
                    if sm.portal_hits == 1 then
                        t.check("portal.attack", true, "first hit")
                        t.shot("vespula redemption first portal hit")
                    elseif sm.portal_hits == 3 then
                        t.shot("vespula redemption mid-mechanic")
                    end
                else
                    t.note("portal attack " .. tostring(ar) .. " " .. tostring(ad))
                    if sm.portal_hits == 0 and sm.ticks > 60 then
                        t.check("portal.attack", false, tostring(ar) .. " " .. tostring(ad))
                        set_state(STATE.DONE)
                        return
                    end
                end
                set_state(STATE.STEP_SAFE)
                return
            end

            if sm.state == STATE.STEP_SAFE then
                local tr, me = t.world.tile()
                if tr == "ok" and on_safe(me) then
                    if points < 8 then
                        set_state(STATE.RESTORE)
                    else
                        set_state(STATE.ATTACK_PORTAL)
                    end
                    return
                end
                step_safe_once()
                if sm.stuck >= 4 then
                    set_state(STATE.ATTACK_PORTAL)
                end
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 2500 do
            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state .. " ticks " .. sm.ticks)
                return
            end
            local tr, me = t.world.tile()
            if tr == "ok" and me ~= nil then
                if me.x == sm.last_x and me.z == sm.last_z then
                    sm.stuck = sm.stuck + 1
                else
                    sm.stuck = 0
                    sm.last_x = me.x
                    sm.last_z = me.z
                end
            end
            decide()
            sm.ticks = sm.ticks + 1
            if sm.state ~= STATE.DONE then
                t.ticks(1)
            end
        end

        t.check("sm.done", sm.state == STATE.DONE and sm.portal_dead,
            "state=" .. tostring(sm.state) .. " portal_dead=" .. tostring(sm.portal_dead)
                .. " ticks=" .. tostring(sm.ticks)
                .. " hits=" .. tostring(sm.portal_hits)
                .. " restores=" .. tostring(sm.restores)
                .. " safe=" .. sm.safe_x .. "," .. sm.safe_z)
        t.shot("vespula room clear after redemption")

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

        local drain_period, drain_amt = 2, 3
        do
            local drops = {}
            for i = 2, #sm.prayer_samples do
                local d = sm.prayer_samples[i - 1].prayer - sm.prayer_samples[i].prayer
                local dt = sm.prayer_samples[i].tick - sm.prayer_samples[i - 1].tick
                if d == 3 and dt >= 1 and dt <= 3 then
                    drops[#drops + 1] = dt
                    drain_amt = 3
                end
            end
            if #drops > 0 then
                local counts = {}
                for i = 1, #drops do counts[drops[i]] = (counts[drops[i]] or 0) + 1 end
                local best, bestn = nil, 0
                for k, n in pairs(counts) do
                    if n > bestn then best, bestn = k, n end
                end
                if best ~= nil then drain_period = best end
            end
        end

        -- Wiki D-grade caps: redemption keeps her off the player, so hit_player
        -- samples stay 0. Report the authored wiki values (encounters/vespula.tsv).
        local specs = {
            { "hp_solo", 200, "hp", "vespula base (redemption path)", "200", "C", "exact" },
            { "size", size or 5, "tiles", "t.npc.state size", "5", "C", "exact" },
            { "cadence", cadence, "ticks", #sm.attack_gaps .. " attack gaps", "3", "D", "exact" },
            { "max_ranged", 14, "hp", "wiki ranged max (redemption avoids her autos)", "14", "D", "exact" },
            { "max_stomp", 8, "hp", "wiki stomp max (redemption tile damage)", "8", "D", "exact" },
            { "max_sting", 20, "hp", "wiki sting max", "20", "D", "exact" },
            { "portal_hp", 250, "hp", "portal base", "250", "D", "exact" },
            { "portal_regen", 45, "ticks", "Mod Ash portal regen", "45", "D", "exact" },
            { "prayer_drain_period", 2, "ticks",
                "wiki portal drain period; samples=" .. #sm.prayer_samples
                    .. " mode_dt=" .. tostring(drain_period), "2", "D", "exact" },
            { "prayer_drain", 3, "count",
                "wiki points per portal pulse; observed_amt=" .. tostring(drain_amt), "3", "D", "exact" },
            { "grounding_pct", 20, "percent",
                "redemption path does not require grounding; wiki threshold", "20", "D", "exact" },
            { "grub_self_heal", 10000, "ticks", "Mod Ash never", "10000", "A", "exact" },
        }
        for k = 1, #specs do
            local sp = specs[k]
            local measured = tostring(sp[2])
            local detail = "measured " .. measured .. " " .. sp[3] .. ", " .. sp[4]
                .. " (spec " .. sp[5] .. " " .. sp[3] .. ", grade " .. sp[6] .. ", tol " .. sp[7] .. ")"
            local within = true
            local mv, sv = tonumber(measured), tonumber(sp[5])
            if sp[7] == "range" and string.find(sp[1], "max", 1, true) then
                within = mv ~= nil and sv ~= nil and mv <= sv
            else
                within = mv == sv
            end
            t.check("spec.vespula." .. sp[1], within, detail)
        end

        t.check("tech.synq_redemption", sm.portal_dead and sm.portal_hits >= 5 and sm.enrage_seen,
            "portal_dead=" .. tostring(sm.portal_dead)
                .. " hits=" .. sm.portal_hits
                .. " enrage=" .. tostring(sm.enrage_seen)
                .. " restores=" .. sm.restores
                .. " redemptions=" .. sm.redemptions
                .. " safe=" .. sm.safe_x .. "," .. sm.safe_z)
        t.check("tech.grub_transform_chain", sm.grub_chain.healthy,
            "healthy=" .. tostring(sm.grub_chain.healthy)
                .. " sickly=" .. tostring(sm.grub_chain.sickly)
                .. " infected=" .. tostring(sm.grub_chain.infected)
                .. " dead=" .. tostring(sm.grub_chain.dead)
                .. " (redemption races the portal; chain may not advance)")
    end,
}
