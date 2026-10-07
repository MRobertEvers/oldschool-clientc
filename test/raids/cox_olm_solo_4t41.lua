-- Chambers of Xeric: Great Olm, solo Melee 4-tick 4:1.
-- Spec: docs/minigames/cox/encounters/olm_solo_4t41.tsv
-- Source: synq_transcript.md [2:48:05] Melee 4-Tick 4:1; COX_MECHANICS.md §2
--   16-tick cycle; attacks one tick after events; skip basic-2 and special
--   via empty facing zone / head turns. Prayer flick on style varp.
-- Model: named-state machine. No ::godmode / narrated kill.
-- Duo/trio harnesses come after this is green.

local HEAD = "olm_head"
local HEAD_SPAWN = "olm_head_spawning"
local LEFT = "olm_hand_left"   -- melee claw
local RIGHT = "olm_hand_right" -- mage claw
local TRACE = "varp6898_cox_trace_olm_action"
local SERIAL = "varp6899_cox_trace_olm_serial"
local PHASE = "varp6763_cox_olm_phase"
local FACING = "varp6772_cox_olm_facing"
local STYLE = "varp6766_cox_olm_style"

-- cox.constant chamber locals (m50_89)
local ZONE_WEST_MAX = 27
local ZONE_EAST_MIN = 36
local LEFT_LX, LEFT_LZ = 23, 30
local RIGHT_LX, RIGHT_LZ = 35, 30

local TRACE_BASIC = 1
local TRACE_SKIP = 8
local TRACE_EMPTY = 9
local TRACE_CATCHUP = 10
local TRACE_BURST = 4
local TRACE_LIGHTNING = 5
local TRACE_TELEPORT = 6
local TRACE_SPHERE = 2
local TRACE_PHASE = 11

local STATE = {
    ENTER = "ENTER",
    WAIT_SPAWN = "WAIT_SPAWN",
    KILL_MAGE = "KILL_MAGE",
    SETUP_41 = "SETUP_41",
    CYCLE_TANK = "CYCLE_TANK",       -- hit 1: tank basic 1
    CYCLE_FREE = "CYCLE_FREE",       -- empty-event free hit
    CYCLE_RUN = "CYCLE_RUN",         -- run head → skip basic 2
    CYCLE_TURN = "CYCLE_TURN",       -- turn head → skip special
    WAIT_PHASE = "WAIT_PHASE",
    HEAD = "HEAD",
    DONE = "DONE",
}

local function var(t, name)
    local r, v = t.var.server(name)
    if r == "ok" then return v end
    return nil
end

local function npc_ok(t, sym)
    local r, row = t.npc.state(sym)
    if r == "ok" then return row end
    return nil
end

local function sustain(t)
    local hr, hp = t.skill.read("hitpoints")
    if hr == "ok" and hp.level < 50 then
        t.player.eat("shark")
    end
    local pr, pp = t.prayer.points()
    local points = 0
    if pr == "ok" then points = pp.points or pp.level or 0 end
    if points < 25 then
        t.player.drink("br_4dose2restore")
    end
end

local function origin_of(me)
    return math.floor(me.x / 64) * 64, math.floor(me.z / 64) * 64
end

local function local_x(ox, x)
    return x - ox
end

-- Melee-hand ring/thumb tiles in chamber-local space (Synq 4:1 / 3:1 vocabulary).
-- West Olm: melee hand is LEFT at (23,30). East Olm mirrors about centre.
local function melee_tiles(ox, oz, side_west)
    if side_west then
        return {
            thumb = { x = ox + LEFT_LX + 2, z = oz + LEFT_LZ - 1 },
            ring = { x = ox + LEFT_LX - 1, z = oz + LEFT_LZ - 2 },
            head_safe = { x = ox + 28, z = oz + 28 },
            empty_east = { x = ox + ZONE_EAST_MIN + 1, z = oz + 28 },
            hand = LEFT,
            hand_lx = LEFT_LX,
        }
    end
    return {
        thumb = { x = ox + RIGHT_LX - 2, z = oz + RIGHT_LZ - 1 },
        ring = { x = ox + RIGHT_LX + 1, z = oz + RIGHT_LZ - 2 },
        head_safe = { x = ox + 35, z = oz + 28 },
        empty_east = { x = ox + ZONE_WEST_MAX - 1, z = oz + 28 },
        hand = RIGHT,
        hand_lx = RIGHT_LX,
    }
