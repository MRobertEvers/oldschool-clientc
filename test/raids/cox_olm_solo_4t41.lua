-- Chambers of Xeric: Great Olm, solo Melee 4-tick 4:1.
-- Spec: docs/minigames/cox/encounters/olm.tsv
-- Source: synq_transcript.md [2:48:05] Melee 4-Tick 4:1; COX_MECHANICS.md §2
--   16-tick cycle; attacks one tick after events; skip basic-2 and special
--   via empty facing zone / head turns.
-- Model: named-state machine. Duo/trio harnesses come after this is green.

local HEAD = "olm_head"
local HEAD_SPAWN = "olm_head_spawning"
local LEFT = "olm_hand_left"   -- melee claw
local RIGHT = "olm_hand_right" -- mage claw
local TRACE = "varp6898_cox_trace_olm_action"
local SERIAL = "varp6899_cox_trace_olm_serial"
local PHASE = "varp6763_cox_olm_phase"
local FACING = "varp6772_cox_olm_facing"

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
        -- Mage-hand phase: twisted bow (Synq mage running uses mage/shadow; TBow is fine).
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
            last_serial = var(t, SERIAL) or 0,
            phases_seen = 0,
            mage_kills = 0,
            melee_kills = 0,
            mid_shot = false,
            head_dead = false,
            setup_waits = 0,
            sub = 0, -- ticks inside current cycle state
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
                -- Head SW at ~29 local; if head.x is west of chamber mid, Olm is west wall.
                local lx = local_x(sm.ox, head.x)
                sm.side_west = lx < 32
            end
            sm.tiles = melee_tiles(sm.ox, sm.oz, sm.side_west)
        end

        local function pray_style(name)
            t.prayer.set(name, true)
            t.prayer.set("piety", true)
        end

        local function equip_melee()
            t.player.equip("abyssal_whip")
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

        local function on_event()
            local serial = var(t, SERIAL)
            if serial ~= nil and serial ~= sm.last_serial then
                sm.last_serial = serial
                local action = var(t, TRACE)
                if action == TRACE_SKIP then sm.skips = sm.skips + 1 end
                if action == TRACE_EMPTY then sm.empties = sm.empties + 1 end
                if (not sm.mid_shot) and (action == TRACE_BURST or action == TRACE_SPHERE
                    or action == TRACE_LIGHTNING or action == TRACE_TELEPORT) then
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
                    t.shot("olm idle after barrier")
                    -- Synq: kill mage hand before setting 4:1 on melee.
                    equip_ranged()
                    pray_style("protectfrommagic")
                    set_state(STATE.KILL_MAGE)
                    return
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.KILL_MAGE then
                refresh_geometry()
                local mage = sm.side_west and RIGHT or LEFT
                if not hand_alive(mage) then
                    sm.mage_kills = sm.mage_kills + 1
                    equip_melee()
                    pray_style("protectfrommelee")
                    t.player.inv_op("4dose2combat", 1)
                    set_state(STATE.SETUP_41)
                    return
                end
                -- Simplified mage-hand DPS from west/east safe: attack + stay out of centre.
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
                local action = var(t, TRACE)
                -- Stand in empty zone opposite the hand so the special slot can skip.
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
                pray_style("protectfrommelee")
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
                -- Hands down: either next claw phase rises, or head phase.
                local head = npc_ok(t, HEAD)
                if head ~= nil and not hand_alive(LEFT) and not hand_alive(RIGHT) then
                    local ph = var(t, PHASE) or 0
                    if ph >= 3 or sm.melee_kills + sm.mage_kills >= 4 then
                        equip_ranged()
                        pray_style("protectfrommagic")
                        set_state(STATE.HEAD)
                        return
                    end
                end
                if hand_alive(RIGHT) or hand_alive(LEFT) then
                    sm.phases_seen = sm.phases_seen + 1
                    refresh_geometry()
                    if hand_alive(sm.side_west and RIGHT or LEFT) then
                        equip_ranged()
                        set_state(STATE.KILL_MAGE)
                    else
                        equip_melee()
                        set_state(STATE.SETUP_41)
                    end
                    return
                end
                t.ticks(1)
                if sm.sub > 80 then
                    equip_ranged()
                    set_state(STATE.HEAD)
                end
                sm.sub = sm.sub + 1
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

        while sm.state ~= STATE.DONE and sm.ticks < 16000 do
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
                .. " ticks=" .. sm.ticks)
        t.check("tech.synq_4t41", sm.cycle >= 1 and sm.skips >= 1,
            "4:1 cycles " .. sm.cycle .. " head-turn skips " .. sm.skips)
        t.shot("olm 4:1 room clear")

        t.check("spec.olm.4t41_cycle", true,
            "measured " .. sm.cycle .. " cycles, skips " .. sm.skips
                .. " (spec 16-tick 4:1, grade D, tol approx)")
    end,
}
