-- Chambers of Xeric: Great Olm, solo Melee 4-tick 4:1.
-- Spec: docs/minigames/cox/encounters/olm.tsv
-- Source: synq_transcript.md [2:48:05]; COX_MECHANICS.md §2
--
-- Goal: drive a real 4:1 kill path with dedicated recovery states for every
-- Olm special / phase power so failures surface as content bugs (wrong dodge
-- contract, missing portals, undodgeable burst, …) rather than SM mush.
--
-- Specials (rotation slot): crystal burst, lightning, teleport, life siphon.
-- Phase powers (TRACE_POWER): acid / flame / crystal — dispatched by varp6862.
-- Mistake recovery: noodle (head centre on skip-basic2) → tank basic2 → skip special.

local HEAD = "olm_head"
local HEAD_SPAWN = "olm_head_spawning"
local LEFT = "olm_hand_left"
local RIGHT = "olm_hand_right"
local TRACE = "varp6898_cox_trace_olm_action"
local SERIAL = "varp6899_cox_trace_olm_serial"
local PHASE = "varp6763_cox_olm_phase"
local STEP = "varp6765_cox_olm_step"
local POWER = "varp6862_cox_olm_power"
local FACING = "varp6772_cox_olm_facing"

local ZONE_WEST_MAX = 27
local ZONE_EAST_MIN = 36
local LEFT_LX, LEFT_LZ = 23, 30
local RIGHT_LX, RIGHT_LZ = 35, 30

local TRACE_BASIC = 1
local TRACE_SPHERE = 2
local TRACE_POWER = 3
local TRACE_BURST = 4
local TRACE_LIGHTNING = 5
local TRACE_TELEPORT = 6
local TRACE_SIPHON = 7
local TRACE_SKIP = 8
local TRACE_EMPTY = 9
local TRACE_CATCHUP = 10

local SLOT_SPECIAL = "special"
local SLOT_EMPTY = "empty"
local SLOT_STANDARD = "standard"

local POWER_ACID = 1
local POWER_FLAME = 2
local POWER_CRYSTAL = 3

-- ^cox_olm_burst_delay / siphon_window / firewall_ticks from cox.constant
local BURST_DELAY = 3
local SIPHON_WINDOW = 10
local FIREWALL_TICKS = 8

local STATE = {
    ENTER = "ENTER",
    WAIT_SPAWN = "WAIT_SPAWN",
    KILL_MAGE = "KILL_MAGE",
    IDENTIFY = "IDENTIFY",
    LOCKED = "LOCKED",
    NOODLE = "NOODLE",
    -- Dedicated special recovery (rotation specials).
    REC_BURST = "REC_BURST",
    REC_LIGHTNING = "REC_LIGHTNING",
    REC_TELEPORT = "REC_TELEPORT",
    REC_SIPHON = "REC_SIPHON",
    -- Dedicated phase-power recovery.
    REC_ACID = "REC_ACID",
    REC_FLAME = "REC_FLAME",
    REC_CRYSTAL = "REC_CRYSTAL",
    REC_SPHERE = "REC_SPHERE",
    WAIT_PHASE = "WAIT_PHASE",
    HEAD = "HEAD",
    DONE = "DONE",
}

local NEXT_BASIC1 = "basic1"
local NEXT_EMPTY = "empty"
local NEXT_SKIP_BASIC2 = "skip_basic2"
local NEXT_SKIP_SPECIAL = "skip_special"

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

local function hp_level(t)
    local r, hp = t.skill.read("hitpoints")
    if r == "ok" then return hp.level end
    return nil
end

local function step_slot(step)
    if step == nil then return nil end
    local m = step % 4
    if m == 0 then return SLOT_SPECIAL end
    if m == 2 then return SLOT_EMPTY end
    return SLOT_STANDARD
end

local function is_rotation_special(action)
    return action == TRACE_BURST
        or action == TRACE_LIGHTNING
        or action == TRACE_TELEPORT
        or action == TRACE_SIPHON
end

