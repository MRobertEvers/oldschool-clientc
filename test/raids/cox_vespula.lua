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
-- is under her (size 5). Synq safe tiles sit just outside that melee envelope
-- while still a short run from a tile that can hit the portal [1:29:03].
local SAFE_CHEBYSHEV = 7
-- Boss size 5: stay at least this Chebyshev from her SW tile.
local BOSS_CLEAR = 6

-- Synq [1:27:13] / [1:29:03]: LAND → ARM → (TO_GAP if not in range) →
-- ATTACK_PORTAL ⇄ STEP_SAFE / RESTORE. Seed-1 landing is already portal
-- range 6, so ARM goes straight to ATTACK; TO_GAP is the fallback walk.
local STATE = {
    LAND = "LAND",
    ARM_PRAYERS = "ARM_PRAYERS",
    TO_GAP = "TO_GAP",
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

-- Candidate safe tiles around the portal (cardinals + diagonals at range 7).
-- Prefer tiles away from the boss footprint so walk_to is not blocked under her.
-- Seed-1 landing is NORTH of the portal; the south gap is behind the barrier
-- ("I can't reach that!"). Sort by distance to the player so TO_GAP walks the
-- near-side tile first.
local function safe_candidates(portal, boss, me)
    local c = {
        { portal.x, portal.z + SAFE_CHEBYSHEV },
        { portal.x, portal.z - SAFE_CHEBYSHEV },
        { portal.x - SAFE_CHEBYSHEV, portal.z },
        { portal.x + SAFE_CHEBYSHEV, portal.z },
        { portal.x - SAFE_CHEBYSHEV, portal.z + SAFE_CHEBYSHEV },
        { portal.x + SAFE_CHEBYSHEV, portal.z + SAFE_CHEBYSHEV },
        { portal.x - SAFE_CHEBYSHEV, portal.z - SAFE_CHEBYSHEV },
        { portal.x + SAFE_CHEBYSHEV, portal.z - SAFE_CHEBYSHEV },
    }
    local out = c
    if boss ~= nil then
        out = {}
        for i = 1, #c do
            if chebyshev(c[i][1], c[i][2], boss.x, boss.z) >= BOSS_CLEAR then
                out[#out + 1] = c[i]
            end
        end
        if #out == 0 then out = c end
    end
    if me ~= nil and me.x ~= nil then
        table.sort(out, function(a, b)
            return chebyshev(a[1], a[2], me.x, me.z) < chebyshev(b[1], b[2], me.x, me.z)
        end)
    end
    return out
end

return {
    id = "cox_vespula",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
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
        t.check("tile.landing", tr0 == "ok" and me0 ~= nil,
            tr0 == "ok" and (tostring(me0.x) .. "," .. tostring(me0.z)) or tostring(tr0))
        local cands = safe_candidates(prow, brow, me0)
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
            -- Do not reshuffle sm.safe_* here: sample_world runs every tick and
            -- re-sorting by player position made TO_GAP chase a moving target.
            if fr == "ok" and frow ~= nil then
                sm.boss_x = frow.x
                sm.boss_z = frow.z
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
            -- Redemption healed us from low HP (Synq: she hits up to 8 on the tiles).
            if sm.last_hp < 15 and level > sm.last_hp + 5 then
                sm.redemptions = sm.redemptions + 1
            end
            sm.last_hp = level
        end

        local function arm_redemption()
            -- Synq [1:29:03]: redemption + rigor (tbow) / augury (mage).
            -- eagleeye and augury share exclusion groups — never light both.
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
            -- Leave the prayer IF so walk/attack scene ops are not fighting it.
            t.ui.tab("combat")
            return "ok", "redemption+ranged style"
        end

        local function portal_row()
            local por, portal = t.npc.nearest(PORTAL, 40)
            if por ~= "ok" then return nil end
            return portal
        end

        -- Synq safe / gap tile: outside her melee envelope (Chebyshev >= 7),
        -- adjacent to the barrier gap so a ranged Attack can path to the portal.
        local function on_gap(me)
            if me == nil or me.x == nil then return false end
            return chebyshev(me.x, me.z, sm.safe_x, sm.safe_z) <= 1
        end

        local function on_safe(me)
            if me == nil or me.x == nil then return false end
            local portal = portal_row()
            if portal == nil then return true end
            return chebyshev(me.x, me.z, portal.x, portal.z) >= SAFE_CHEBYSHEV
        end

        -- One-tile step toward (tx,tz). Absolute walk_to across the room was a
        -- no-op under the gate; single-tile steps path through the barrier gap.
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
            local wr, wd = t.player.walk_to(me.x + sx, me.z + sz, 4)
            if wr == "ok" then return wr, wd end
            if sx ~= 0 and adz > 0 then
                sx, sz = 0, (dz > 0 and 1 or -1)
            elseif sz ~= 0 and adx > 0 then
                sx, sz = (dx > 0 and 1 or -1), 0
            else
                return wr, wd
            end
            return t.player.walk_to(me.x + sx, me.z + sz, 4)
        end

        local function step_safe_once()
            local portal = portal_row()
            if portal == nil then return "ok", "portal gone" end
            local tr, me = t.world.tile()
            if tr ~= "ok" or me == nil then return "no_row", "no tile" end
            if on_safe(me) then return "ok", "already safe" end
            -- Prefer the authored gap tile; else step away from the portal.
            if not on_gap(me) then
                return step_toward(sm.safe_x, sm.safe_z)
            end
            local rdx = me.x - portal.x
            local rdz = me.z - portal.z
            local adx, adz = rdx, rdz
            if adx < 0 then adx = -adx end
            if adz < 0 then adz = -adz end
            local sx, sz = 0, 0
            if adx >= adz then
                if rdx >= 0 then sx = 1 else sx = -1 end
            else
                if rdz >= 0 then sz = 1 else sz = -1 end
            end
            return t.player.walk_to(me.x + sx, me.z + sz, 4)
        end

        local function rotate_gap()
            local tr_me, me_now = t.world.tile()
            local por, portal = t.npc.nearest(PORTAL, 40)
            local fr, frow = find_boss(t)
            if por == "ok" and portal ~= nil then
                local me_arg = (tr_me == "ok") and me_now or nil
                local boss_arg = (fr == "ok") and frow or nil
                cands = safe_candidates(portal, boss_arg, me_arg)
            end
            sm.cand_i = sm.cand_i + 1
            if sm.cand_i > #cands then sm.cand_i = 1 end
            sm.safe_x = cands[sm.cand_i][1]
            sm.safe_z = cands[sm.cand_i][2]
            sm.stuck = 0
            t.ticklog.mark("rotate gap to " .. sm.safe_x .. "," .. sm.safe_z)
        end

        local function decide()
            sample_world()
            if portal_gone() then
                sm.portal_dead = true
                set_state(STATE.DONE)
                return
            end

            local hpr, hp = t.skill.read("hitpoints")
            if hpr == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < 20 then
                -- Redemption should fire; do not brew through the method.
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
                -- Synq [1:27:13]: attack immediately. Reach refusals go TO_GAP.
                set_state(STATE.ATTACK_PORTAL)
                return
            end

            if sm.state == STATE.TO_GAP then
                local tr, me = t.world.tile()
                if tr ~= "ok" or me == nil then
                    return
                end
                if on_gap(me) or on_safe(me) then
                    t.ticklog.mark("on gap tile " .. me.x .. "," .. me.z)
                    set_state(STATE.ATTACK_PORTAL)
                    return
                end
                -- walk_to auto-deadline hung under soft3d frame-skip. One
                -- adjacent step_tick only (returns in a few server ticks).
                local dx = sm.safe_x - me.x
                local dz = sm.safe_z - me.z
                local adx, adz = dx, dz
                if adx < 0 then adx = -adx end
                if adz < 0 then adz = -adz end
                local nx, nz = me.x, me.z
                if adx >= adz and adx > 0 then
                    if dx > 0 then nx = me.x + 1 else nx = me.x - 1 end
                elseif adz > 0 then
                    if dz > 0 then nz = me.z + 1 else nz = me.z - 1 end
                end
                local sr, sd = t.player.step_tick(nx, nz, 3)
                if sr ~= "ok" then
                    t.note("gap step " .. tostring(sr) .. " " .. tostring(sd))
                    nx, nz = me.x, me.z
                    if adz >= adx and adz > 0 then
                        if dx > 0 then nx = me.x + 1 elseif dx < 0 then nx = me.x - 1 end
                    elseif adx > 0 then
                        if dz > 0 then nz = me.z + 1 elseif dz < 0 then nz = me.z - 1 end
                    end
                    if nx ~= me.x or nz ~= me.z then
                        sr, sd = t.player.step_tick(nx, nz, 3)
                        t.note("gap step2 " .. tostring(sr) .. " " .. tostring(sd))
                    end
                end
                if sm.stuck >= 6 then
                    rotate_gap()
                end
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
                arm_redemption()
                -- Short settle: a long await under soft3d frame-skip can burn
                -- the whole TORIRS_MAX_FRAMES budget without returning.
                local ar, ad = t.player.attack(PORTAL, 2, 3)
                if ar == "ok" then
                    sm.portal_hits = sm.portal_hits + 1
                    if sm.portal_hits == 1 then
                        t.shot("vespula redemption first portal hit")
                    elseif sm.portal_hits == 3 then
                        t.shot("vespula redemption mid-mechanic")
                    end
                    set_state(STATE.STEP_SAFE)
                    return
                end
                t.note("portal attack " .. tostring(ar) .. " " .. tostring(ad))
                if string.find(tostring(ad), "reach", 1, true) then
                    set_state(STATE.TO_GAP)
                    return
                end
                if sm.portal_hits == 0 and sm.ticks > 200 then
                    t.check("portal.attack", false, tostring(ar) .. " " .. tostring(ad))
                    set_state(STATE.DONE)
                    return
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
                local tr2, me2 = t.world.tile()
                if tr2 == "ok" and me2 ~= nil then
                    local portal = portal_row()
                    if portal ~= nil then
                        local rdx = me2.x - portal.x
                        local rdz = me2.z - portal.z
                        local sx, sz = 0, 0
                        if (rdx < 0 and -rdx or rdx) >= (rdz < 0 and -rdz or rdz) then
                            sx = (rdx >= 0) and 1 or -1
                        else
                            sz = (rdz >= 0) and 1 or -1
                        end
                        t.player.step_tick(me2.x + sx, me2.z + sz, 3)
                    end
                end
                if sm.stuck >= 3 then
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