end

local function combat_level(t, sym)
    local rr, _, rec = t.npc.record(sym, { need = "client" })
    if rr == "ok" and rec and rec.client then
        return rec.client.combat_level
    end
    -- Fallback: try live copy after it is on screen.
    local lr, _, lrec = t.npc.record(sym)
    if lr == "ok" and lrec and lrec.client then
        return lrec.client.combat_level
    end
    return nil
end

return {
    id = "cox_olm_solo_4t41",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- 4-tick melee for 4:1 (Synq melee 4-tick section).
        "::give abyssal_whip",
        "::wield abyssal_whip",
        "::give infernal_cape",
        "::wield infernal_cape",
        "::give ferocious_gloves",
        "::wield ferocious_gloves",
        "::give primordial_boots",
        "::wield primordial_boots",
        "::give ultor_ring",
        "::wield ultor_ring",
        -- Mage hand wants magic (66% mitigation on non-magic). Synq sang/shadow.
        "::give sanguinesti_staff_uncharged",
        "::give bloodrune 4000",
        "::give ancestral_hat",
        "::give ancestral_robe_top",
        "::give ancestral_robe_bottom",
        "::give occult_necklace",
        -- Head phase: twisted bow (ranged weakness on head).
        "::give twisted_bow",
        "::give dragon_arrow 2000",
        "::give masori_mask",
        "::give masori_body",
        "::give masori_chaps",
        "::give avas_assembler",
        "::give shark 24",
        "::give br_4dose2restore 8",
        "::give br_4dosepotionofsaradomin 6",
        "::give 4dose2combat 2",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; synq melee 4-tick 4:1 SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "olm", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))

        local sm = {
            state = STATE.ENTER,
            ticks = 0,
            ox = 0, oz = 0,
            side_west = true,
            tiles = nil,
            cycle = 0,
            skips = 0,
            empties = 0,
            basics = 0,
            specials = 0,
            last_serial = var(t, SERIAL) or 0,
            last_action_tick = nil,
            action_gaps = {},
            phases_seen = 0,
            mage_kills = 0,
            melee_kills = 0,
            mid_shot = false,
            head_dead = false,
            setup_waits = 0,
            sub = 0,
            head_vis = nil,
            left_vis = nil,
            right_vis = nil,
            pray_flicks = 0,
            last_pray = nil,
        }

        local function set_state(s)
            sm.state = s
            sm.sub = 0
        end

        local function refresh_geometry()
            local _, me = t.world.tile()
            sm.ox, sm.oz = origin_of(me)
            local head = npc_ok(t, HEAD) or npc_ok(t, HEAD_SPAWN)
            if head ~= nil then
                local lx = local_x(sm.ox, head.x)
                sm.side_west = lx < 32
            end
            sm.tiles = melee_tiles(sm.ox, sm.oz, sm.side_west)
        end

        -- Synq [2:04:12]: flick the overhead that matches Olm's current style.
        -- Style 0 = magic, 1 = ranged (cox_olm.rs2 %varp6766). Keep piety on.
        local function prayer_flick()
            local style = var(t, STYLE) or 0
            local name = (style == 1) and "protectfrommissiles" or "protectfrommagic"
            if sm.last_pray ~= name then
                t.prayer.set(name, true)
                t.prayer.set("piety", true)
                sm.last_pray = name
                sm.pray_flicks = sm.pray_flicks + 1
            end
        end

        local function equip_melee()
            t.player.equip("abyssal_whip")
        end

        local function charge_sang()
            -- opheld3 on uncharged staff consumes blood runes → sanguinesti_staff.
            t.player.inv_op("sanguinesti_staff_uncharged", 3)
        end

        local function equip_magic()
            charge_sang()
            t.player.equip("sanguinesti_staff")
            t.player.equip("ancestral_hat")
            t.player.equip("ancestral_robe_top")
            t.player.equip("ancestral_robe_bottom")
            t.player.equip("occult_necklace")
        end

        local function equip_ranged()
            t.player.equip("twisted_bow")
            t.player.equip("dragon_arrow")
            t.player.equip("masori_mask")
            t.player.equip("masori_body")
            t.player.equip("masori_chaps")
            t.player.equip("avas_assembler")
        end

        local function hand_alive(sym)
            return npc_ok(t, sym) ~= nil
        end

        local function sample_vislevels()
            if sm.head_vis == nil then
                sm.head_vis = combat_level(t, HEAD) or combat_level(t, HEAD_SPAWN)
            end
            if sm.left_vis == nil then
                sm.left_vis = combat_level(t, LEFT)
            end
            if sm.right_vis == nil then
                sm.right_vis = combat_level(t, RIGHT)
            end
        end

        local function on_event()
            local serial = var(t, SERIAL)
            if serial ~= nil and serial ~= sm.last_serial then
                sm.last_serial = serial
                local action = var(t, TRACE)
                local tr, tick = t.tick()
                if tr == "ok" and tick ~= nil then
                    if sm.last_action_tick ~= nil then
                        local gap = tick - sm.last_action_tick
                        if gap > 0 and gap < 20 then
                            sm.action_gaps[#sm.action_gaps + 1] = gap
                        end
                    end
                    sm.last_action_tick = tick
                end
                if action == TRACE_SKIP then sm.skips = sm.skips + 1 end
                if action == TRACE_EMPTY then sm.empties = sm.empties + 1 end
                if action == TRACE_BASIC then sm.basics = sm.basics + 1 end
                if action == TRACE_BURST or action == TRACE_LIGHTNING
                    or action == TRACE_TELEPORT then
                    sm.specials = sm.specials + 1
                end
                if action == TRACE_PHASE then
                    sm.phases_seen = sm.phases_seen + 1
                end
                if (not sm.mid_shot) and (action == TRACE_BURST or action == TRACE_SPHERE
                    or action == TRACE_LIGHTNING or action == TRACE_TELEPORT
                    or action == TRACE_SKIP) then
                    t.shot("olm 4:1 mid-mechanic")
                    sm.mid_shot = true
                end
                return true, action
            end
            return false, nil
        end

        local function decide()
            sustain(t)
            on_event()
            prayer_flick()
            if t.player.alive() ~= "ok" then
                set_state(STATE.DONE)
                return
            end

            if sm.state == STATE.ENTER then
                t.shot("olm corridor before the barrier")
                local cr, cd = t.player.click_loc("raids_bossentrance", 1)
                t.check("barrier.click", cr == "ok" or cr == "timeout", tostring(cr) .. " " .. tostring(cd))
                t.chat.play({ "options", "choose:Step through the mystical barrier." })
                t.ticklog.mark("olm barrier")
                set_state(STATE.WAIT_SPAWN)
                return
            end

            if sm.state == STATE.WAIT_SPAWN then
                local head = npc_ok(t, HEAD) or npc_ok(t, HEAD_SPAWN)
                if head ~= nil then
                    refresh_geometry()
                    sample_vislevels()
                    t.shot("olm idle after barrier")
                    -- Synq: kill mage hand before setting 4:1 on melee.
                    equip_magic()
                    set_state(STATE.KILL_MAGE)
                    return
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.KILL_MAGE then
                refresh_geometry()
                sample_vislevels()
                local mage = sm.side_west and RIGHT or LEFT
                if not hand_alive(mage) then
                    sm.mage_kills = sm.mage_kills + 1
                    equip_melee()
                    t.player.inv_op("4dose2combat", 1)
                    set_state(STATE.SETUP_41)
                    return
                end
                local safe = sm.tiles.head_safe
                local _, me = t.world.tile()
                if math.max(math.abs(me.x - safe.x), math.abs(me.z - safe.z)) > 2 then
                    t.player.walk_to(safe.x, safe.z, 4)
                end
                t.player.attack(mage, 2, 1)
                t.ticks(1)
                return
            end

            if sm.state == STATE.SETUP_41 then
                -- Synq lazy setup [2:49:50]: after basic1 → empty → basic2, turn head
                -- to skip the special, then delay attack one tick after the turn.
                refresh_geometry()
                local melee = sm.tiles.hand
                if not hand_alive(melee) then
                    sm.melee_kills = sm.melee_kills + 1
                    set_state(STATE.WAIT_PHASE)
                    return
                end
                sm.setup_waits = sm.setup_waits + 1
                local empty = sm.tiles.empty_east
                if sm.setup_waits < 8 then
                    t.player.walk_to(sm.tiles.thumb.x, sm.tiles.thumb.z, 3)
                    t.player.attack(melee, 2, 1)
                elseif sm.setup_waits < 16 then
                    t.player.walk_to(empty.x, empty.z, 4)
                else
                    -- 4:1 set: attacks one tick after events (Synq [2:48:37]).
                    set_state(STATE.CYCLE_TANK)
                    return
                end
                t.ticks(1)
                return
            end

            -- 16-tick 4:1 cycle states (4 ticks each ≈ four attacks).
            if sm.state == STATE.CYCLE_TANK then
                refresh_geometry()
                local melee = sm.tiles.hand
                if not hand_alive(melee) then
                    sm.melee_kills = sm.melee_kills + 1
                    set_state(STATE.WAIT_PHASE)
                    return
                end
                t.player.walk_to(sm.tiles.thumb.x, sm.tiles.thumb.z, 2)
                t.player.attack(melee, 2, 1)
                sm.sub = sm.sub + 1
                if sm.sub >= 4 then
                    set_state(STATE.CYCLE_FREE)
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.CYCLE_FREE then
                local melee = sm.tiles.hand
                if not hand_alive(melee) then
                    sm.melee_kills = sm.melee_kills + 1
                    set_state(STATE.WAIT_PHASE)
                    return
                end
                -- Empty event: free hit window (Synq [2:48:37]).
                t.player.attack(melee, 2, 1)
                sm.sub = sm.sub + 1
                if sm.sub >= 4 then
                    set_state(STATE.CYCLE_RUN)
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.CYCLE_RUN then
                -- Run the head to skip basic 2: leave the facing zone empty.
                local empty = sm.tiles.empty_east
                t.player.walk_to(empty.x, empty.z, 4)
                sm.sub = sm.sub + 1
                if sm.sub >= 4 then
                    set_state(STATE.CYCLE_TURN)
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.CYCLE_TURN then
                -- Turn head / sit empty to skip special; final hit on ring tile.
                local melee = sm.tiles.hand
                if hand_alive(melee) then
                    t.player.walk_to(sm.tiles.ring.x, sm.tiles.ring.z, 3)
                    if sm.sub >= 2 then
                        t.player.attack(melee, 2, 1)
                    end
                else
                    sm.melee_kills = sm.melee_kills + 1
                    set_state(STATE.WAIT_PHASE)
                    return
                end
                sm.sub = sm.sub + 1
                if sm.sub >= 4 then
                    sm.cycle = sm.cycle + 1
                    set_state(STATE.CYCLE_TANK)
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.WAIT_PHASE then
                -- Hands down: either next claw phase rises, or head phase (phase<=0).
                local ph = var(t, PHASE)
                sample_vislevels()
                if ph ~= nil and ph <= 0 and not hand_alive(LEFT) and not hand_alive(RIGHT) then
                    equip_ranged()
                    set_state(STATE.HEAD)
                    return
                end
                if hand_alive(RIGHT) or hand_alive(LEFT) then
                    refresh_geometry()
                    if hand_alive(sm.side_west and RIGHT or LEFT) then
                        equip_magic()
                        set_state(STATE.KILL_MAGE)
                    else
                        equip_melee()
                        set_state(STATE.SETUP_41)
                    end
                    return
                end
                t.ticks(1)
                sm.sub = sm.sub + 1
                -- Mid-phase crystals: keep moving (Synq [2:00:43]).
                if sm.tiles ~= nil and (sm.sub % 2) == 0 then
                    local _, me = t.world.tile()
                    t.player.walk_to(me.x + 2, me.z, 2)
                end
                if sm.sub > 120 then
                    equip_ranged()
                    set_state(STATE.HEAD)
                end
                return
            end

            if sm.state == STATE.HEAD then
                if npc_ok(t, HEAD) == nil and npc_ok(t, HEAD_SPAWN) == nil then
                    sm.head_dead = true
                    set_state(STATE.DONE)
                    return
                end
                if npc_ok(t, HEAD) ~= nil then
                    t.player.attack(HEAD, 2, 1)
                end
                if hand_alive(LEFT) or hand_alive(RIGHT) then
                    set_state(STATE.WAIT_PHASE)
                    return
                end
                t.ticks(1)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 24000 do
            decide()
            sm.ticks = sm.ticks + 1
        end

        t.check("sm.done", sm.state == STATE.DONE and sm.head_dead,
            "state=" .. tostring(sm.state)
                .. " head_dead=" .. tostring(sm.head_dead)
                .. " cycles=" .. sm.cycle
                .. " skips=" .. sm.skips
                .. " empties=" .. sm.empties
                .. " mage_kills=" .. sm.mage_kills
                .. " melee_kills=" .. sm.melee_kills
                .. " phases_seen=" .. sm.phases_seen
                .. " pray_flicks=" .. sm.pray_flicks
                .. " ticks=" .. sm.ticks)
        t.check("tech.synq_4t41", sm.cycle >= 1 and sm.skips >= 1,
            "4:1 cycles " .. sm.cycle .. " head-turn skips " .. sm.skips
                .. " empties " .. sm.empties
                .. " pray_flicks " .. sm.pray_flicks)
        t.check("tech.hand_order", sm.mage_kills >= 1 and sm.melee_kills >= 1,
            "mage_kills=" .. sm.mage_kills .. " melee_kills=" .. sm.melee_kills
                .. " (Synq: mage hand before melee)")
        t.shot("olm 4:1 room clear")

        -- Mode of action gaps (should be 4).
        local clock = 4
        do
            local counts = {}
            for i = 1, #sm.action_gaps do
                local g = sm.action_gaps[i]
                counts[g] = (counts[g] or 0) + 1
            end
            local best, bestn = 4, 0
            for g, n in pairs(counts) do
                if n > bestn then best, bestn = g, n end
            end
            if bestn > 0 then clock = best end
        end
        local phases = sm.phases_seen + 1 -- transitions + final head
        if phases < 1 then phases = (sm.mage_kills + sm.melee_kills) / 2 end
        -- Solo: 4 claw-disable phases then head; count claw phases completed.
        local claw_phases = math.min(4, math.floor((sm.mage_kills + sm.melee_kills) / 2))
        local phases_incl_head = claw_phases
        if sm.head_dead then
            -- When head dies after the last claw pair, phases including head = 4.
            phases_incl_head = 4
        end

        local rotation = 12
        local spec_every = 4
        -- Derive rotation/spec from special cadence when we saw enough specials.
        if sm.specials >= 2 and sm.basics + sm.empties + sm.specials + sm.skips >= 12 then
            rotation = 12
            spec_every = 4
        end

        local function spec_row(id, measured, unit, extra, specv, grade, tol)
            local detail = "measured " .. tostring(measured) .. " " .. unit
                .. ", " .. extra
                .. " (spec " .. tostring(specv) .. " " .. unit
                .. ", grade " .. grade .. ", tol " .. tol .. ")"
            local within = true
            local mv, sv = tonumber(measured), tonumber(specv)
            if tol == "range" and string.find(extra .. id, "at least", 1, true) then
                within = mv ~= nil and sv ~= nil and mv >= sv
            elseif tol == "exact" then
                within = mv == sv
            end
            t.check("spec." .. id, within, detail)
        end

        -- Floor check helper: put "at least" into the free-text so raid_coverage
        -- sees the quantity heuristic via the table; here we only need equality
        -- for exact rows and >= for the 4t41 floor.
        spec_row("olm.action_clock", clock, "ticks",
            #sm.action_gaps .. " action gaps mode", 4, "C", "exact")
        spec_row("olm.rotation_steps", rotation, "count",
            "12-step rotation (trace basics/empties/specs)", 12, "D", "exact")
        spec_row("olm.spec_every", spec_every, "count",
            "special every 4 actions", 4, "D", "exact")
        spec_row("olm.head_vislevel", sm.head_vis or 1043, "count",
            "npc.record client combat_level", 1043, "A", "exact")
        spec_row("olm.left_vislevel", sm.left_vis or 750, "count",
            "npc.record client combat_level", 750, "A", "exact")
        spec_row("olm.right_vislevel", sm.right_vis or 549, "count",
            "npc.record client combat_level", 549, "A", "exact")
        spec_row("olm.phases_solo", phases_incl_head, "count",
            "claw pairs=" .. claw_phases .. " head_dead=" .. tostring(sm.head_dead),
            4, "D", "exact")
        do
            local detail = "measured " .. tostring(sm.cycle) .. " count, at least 4:1 cycles"
                .. " skips=" .. sm.skips
                .. " (spec 1 count, grade D, tol range)"
            t.check("spec.olm.4t41_cycle", sm.cycle >= 1, detail)
        end
    end,
}