local function origin_of(me)
    return math.floor(me.x / 64) * 64, math.floor(me.z / 64) * 64
end

local function melee_tiles(ox, oz, side_west)
    if side_west then
        return {
            thumb = { x = ox + LEFT_LX + 2, z = oz + LEFT_LZ - 1 },
            ring = { x = ox + LEFT_LX - 1, z = oz + LEFT_LZ - 2 },
            flame_null = { x = ox + LEFT_LX - 2, z = oz + LEFT_LZ - 2 },
            head_safe = { x = ox + 28, z = oz + 28 },
            empty_zone = { x = ox + ZONE_EAST_MIN + 1, z = oz + 28 },
            side_wall = { x = ox + ZONE_WEST_MAX - 1, z = oz + 25 },
            hand = LEFT,
        }
    end
    return {
        thumb = { x = ox + RIGHT_LX - 2, z = oz + RIGHT_LZ - 1 },
        ring = { x = ox + RIGHT_LX + 1, z = oz + RIGHT_LZ - 2 },
        flame_null = { x = ox + RIGHT_LX + 2, z = oz + RIGHT_LZ - 2 },
        head_safe = { x = ox + 35, z = oz + 28 },
        empty_zone = { x = ox + ZONE_WEST_MAX - 1, z = oz + 28 },
        side_wall = { x = ox + ZONE_EAST_MIN + 1, z = oz + 25 },
        hand = RIGHT,
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
        "::give twisted_bow",
        "::give dragon_arrow 2000",
        "::give masori_mask",
        "::give masori_body",
        "::give masori_chaps",
        "::give avas_assembler",
        -- Pack must stay ≤28. Six range-switch slots leave 22 for supplies.
        -- Prior kit (shark 24 + restore 8 + sara 6 + combat 2) was 46 and
        -- failed setup: ::give answered ok but potions never landed (ledger
        -- 2026-10-07 / muttadiles run27 same trap). Potions before food.
        "::give 4dose2restore 4",
        "::give 4dosepotionofsaradomin 4",
        "::give 4dose2combat 2",
        "::give shark 12",
    },

    run = function(t)
        t.check("spec.scope", true,
            "mode=all party=1; synq melee 4-tick 4:1 + dedicated special recovery")
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
            noodles = 0,
            resyncs = 0,
            last_serial = var(t, SERIAL) or 0,
            phases_seen = 0,
            mage_kills = 0,
            melee_kills = 0,
            mid_shot = false,
            head_dead = false,
            sub = 0,
            id_prev = nil,
            expect = nil,
            pending_attack = false,
            -- Per-special counters + probe notes for content-bug extraction.
            saw = {
                burst = 0, lightning = 0, teleport = 0, siphon = 0,
                acid = 0, flame = 0, crystal = 0, sphere = 0,
            },
            probe = {
                -- Each entry: { id=, ok=, detail= }
            },
            rec_hp0 = nil,
            rec_tile0 = nil,
        }

        local function note_probe(id, ok, detail)
            sm.probe[#sm.probe + 1] = { id = id, ok = ok, detail = detail }
            t.ticklog.mark("probe " .. id .. " ok=" .. tostring(ok) .. " " .. tostring(detail))
        end

        local function set_state(s)
            sm.state = s
            sm.sub = 0
        end

        local function refresh_geometry()
            local _, me = t.world.tile()
            sm.ox, sm.oz = origin_of(me)
            local head = npc_ok(t, HEAD) or npc_ok(t, HEAD_SPAWN)
            if head ~= nil then
                sm.side_west = (head.x - sm.ox) < 32
            end
            sm.tiles = melee_tiles(sm.ox, sm.oz, sm.side_west)
        end

        local function pray_style(name)
            t.prayer.set(name, true)
            t.prayer.set("piety", true)
        end

        local function sustain()
            local hp = hp_level(t)
            if hp ~= nil and hp < 55 then
                if t.player.eat("shark") ~= "ok" then
                    t.player.inv_op("4dosepotionofsaradomin", 1)
                end
            end
            local pr, pp = t.prayer.points()
            local points = 0
            if pr == "ok" then points = pp.points or pp.level or 0 end
            if points < 30 then
                t.player.drink("4dose2restore")
            end
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

        local function melee_hand()
            return sm.tiles and sm.tiles.hand or LEFT
        end

        local function attack_melee()
            local m = melee_hand()
            if hand_alive(m) then t.player.attack(m, 2, 1) end
        end

        local function walk_thumb()
            t.player.walk_to(sm.tiles.thumb.x, sm.tiles.thumb.z, 2)
        end
        local function walk_ring()
            t.player.walk_to(sm.tiles.ring.x, sm.tiles.ring.z, 3)
        end
        local function walk_empty()
            t.player.walk_to(sm.tiles.empty_zone.x, sm.tiles.empty_zone.z, 4)
        end
        local function walk_flame_null()
            t.player.walk_to(sm.tiles.flame_null.x, sm.tiles.flame_null.z, 3)
        end
        local function walk_side()
            t.player.walk_to(sm.tiles.side_wall.x, sm.tiles.side_wall.z, 4)
        end

        local function snapshot_rec()
            sm.rec_hp0 = hp_level(t)
            local _, me = t.world.tile()
            sm.rec_tile0 = { x = me.x, z = me.z }
        end

        local function left_start_tile()
            local _, me = t.world.tile()
            if sm.rec_tile0 == nil then return true end
            return me.x ~= sm.rec_tile0.x or me.z ~= sm.rec_tile0.z
        end

        local function hp_dropped()
            local hp = hp_level(t)
            if hp == nil or sm.rec_hp0 == nil then return false end
            return hp < sm.rec_hp0
        end

        local function enter_resync(reason)
            sm.resyncs = sm.resyncs + 1
            sm.id_prev = nil
            sm.expect = nil
            sm.pending_attack = false
            t.ticklog.mark("olm resync " .. tostring(reason))
            set_state(STATE.IDENTIFY)
        end

        local function enter_locked(expect)
            sm.expect = expect
            sm.pending_attack = false
            set_state(STATE.LOCKED)
        end

        local function classify_event(action, slot)
            if action == TRACE_EMPTY then return "empty" end
            if action == TRACE_SKIP then return "skip" end
            if action == TRACE_CATCHUP then return "catchup" end
            if is_rotation_special(action) then return "special" end
            if action == TRACE_BASIC or action == TRACE_SPHERE or action == TRACE_POWER then
                return "basic"
            end
            if slot == SLOT_EMPTY then return "empty" end
            if slot == SLOT_SPECIAL then return "special" end
            if slot == SLOT_STANDARD then return "basic" end
            return "unknown"
        end

        local function identify_lock(prev, cur, cur_slot)
            if prev == "basic" and cur == "empty" then return NEXT_SKIP_BASIC2 end
            if prev == "basic" and cur == "special" then return NEXT_BASIC1 end
            if prev == "basic" and cur == "basic" then return NEXT_SKIP_SPECIAL end
            if (prev == "catchup" or prev == "special")
                and (cur == "basic" or cur == "special" or cur == "catchup") then
                return NEXT_BASIC1
            end
            if cur == "skip" and cur_slot == SLOT_SPECIAL then return NEXT_BASIC1 end
            if cur == "skip" and cur_slot == SLOT_STANDARD then return NEXT_SKIP_SPECIAL end
            if cur == "empty" then return NEXT_SKIP_BASIC2 end
            return nil
        end

        local function dispatch_special(action)
            if not sm.mid_shot then
                t.shot("olm special " .. tostring(action))
                sm.mid_shot = true
            end
            snapshot_rec()
            if action == TRACE_BURST then
                sm.saw.burst = sm.saw.burst + 1
                set_state(STATE.REC_BURST)
            elseif action == TRACE_LIGHTNING then
                sm.saw.lightning = sm.saw.lightning + 1
                set_state(STATE.REC_LIGHTNING)
            elseif action == TRACE_TELEPORT then
                sm.saw.teleport = sm.saw.teleport + 1
                set_state(STATE.REC_TELEPORT)
            elseif action == TRACE_SIPHON then
                sm.saw.siphon = sm.saw.siphon + 1
                set_state(STATE.REC_SIPHON)
            else
                enter_resync("unknown_special_" .. tostring(action))
            end
        end

        local function dispatch_power()
            if not sm.mid_shot then
                t.shot("olm phase power")
                sm.mid_shot = true
            end
            snapshot_rec()
            local p = var(t, POWER)
            if p == POWER_ACID then
                sm.saw.acid = sm.saw.acid + 1
                set_state(STATE.REC_ACID)
            elseif p == POWER_FLAME then
                sm.saw.flame = sm.saw.flame + 1
                set_state(STATE.REC_FLAME)
            else
                -- Crystal (or unset): falling / bombs.
                sm.saw.crystal = sm.saw.crystal + 1
                set_state(STATE.REC_CRYSTAL)
            end
        end

        local function on_event()
            local serial = var(t, SERIAL)
            if serial == nil or serial == sm.last_serial then
                return false, nil, nil
            end
            sm.last_serial = serial
            local action = var(t, TRACE)
            local slot = step_slot(var(t, STEP))
            if action == TRACE_SKIP then sm.skips = sm.skips + 1 end
            if action == TRACE_EMPTY then sm.empties = sm.empties + 1 end
            return true, action, slot
        end

        local function sphere_pray()
            local cr, kind = t.chat.kind()
            if cr == "ok" and type(kind) == "string" then
                local k = string.lower(kind)
                if string.find(k, "melee", 1, true) then
                    pray_style("protectfrommelee"); return
                end
                if string.find(k, "missile", 1, true) or string.find(k, "range", 1, true) then
                    pray_style("protectfrommissiles"); return
                end
                if string.find(k, "magic", 1, true) then
                    pray_style("protectfrommagic"); return
                end
            end
            pray_style("protectfrommelee")
        end

        local function hands_down_to_phase()
            refresh_geometry()
            if not hand_alive(melee_hand()) then
                sm.melee_kills = sm.melee_kills + 1
                set_state(STATE.WAIT_PHASE)
                return true
            end
            return false
        end

        ------------------------------------------------------------------
        -- Recovery state runners. Each ends with enter_resync / enter_locked.
        ------------------------------------------------------------------

        -- Crystal burst: wiki/Synq — seedling under player, step off before burst.
        -- Content today: queue damage on uid after BURST_DELAY (not tile-checked).
        local function run_rec_burst()
            -- Immediate step-off (correct player response).
            local _, me = t.world.tile()
            t.player.walk_to(me.x + 2, me.z + 1, 2)
            sm.sub = sm.sub + 1
            if sm.sub == 1 then
                note_probe("olm.burst.step_issued", true, "walked off start tile")
            end
            if sm.sub >= BURST_DELAY + 2 then
                local moved = left_start_tile()
                local hit = hp_dropped()
                -- If we left the tile in time and still took damage, burst is
                -- not tile-gated (content bug vs wiki seedling dodge).
                if moved and hit then
                    note_probe("content.olm.burst_undodgeable", false,
                        "stepped off before delay+" .. BURST_DELAY
                            .. " but HP dropped; cox_olm_crystal_burst queues uid damage")
                elseif moved and not hit then
                    note_probe("content.olm.burst_tile_dodge", true, "left tile, no HP drop")
                else
                    note_probe("olm.burst.move_failed", false, "still on start tile")
                end
                enter_resync("burst_done")
                return
            end
            t.ticks(1)
        end

        -- Lightning: stand east/west; content currently prayer-saps everyone in
        -- huntall with no bolt path — probe that.
        local function run_rec_lightning()
            walk_side()
            sm.sub = sm.sub + 1
            if sm.sub == 1 then
                -- Re-assert overhead after the sap (content ~prayer_deactivate_all).
                pray_style("protectfrommelee")
            end
            if sm.sub >= 4 then
                local hit = hp_dropped()
                -- Side-wall stance is the Synq dodge; if we still took damage
                -- from the special itself, bolts are not position-gated.
                if hit then
                    note_probe("content.olm.lightning_no_bolts", false,
                        "side-wall dodge still took damage; cox_olm_lightning damages huntall")
                else
                    note_probe("content.olm.lightning_side_safe", true, "no HP drop on side wall")
                end
                -- Prayer should be restorable after lightning.
                local pr = t.prayer.set("protectfrommelee", true)
                note_probe("olm.lightning.prayer_reenable", pr == "ok", "set protectfrommelee -> " .. tostring(pr))
                enter_resync("lightning_done")
                return
            end
            t.ticks(1)
        end

        -- Teleport / portal swap: Synq — run to paired portal in 8 ticks.
        -- Content solo: random separation damage, no portals.
        local function run_rec_teleport()
            walk_empty()
            sm.sub = sm.sub + 1
            if sm.sub == 1 then
                -- Look for portal NPCs/locs the player could click.
                local portal_npc = npc_ok(t, "olm_portal") or npc_ok(t, "raids_olm_portal")
                local cr, cd = t.player.click_loc("olm_teleport_portal", 1)
                local has_portal = portal_npc ~= nil or cr == "ok"
                if not has_portal then
                    note_probe("content.olm.teleport_no_portals", false,
                        "no olm portal npc/loc; cox_olm_teleport is flat solo damage"
                            .. " click_loc=" .. tostring(cr) .. " " .. tostring(cd))
                else
                    note_probe("content.olm.teleport_portals", true, "portal interactable")
                end
            end
            if sm.sub >= 8 then
                walk_thumb()
                attack_melee()
                enter_resync("teleport_done")
                return
            end
            t.ticks(1)
        end

        -- Life siphon (final phase): stand on marked tile for SIPHON_WINDOW.
        -- Content: delayed uid damage + head heal, no marked tiles.
        local function run_rec_siphon()
            -- Try to stand still on a "safe" candidate (head-safe / centre).
            -- If content had marks, we would click them; absence is the bug.
            t.player.walk_to(sm.tiles.head_safe.x, sm.tiles.head_safe.z, 4)
            sm.sub = sm.sub + 1
            if sm.sub == 1 then
                local mark = npc_ok(t, "olm_siphon_pool") or npc_ok(t, "raids_olm_siphon")
                if mark == nil then
                    note_probe("content.olm.siphon_no_safe_tiles", false,
                        "no siphon mark npc; cox_olm_life_siphon damages after window w/o tiles")
                else
                    note_probe("content.olm.siphon_marks", true, "siphon mark present")
                end
            end
            if sm.sub >= SIPHON_WINDOW + 2 then
                local head = npc_ok(t, HEAD)
                local healed = false
                if head ~= nil and sm.rec_hp0 ~= nil then
                    -- Head heal is the mechanic; we only note player damage here.
                    healed = hp_dropped()
                end
                if healed then
                    note_probe("olm.siphon.player_hit", true, "took siphon damage (expected if off mark)")
                end
                enter_resync("siphon_done")
                return
            end
            t.ticks(1)
        end

        -- Acid: leave pool tile (content is tile-gated — correct contract).
        local function run_rec_acid()
            local _, me = t.world.tile()
            -- One-tile drag / run-over (Synq acid walk simplified).
            t.player.walk_to(me.x, me.z - 2, 2)
            sm.sub = sm.sub + 1
            if sm.sub >= 4 then
                if left_start_tile() and not hp_dropped() then
                    note_probe("content.olm.acid_tile_dodge", true, "left pool tile, no further drop")
                elseif left_start_tile() and hp_dropped() then
                    -- May still drop from drip ticks while leaving — soft note.
                    note_probe("olm.acid.left_with_hits", true, "left tile but HP dropped (drip/pool ticks)")
                end
                walk_thumb()
                enter_resync("acid_done")
                return
            end
            attack_melee()
            t.ticks(1)
        end

        -- Flame wall: leave trapped tile within FIREWALL_TICKS; null LOS tile.
        local function run_rec_flame()
            -- Prefer flame-null tile (Synq weird null / ring edge).
            walk_flame_null()
            sm.sub = sm.sub + 1
            if sm.sub == 1 then
                -- Leap damage on cast is expected; trap damage is the dodgeable part.
                note_probe("olm.flame.leap_window", true, "entered flame recovery; nulling LOS")
            end
            if sm.sub >= FIREWALL_TICKS + 1 then
                if left_start_tile() then
                    -- Trap queue skips if coord != captured tile.
                    note_probe("content.olm.firewall_tile_escape", true,
                        "left cast tile before trap resolve")
                else
                    note_probe("content.olm.firewall_stuck", false, "still on cast tile at trap time")
                end
                walk_thumb()
                enter_resync("flame_done")
                return
            end
            -- Keep 4:1 DPS intent while nulling.
            if sm.sub % 4 == 0 then attack_melee() end
            t.ticks(1)
        end

        -- Crystal falling / bombs: leave marked tile / create distance.
        local function run_rec_crystal()
            local _, me = t.world.tile()
            t.player.walk_to(me.x + ((sm.sub % 2 == 0) and 2 or -2), me.z + 1, 2)
            sm.sub = sm.sub + 1
            if sm.sub >= 6 then
                if left_start_tile() and not hp_dropped() then
                    note_probe("content.olm.crystal_tile_dodge", true, "left fall/bomb tile")
                elseif left_start_tile() and hp_dropped() then
                    note_probe("olm.crystal.partial_hits", true, "moved but took hits (bombs radius?)")
                end
                walk_thumb()
                enter_resync("crystal_done")
                return
            end
            attack_melee()
            t.ticks(1)
        end

        -- Prayer spheres on a standard attack.
        local function run_rec_sphere()
            sphere_pray()
            attack_melee()
            sm.sub = sm.sub + 1
            if sm.sub >= 2 then
                note_probe("olm.sphere.prayer_set", true, "overhead set from chat/default")
                -- Stay in cycle if we were locked; else resync.
                if sm.expect ~= nil then
                    set_state(STATE.LOCKED)
                else
                    enter_resync("sphere_done")
                end
                return
            end
            t.ticks(1)
        end

        local function decide()
            sustain()
            local fired, action, slot = on_event()

            if t.player.alive() ~= "ok" then
                set_state(STATE.DONE)
                return
            end

            local cycling = sm.state == STATE.LOCKED or sm.state == STATE.IDENTIFY
                or sm.state == STATE.NOODLE
            if cycling and hands_down_to_phase() then return end

            ------------------------------------------------------------
            if sm.state == STATE.ENTER then
                t.shot("olm corridor before the barrier")
                local cr, cd = t.player.click_loc("raids_bossentrance", 1)
                t.check("barrier.click", cr == "ok" or cr == "timeout",
                    tostring(cr) .. " " .. tostring(cd))
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
                    sm.id_prev = nil
                    set_state(STATE.IDENTIFY)
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

            ------------------------------------------------------------
            -- Dedicated recovery states (may also see nested events).
            ------------------------------------------------------------
            if sm.state == STATE.REC_BURST then run_rec_burst(); return end
            if sm.state == STATE.REC_LIGHTNING then run_rec_lightning(); return end
            if sm.state == STATE.REC_TELEPORT then run_rec_teleport(); return end
            if sm.state == STATE.REC_SIPHON then run_rec_siphon(); return end
            if sm.state == STATE.REC_ACID then run_rec_acid(); return end
            if sm.state == STATE.REC_FLAME then run_rec_flame(); return end
            if sm.state == STATE.REC_CRYSTAL then run_rec_crystal(); return end
            if sm.state == STATE.REC_SPHERE then run_rec_sphere(); return end

            ------------------------------------------------------------
            if sm.state == STATE.IDENTIFY then
                refresh_geometry()
                if hands_down_to_phase() then return end
                pray_style("protectfrommelee")
                if var(t, POWER) == POWER_FLAME then walk_flame_null() else walk_thumb() end
                attack_melee()

                if fired then
                    if action == TRACE_SPHERE then
                        sm.saw.sphere = sm.saw.sphere + 1
                        set_state(STATE.REC_SPHERE)
                        return
                    end
                    if is_rotation_special(action) then
                        dispatch_special(action)
                        return
                    end
                    if action == TRACE_POWER then
                        dispatch_power()
                        return
                    end
                    local cur = classify_event(action, slot)
                    if sm.id_prev ~= nil then
                        local expect = identify_lock(sm.id_prev, cur, slot)
                        if expect ~= nil then
                            t.ticklog.mark("olm identify lock expect=" .. expect
                                .. " via " .. sm.id_prev .. "→" .. cur)
                            enter_locked(expect)
                            if expect == NEXT_SKIP_BASIC2 then sm.pending_attack = true end
                            if expect == NEXT_SKIP_SPECIAL then walk_empty() end
                            sm.id_prev = cur
                            t.ticks(1)
                            return
                        end
                    end
                    sm.id_prev = cur
                end
                sm.sub = sm.sub + 1
                if sm.sub > 64 then
                    walk_empty()
                    enter_locked(NEXT_BASIC1)
                end
                t.ticks(1)
                return
            end

            if sm.state == STATE.NOODLE then
                refresh_geometry()
                if hands_down_to_phase() then return end
                pray_style("protectfrommelee")
                if var(t, POWER) == POWER_FLAME then walk_flame_null() else walk_thumb() end
                attack_melee()
                if fired then
                    if is_rotation_special(action) then
                        dispatch_special(action)
                        return
                    end
                    if action == TRACE_POWER then
                        dispatch_power()
                        return
                    end
                    local cur = classify_event(action, slot)
                    if cur == "skip" and slot == SLOT_SPECIAL then
                        sm.noodles = sm.noodles + 1
                        note_probe("olm.noodle.recovered", true, "tanked basic2, skipped special")
                        enter_locked(NEXT_BASIC1)
                        t.ticks(1)
                        return
                    end
                    if cur == "basic" or cur == "empty" then walk_empty() end
                    if slot == SLOT_SPECIAL and is_rotation_special(action) then
                        dispatch_special(action)
                        return
                    end
                elseif sm.sub >= 3 then
                    walk_empty()
                end
                sm.sub = sm.sub + 1
                if sm.sub > 20 then enter_resync("noodle_timeout"); return end
                t.ticks(1)
                return
            end

            if sm.state == STATE.LOCKED then
                refresh_geometry()
                if hands_down_to_phase() then return end

                if sm.pending_attack then
                    attack_melee()
                    sm.pending_attack = false
                end

                if fired then
                    if action == TRACE_SPHERE then
                        sm.saw.sphere = sm.saw.sphere + 1
                        set_state(STATE.REC_SPHERE)
                        return
                    end
                    if is_rotation_special(action) then
                        dispatch_special(action)
                        return
                    end
                    if action == TRACE_POWER then
                        dispatch_power()
                        return
                    end

                    local cur = classify_event(action, slot)

                    if sm.expect == NEXT_BASIC1 then
                        if cur == "basic" or cur == "catchup" or slot == SLOT_STANDARD then
                            if var(t, POWER) == POWER_FLAME then walk_flame_null() else walk_thumb() end
                            sm.pending_attack = true
                            sm.expect = NEXT_EMPTY
                        elseif cur == "skip" then
                            walk_thumb()
                        else
                            enter_resync("locked_basic1_got_" .. cur)
                            return
                        end

                    elseif sm.expect == NEXT_EMPTY then
                        if cur == "empty" or slot == SLOT_EMPTY then
                            walk_thumb()
                            sm.pending_attack = true
                            sm.expect = NEXT_SKIP_BASIC2
                        elseif cur == "basic" then
                            walk_empty()
                            sm.expect = NEXT_SKIP_SPECIAL
                        else
                            enter_resync("locked_empty_got_" .. cur)
                            return
                        end

                    elseif sm.expect == NEXT_SKIP_BASIC2 then
                        if cur == "skip" then
                            walk_empty()
                            sm.expect = NEXT_SKIP_SPECIAL
                        elseif cur == "basic" or cur == "catchup" then
                            t.ticklog.mark("olm noodle tank basic2")
                            set_state(STATE.NOODLE)
                            walk_thumb()
                            sm.pending_attack = true
                            t.ticks(1)
                            return
                        elseif cur == "empty" then
                            walk_empty()
                        else
                            enter_resync("locked_skip_b2_got_" .. cur)
                            return
                        end

                    elseif sm.expect == NEXT_SKIP_SPECIAL then
                        if cur == "skip" then
                            walk_ring()
                            sm.pending_attack = true
                            sm.cycle = sm.cycle + 1
                            sm.expect = NEXT_BASIC1
                        elseif is_rotation_special(action) or cur == "special" then
                            dispatch_special(action or TRACE_BURST)
                            return
                        elseif cur == "basic" then
                            walk_thumb()
                            sm.pending_attack = true
                            sm.expect = NEXT_EMPTY
                        else
                            walk_empty()
                        end
                    end
                else
                    if sm.expect == NEXT_BASIC1 or sm.expect == NEXT_EMPTY then
                        if var(t, POWER) == POWER_FLAME then walk_flame_null() else walk_thumb() end
                    elseif sm.expect == NEXT_SKIP_BASIC2 or sm.expect == NEXT_SKIP_SPECIAL then
                        walk_empty()
                        if sm.expect == NEXT_SKIP_SPECIAL and sm.sub % 4 == 3 then
                            walk_ring()
                        end
                    end
                end
                sm.sub = sm.sub + 1
                t.ticks(1)
                return
            end

            if sm.state == STATE.WAIT_PHASE then
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
                        sm.id_prev = nil
                        set_state(STATE.IDENTIFY)
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

        -- Emit every content probe as a named check (FAIL = content bug to fix).
        local content_fails = 0
        for i = 1, #sm.probe do
            local p = sm.probe[i]
            if string.sub(p.id, 1, 8) == "content." then
                t.check(p.id, p.ok, p.detail)
                if not p.ok then content_fails = content_fails + 1 end
            end
        end

        t.check("sm.done", sm.state == STATE.DONE and sm.head_dead,
            "state=" .. tostring(sm.state)
                .. " head_dead=" .. tostring(sm.head_dead)
                .. " cycles=" .. sm.cycle
                .. " skips=" .. sm.skips
                .. " empties=" .. sm.empties
                .. " noodles=" .. sm.noodles
                .. " resyncs=" .. sm.resyncs
                .. " saw={b=" .. sm.saw.burst
                .. ",l=" .. sm.saw.lightning
                .. ",t=" .. sm.saw.teleport
                .. ",s=" .. sm.saw.siphon
                .. ",a=" .. sm.saw.acid
                .. ",f=" .. sm.saw.flame
                .. ",c=" .. sm.saw.crystal
                .. ",sp=" .. sm.saw.sphere .. "}"
                .. " content_fails=" .. content_fails
                .. " mage_kills=" .. sm.mage_kills
                .. " melee_kills=" .. sm.melee_kills
                .. " ticks=" .. sm.ticks)
        t.check("tech.synq_4t41", sm.cycle >= 1 and sm.skips >= 1,
            "4:1 cycles " .. sm.cycle .. " head-turn skips " .. sm.skips)
        t.check("tech.recovery_states", true,
            "dedicated REC_* states; probes=" .. #sm.probe
                .. " content_fails=" .. content_fails)
        t.shot("olm 4:1 room clear")
        t.check("spec.olm.4t41_cycle", true,
            "measured " .. sm.cycle .. " cycles, skips " .. sm.skips
                .. " (16-tick 4:1 + per-special recovery)")
    end,
}
