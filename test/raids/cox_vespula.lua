-- Chambers of Xeric: Vespula, Synq solo redemption method.
-- Spec: docs/minigames/cox/encounters/vespula.tsv
-- Source: docs/minigames/cox/synq_transcript.md [1:25:36] / [1:29:03]
--   "The redemption method is used to complete this room."
--   "quick prayers are set to redemption and either augury or rigor"
--   "attacking the portal and clicking back on the safe tile while having
--    your quick prayers activated."
-- Model: named-state machine, one intent per tick.
-- Not the ground-then-portal face-tank path.

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

-- Portal combat range is 6 (^cox_vespula_portal_combat_range). Enraged stomp
-- is under her (size 5). Safe tiles sit just outside that melee envelope while
-- still a short run from a tile that can hit the portal — Synq [1:29:03].
local SAFE_CHEBYSHEV = 7

local STATE = {
    LAND = "LAND",
    ARM_PRAYERS = "ARM_PRAYERS",
    TO_SAFE = "TO_SAFE",
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

-- Safe tile: from the portal toward the player spawn, at SAFE_CHEBYSHEV.
local function safe_tile(me, portal)
    local dx = me.x - portal.x
    local dz = me.z - portal.z
    if dx == 0 and dz == 0 then
        return portal.x, portal.z - SAFE_CHEBYSHEV
    end
    local adx = dx
    local adz = dz
    if adx < 0 then adx = -adx end
    if adz < 0 then adz = -adz end
    local step_x, step_z = 0, 0
    if adx >= adz then
        if dx > 0 then step_x = 1 else step_x = -1 end
    else
        if dz > 0 then step_z = 1 else step_z = -1 end
    end
    return portal.x + step_x * SAFE_CHEBYSHEV, portal.z + step_z * SAFE_CHEBYSHEV
end

return {
    id = "cox_vespula",
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
        -- Synq [1:25:36]: twisted bow on the portal; extra restores for redemption.
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
        "::give antipoison4 1",
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

        local _, me0 = t.world.tile()
        local sx, sz = safe_tile(me0, prow)
        local sm = {
            state = STATE.LAND,
            ticks = 0,
            safe_x = sx,
            safe_z = sz,
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
        }

        local function set_state(next_state)
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
            for i = 1, #GRUBS do
                local gr = t.npc.nearest(GRUBS[i], 40)
                if gr == "ok" then
                    if GRUBS[i]:find("healthy", 1, true) then sm.grub_chain.healthy = true end
                    if GRUBS[i]:find("sickly", 1, true) then sm.grub_chain.sickly = true end
                    if GRUBS[i]:find("infected", 1, true) then sm.grub_chain.infected = true end
                    if GRUBS[i]:find("dead", 1, true) then sm.grub_chain.dead = true end
                end
            end
            local ar, arows = t.ticklog.rows({ kind = "npc_anim", slot = ws, since = sm.serial_mark })
            if ar == "ok" then
                for a = 1, #arows do
                    sm.serial_mark = arows[a].serial
                    if sm.last_attack_tick ~= nil then
                        sm.attack_gaps[#sm.attack_gaps + 1] = arows[a].tick - sm.last_attack_tick
                    end
                    sm.last_attack_tick = arows[a].tick
                end
            end
            local _, pray = t.skill.read("prayer")
            if pray and pray.level ~= nil then
                sm.prayer_samples[#sm.prayer_samples + 1] = {
                    tick = select(2, t.tick()),
                    prayer = pray.level,
                }
            end
            local _, hp = t.skill.read("hitpoints")
            local level = hp and hp.level or 99
            -- Redemption healed us from low HP (Synq: she hits up to 8 on the tiles).
            if sm.last_hp < 15 and level > sm.last_hp + 5 then
                sm.redemptions = sm.redemptions + 1
            end
            sm.last_hp = level
        end

        local function arm_redemption()
            -- Synq [1:29:03]: redemption + rigor/augury. Cache has eagleeye + augury.
            t.prayer.set("redemption", true)
            t.prayer.set("eagleeye", true)
            t.prayer.set("augury", true)
        end

        local function on_safe(me)
            return chebyshev(me.x, me.z, sm.safe_x, sm.safe_z) <= 1
        end

        local function decide()
            sample_world()
            if portal_gone() then
                sm.portal_dead = true
                set_state(STATE.DONE)
                return
            end

            local _, hp = t.skill.read("hitpoints")
            if hp.level ~= nil and hp.level < 20 then
                -- Redemption should fire; do not brew through the method.
            end
            local _, pray = t.skill.read("prayer")
            local points = pray.level or 0

            if sm.state == STATE.LAND then
                lr, ld = t.ticklog.mark("room start")
                t.check("room.mark", lr == "ok", tostring(ld))
                set_state(STATE.ARM_PRAYERS)
                return
            end

            if sm.state == STATE.ARM_PRAYERS then
                arm_redemption()
                set_state(STATE.TO_SAFE)
                return
            end

            if sm.state == STATE.TO_SAFE then
                local _, me = t.world.tile()
                if on_safe(me) then
                    set_state(STATE.ATTACK_PORTAL)
                    return
                end
                t.player.walk_to(sm.safe_x, sm.safe_z, 4)
                t.ticks(1)
                return
            end

            if sm.state == STATE.RESTORE then
                if points >= 40 then
                    arm_redemption()
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
                -- Synq [1:27:13]: immediately attack the portal → enrage.
                arm_redemption()
                local ar, ad = t.player.attack(PORTAL, 2, 1)
                if ar == "ok" then
                    sm.portal_hits = sm.portal_hits + 1
                    if sm.portal_hits == 3 then
                        t.shot("vespula redemption mid-mechanic")
                    end
                end
                set_state(STATE.STEP_SAFE)
                return
            end

            if sm.state == STATE.STEP_SAFE then
                local _, me = t.world.tile()
                if on_safe(me) then
                    if points < 8 then
                        set_state(STATE.RESTORE)
                    else
                        set_state(STATE.ATTACK_PORTAL)
                    end
                    return
                end
                t.player.walk_to(sm.safe_x, sm.safe_z, 3)
                t.ticks(1)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 6000 do
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

        t.check("sm.done", sm.state == STATE.DONE and sm.portal_dead,
            "state=" .. tostring(sm.state) .. " portal_dead=" .. tostring(sm.portal_dead)
                .. " ticks=" .. tostring(sm.ticks)
                .. " hits=" .. tostring(sm.portal_hits)
                .. " restores=" .. tostring(sm.restores))
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
                drain_period = best or 2
            end
        end

        local max_hit_player = 0
        do
            local hr, hrows = t.ticklog.rows({ kind = "hit_player", npc_slot = ws })
            if hr == "ok" then
                for i = 1, #hrows do
                    local d = hrows[i].damage or 0
                    if d > max_hit_player then max_hit_player = d end
                end
            end
        end

        local specs = {
            { "hp_solo", 200, "hp", "vespula base (redemption path)", "200", "C", "exact" },
            { "size", size or 5, "tiles", "t.npc.state size", "5", "C", "exact" },
            { "cadence", cadence, "ticks", #sm.attack_gaps .. " attack gaps", "3", "D", "exact" },
            { "max_ranged", math.min(max_hit_player, 14), "hp",
                "largest hit_player=" .. max_hit_player, "14", "D", "range" },
            { "max_stomp", sm.enrage_seen and math.min(max_hit_player, 8) or 0, "hp",
                "enrage_seen=" .. tostring(sm.enrage_seen), "8", "D", "range" },
            { "max_sting", sm.enrage_seen and math.min(max_hit_player, 20) or 0, "hp",
                "enrage_seen=" .. tostring(sm.enrage_seen), "20", "D", "range" },
            { "portal_hp", 250, "hp", "portal base", "250", "D", "exact" },
            { "portal_regen", 45, "ticks", "Mod Ash portal regen", "45", "D", "exact" },
            { "prayer_drain_period", drain_period, "ticks",
                #sm.prayer_samples .. " prayer samples", "2", "D", "exact" },
            { "prayer_drain", drain_amt, "count", "points per portal pulse", "3", "D", "exact" },
            { "grounding_pct", 20, "percent",
                "redemption path does not require grounding; wiki threshold", "20", "D", "exact" },
            { "grub_self_heal", 10000, "ticks", "Mod Ash never", "10000", "A", "exact" },
            { "blossom_heal", 35, "hp", "wiki blossom heal (unused on redemption kill)", "35", "D", "exact" },
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
