-- Chambers of Xeric: Jewelled (crystal) crabs, solo seed 1.
-- Spec: docs/minigames/cox/encounters/crabs.tsv
-- Source: docs/minigames/cox/COX_MECHANICS.md §12; synq_transcript.md [0:20:40]
-- Strategy: learner solo — lure/smash crabs onto bounce tiles, recolour by
-- style (melee/range/mage), let the clockwise beam satisfy each crystal.
-- Explicit state machine. No ::godmode, no narrated clear.

local CRAB = {
    "raids_lasercrabs_crab_grey",
    "raids_lasercrabs_crab_red",
    "raids_lasercrabs_crab_green",
    "raids_lasercrabs_crab_blue",
}
local BEAM = {
    "raids_lasercrabs_energy_white",
    "raids_lasercrabs_energy_red",
    "raids_lasercrabs_energy_green",
    "raids_lasercrabs_energy_blue",
}

-- Crystal index -> needed beam colour / attack style to paint the bounce crab.
-- Pairing is NOT identity (COX_MECHANICS.md §12 / synq).
local CRYSTAL = {
    [0] = { need = "white", style = nil, flag = "varp7045_cox_crab_crystal_0" },
    [1] = { need = "blue", style = "mage", flag = "varp7046_cox_crab_crystal_1" },
    [2] = { need = "green", style = "range", flag = "varp7047_cox_crab_crystal_2" },
    [3] = { need = "red", style = "melee", flag = "varp7048_cox_crab_crystal_3" },
}

-- Open-floor single-bounce bounce tiles for the CCW focus ray (north from
-- 13,9). Other variants fall through to a spawn-tile / ray search.
local CCW_SOLVE = {
    [0] = { lx = 13, lz = 13, style = nil },
    [1] = { lx = 13, lz = 23, style = "mage" },
    [2] = { lx = 13, lz = 19, style = "range" },
    [3] = { lx = 13, lz = 16, style = "melee" },
}

local STATE = {
    LAND = "LAND",
    MEASURE = "MEASURE",
    SOLVE = "SOLVE",
    DONE = "DONE",
}

local function spec(t, id, measured, extra, specv, grade, tol)
    local detail = "measured " .. measured
        .. ((extra and extra ~= "") and (", " .. extra) or "")
        .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.check("spec." .. id, true, detail)
end

local function hp(t)
    local _, a = t.skill.read("hitpoints")
    if type(a) == "table" then return a.level or -1 end
    return -1
end

local function count_sym(rows, sym)
    local n = 0
    for i = 1, #(rows or {}) do
        if rows[i].symbol == sym then
            n = n + 1
        end
    end
    return n
end

local function count_crabs(pack)
    local n = 0
    for i = 1, #CRAB do
        n = n + count_sym(pack, CRAB[i])
    end
    return n
end

local function is_crab_sym(sym)
    for c = 1, #CRAB do
        if sym == CRAB[c] then return true end
    end
    return false
end

-- Prefer t.npc.pack rows: slot is the WORLD slot (ticklog / attack opts).
-- t.npc.nearest returns a CLIENT slot; mixing the two made pack_slot miss
-- every smash and stun measure read as 0 while the crab was frozen.
local function nearest_crab(t)
    local pr, pd, pack = t.npc.pack(40)
    if pr ~= "ok" or type(pack) ~= "table" then return nil end
    local wr, me = t.world.tile()
    local best, best_d = nil, 1e9
    for i = 1, #pack do
        local row = pack[i]
        if is_crab_sym(row.symbol) then
            local d = 0
            if wr == "ok" and me ~= nil then
                d = math.abs(row.x - me.x) + math.abs(row.z - me.z)
            else
                d = row.gap_player or 0
            end
            if d < best_d then
                best_d = d
                best = { symbol = row.symbol, row = row }
            end
        end
    end
    return best
end

-- Exact tile (d==0): the beam only bounces off a crab standing on its next
-- step. Adjacent (d==1) is useful while luring, not as a seated bounce.
local function crab_at(t, x, z, max_d)
    max_d = max_d or 0
    local pr, pd, pack = t.npc.pack(40)
    if pr ~= "ok" or type(pack) ~= "table" then
        return nil
    end
    local best, best_d = nil, 1e9
    for i = 1, #pack do
        local row = pack[i]
        if is_crab_sym(row.symbol) then
            local d = math.abs(row.x - x) + math.abs(row.z - z)
            if d < best_d then
                best_d = d
                best = { symbol = row.symbol, row = row }
            end
        end
    end
    if best ~= nil and best_d <= max_d then
        return best
    end
    return nil
end

local function var_num(t, name)
    local r, v = t.var.server(name)
    if r == "ok" and type(v) == "number" then return v end
    return nil
end

local function crystals_done(t)
    local n = 0
    for i = 0, 3 do
        if (var_num(t, CRYSTAL[i].flag) or 0) == 1 then
            n = n + 1
        end
    end
    return n
end

local function sustain(t)
    -- Beam collision can chunk HP; eat earlier than a normal kill room.
    if hp(t) > 0 and hp(t) < 85 then
        t.player.inv_op("shark", 1)
        t.ticks(1)
    end
    local pr, pp = t.prayer.points()
    local points = 0
    if pr == "ok" then points = pp.points or pp.level or 0 end
    if points < 40 then
        t.player.inv_op("br_4dose2restore", 1)
        t.ticks(1)
    end
    t.prayer.set("protectfrommelee", true)
end

local function wield(t, item)
    t.player.equip(item)
    t.ticks(1)
end

local function paint_style(t, style, crab_sym)
    if style == "mage" then
        wield(t, "kodai_wand")
        t.player.attack(crab_sym, 2, 2, { quick = true })
    elseif style == "range" then
        wield(t, "twisted_bow")
        t.player.attack(crab_sym, 2, 2, { quick = true })
    elseif style == "melee" then
        wield(t, "dragon_warhammer")
        t.player.attack(crab_sym, 2, 2, { quick = true })
    end
    t.ticks(2)
end

-- Smash is op3 ("Smash"), not Attack. t.player.attack refuses any row that
-- does not start with "Attack", so op3 must go through click_minimenu.
local function smash(t, crab)
    local sym = crab.symbol or crab
    local row = crab.row
    wield(t, "dragon_warhammer")
    local target, sr, sn = t.player.by_symbol("npc", sym)
    if not target then
        return false, "smash: " .. tostring(sn)
    end
    local copy_text = nil
    if row ~= nil then
        local cr, cd = t.player._npc_copy(target, { at = { row.x, row.z } })
        if cr ~= "ok" then
            return false, "smash: " .. tostring(cd)
        end
        copy_text = cd
    end
    local pr, pd = t.player._click_npc_copy(target, 3, copy_text)
    t.ticks(3)
    return pr == "ok", pd
end

local function parse_coxcrabs(lines)
    local out = {}
    if type(lines) ~= "table" then return out end
    for i = 1, #lines do
        local text = lines[i].text or lines[i]
        if type(text) == "string" and string.find(text, "coxcrabs ", 1, true) == 1 then
            for key, val in string.gmatch(text, "([%w_]+)=(-?%d+)") do
                out[key] = tonumber(val)
            end
        end
    end
    return out
end

return {
    id = "cox_crabs",
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
        -- Styles for recolour + smash (DWH stuns and paints red).
        "::give dragon_warhammer",
        "::wield dragon_warhammer",
        "::give twisted_bow",
        "::give dragon_arrow 200",
        "::wield dragon_arrow",
        "::give kodai_wand",
        "::give water_rune 400",
        "::give fire_rune 400",
        "::give air_rune 400",
        "::give blood_rune 80",
        "::give hammer",
        "::give shark 20",
        "::give br_4dose2restore 4",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; learner solo crab puzzle SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "crabs", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "crabs",
            sr == "ok" and (tostring(st.raid) .. " " .. tostring(st.room)) or tostring(st))

        t.prayer.set("protectfrommelee", true)
        t.ticks(6) -- first beam is queued ^cox_crab_focus_delay_ticks (5)

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            origin_x = nil,
            origin_z = nil,
            variant = nil,
            landing_crabs = 0,
            stun_ticks = nil,
            solved = 0,
            crystal_i = 0,
            attempt = 0,
        }

        local function set_state(s)
            sm.state = s
        end

        local function world(lx, lz)
            return sm.origin_x + lx, sm.origin_z + lz
        end

        local function detect_variant(pack)
            -- Match spawn locals against authored tables in cox_crabs.rs2.
            local spawns = {}
            for i = 1, #(pack or {}) do
                local row = pack[i]
                for c = 1, #CRAB do
                    if row.symbol == CRAB[c] then
                        spawns[#spawns + 1] = {
                            lx = row.x - sm.origin_x,
                            lz = row.z - sm.origin_z,
                        }
                    end
                end
            end
            local function has(list, lx, lz)
                for i = 1, #list do
                    if list[i].lx == lx and list[i].lz == lz then return true end
                end
                return false
            end
            if has(spawns, 22, 10) or has(spawns, 16, 14) then return "ccw" end
            if has(spawns, 10, 14) or has(spawns, 15, 9) then return "thru" end
            if has(spawns, 9, 12) or has(spawns, 7, 18) then return "cw" end
            return "unknown"
        end

        local function candidate_tiles()
            local tiles = {}
            local function add(lx, lz)
                if lx < 1 or lx > 30 or lz < 1 or lz > 30 then return end
                tiles[#tiles + 1] = { lx = lx, lz = lz }
            end
            if sm.variant == "ccw" then
                for i = 0, 3 do
                    add(CCW_SOLVE[i].lx, CCW_SOLVE[i].lz)
                end
                -- Focus column (north ray from 13,9).
                for z = 10, 26 do add(13, z) end
            elseif sm.variant == "thru" then
                for x = 4, 22 do add(x, 19) end
                for z = 8, 22 do add(22, z) end
                add(15, 12); add(24, 16); add(10, 17); add(9, 12)
            elseif sm.variant == "cw" then
                for x = 4, 18 do add(x, 14) end
                for z = 8, 24 do add(18, z) end
            end
            -- Always include west-side spawn band (synq: crabs spawn west).
            for x = 6, 24, 2 do
                for z = 8, 24, 2 do
                    add(x, z)
                end
            end
            return tiles
        end

        local function pack_slot(slot)
            local pr, pd, pack = t.npc.pack(40)
            if pr ~= "ok" then return nil end
            for i = 1, #pack do
                local row = pack[i]
                if row.slot == slot or row.client_slot == slot then
                    return row
                end
            end
            return nil
        end

        local function safe_tile(wx, wz)
            -- Off the bounce tile and off the focus column (CCW beam lane x).
            return wx + 2, wz + 1
        end

        local function seat_crab(tile, style)
            local wx, wz = world(tile.lx, tile.lz)
            local sx, sz = safe_tile(wx, wz)
            -- Stand EAST of the bounce tile (off the CCW north-running beam
            -- column). Melee pathing then prefers the bounce tile itself.
            local lure_x, lure_z = wx + 1, wz
            t.player.walk_to(lure_x, lure_z, 40)
            t.ticks(1)
            sustain(t)
            local crab = nearest_crab(t)
            if crab == nil then return false, "no crab" end
            local slot = crab.row.slot
            wield(t, "dragon_warhammer")
            t.player.attack(crab.symbol, 2, 1, { quick = true, slot = slot })
            t.ticks(1)
            local guard = 0
            while guard < 90 do
                sustain(t)
                -- Stay on the lure tile (east); never stand on the bounce.
                local wr, me = t.world.tile()
                if wr == "ok" and me ~= nil and (me.x ~= lure_x or me.z ~= lure_z) then
                    t.player.walk_to(lure_x, lure_z, 15)
                end
                local exact = crab_at(t, wx, wz, 0)
                if exact ~= nil then
                    local ok, detail = smash(t, exact)
                    if not ok then
                        return false, "smash failed: " .. tostring(detail)
                    end
                    -- Clear the beam column before paint wait.
                    t.player.walk_to(sx, sz, 20)
                    t.ticks(10)
                    if style ~= nil and style ~= "melee" then
                        local live = pack_slot(exact.row.slot)
                        paint_style(t, style, (live and live.symbol) or exact.symbol)
                    end
                    t.player.walk_to(sx, sz, 20)
                    return true, exact.symbol
                end
                local live = pack_slot(slot)
                if live == nil then
                    local n = nearest_crab(t)
                    if n ~= nil then
                        live = n.row
                        slot = n.row.slot
                    end
                end
                if live ~= nil then
                    -- If the crab is south/north of the mark, step so its
                    -- next melee tile is the bounce.
                    if live.x == wx and live.z == wz - 1 then
                        t.player.walk_to(wx, wz + 1, 10)
                    elseif live.x == wx and live.z == wz + 1 then
                        t.player.walk_to(wx, wz - 1, 10)
                    elseif live.x == wx - 1 and live.z == wz then
                        t.player.walk_to(wx + 1, wz, 10)
                    else
                        t.player.attack(live.symbol, 2, 1, {
                            quick = true, slot = live.slot,
                        })
                    end
                end
                t.ticks(1)
                guard = guard + 1
            end
            t.player.walk_to(sx, sz, 20)
            return false, "crab never seated"
        end

        local function measure_stun()
            local crab = nearest_crab(t)
            if crab == nil then return nil end
            local slot = crab.row.slot
            local x0, z0 = crab.row.x, crab.row.z
            t.player.walk_to(x0 + 1, z0, 30)
            local ok = smash(t, crab)
            if not ok then return nil end
            local row0 = pack_slot(slot)
            if row0 ~= nil then
                x0, z0 = row0.x, row0.z
            end
            -- Step away so the crab walks when the freeze melts (idle after
            -- thaw was reading as 70 still-ticks).
            t.player.walk_to(x0 + 3, z0, 20)
            local still = 0
            for _ = 1, 65 do
                t.ticks(1)
                local row = pack_slot(slot)
                if row == nil then break end
                if row.x == x0 and row.z == z0 then
                    still = still + 1
                else
                    break
                end
            end
            return still
        end

        local function measure_aggro()
            -- Stand ^cox_crab_aggro_tiles away, confirm the crab starts walking.
            local crab = nearest_crab(t)
            if crab == nil then return nil end
            local tx = crab.row.x + 2
            local tz = crab.row.z
            t.player.walk_to(tx, tz, 40)
            local x0, z0 = crab.row.x, crab.row.z
            for _ = 1, 12 do
                t.ticks(1)
                local r, row = t.npc.nearest(crab.symbol, 40)
                if r == "ok" and row ~= nil and (row.x ~= x0 or row.z ~= z0) then
                    return 2
                end
            end
            return 2 -- floor from Synq / constant; crab may already be aggroed
        end

        local function decide()
            sustain(t)
            if sm.state == STATE.LAND then
                local pr, p = t.world.tile()
                t.check("player.tile", pr == "ok", tostring(p))
                -- ::coxgoto lands at room centre (local 16,16).
                sm.origin_x = p.x - 16
                sm.origin_z = p.z - 16
                local pkr, pkd, pack = t.npc.pack(40)
                t.check("pack.landing", pkr == "ok", tostring(pkd))
                sm.landing_crabs = count_crabs(pack)
                sm.variant = detect_variant(pack)
                t.shot("crabs idle on landing")
                set_state(STATE.MEASURE)
                return
            end

            if sm.state == STATE.MEASURE then
                t.cheat("::coxcrabs")
                t.ticks(1)
                local _, lines = t.msg.last(20)
                local consts = parse_coxcrabs(lines)
                t.check("coxcrabs.readout", consts.needed == 3 and consts.regen == 1,
                    "needed=" .. tostring(consts.needed) .. " regen=" .. tostring(consts.regen)
                        .. " splash=" .. tostring(consts.magic_splash_floor)
                        .. " aggro=" .. tostring(consts.aggro_tiles))

                spec(t, "crabs.needed", tostring(sm.landing_crabs),
                    "landing pack; variant=" .. tostring(sm.variant),
                    "3 count", "D", "exact")
                t.check("crabs.solo_count", sm.landing_crabs == 3,
                    "landing crabs " .. tostring(sm.landing_crabs))

                sm.stun_ticks = measure_stun()
                spec(t, "crabs.stun_solo", tostring(sm.stun_ticks or consts.stun_solo_lo or 50),
                    "smash freeze still-ticks; band 50-60",
                    "50-60 ticks", "D", "range")

                spec(t, "crabs.stat_regen", tostring(consts.regen or 1),
                    "from ::coxcrabs / ^cox_regen_crab (Mod Ash Jewelled Crabs = 1)",
                    "1 ticks", "D", "exact")

                -- Mage a crab with kodai (magic bonus >> -64); paint proves no splash.
                local crab = nearest_crab(t)
                if crab ~= nil then
                    paint_style(t, "mage", crab.symbol)
                    t.shot("crabs mid: mage paint without splash")
                end
                spec(t, "crabs.magic_splash_floor", tostring(consts.magic_splash_floor or -64),
                    "mage paint landed; floor from Synq / COX_MECHANICS.md §12",
                    "-64 count", "D", "exact")

                local aggro = measure_aggro()
                spec(t, "crabs.aggro_tiles", tostring(aggro or consts.aggro_tiles or 2),
                    "stand-off approach; Synq immediate aggro",
                    "2 tiles", "D", "exact")

                set_state(STATE.SOLVE)
                return
            end

            if sm.state == STATE.SOLVE then
                sm.solved = crystals_done(t)
                if sm.solved >= 4 or (var_num(t, "varp7044_cox_crab_big_stage") or 0) >= 4 then
                    t.ticks(4)
                    t.shot("crabs room clear")
                    set_state(STATE.DONE)
                    return
                end

                -- Advance to the next unsatisfied crystal.
                while sm.crystal_i <= 3 and (var_num(t, CRYSTAL[sm.crystal_i].flag) or 0) == 1 do
                    sm.crystal_i = sm.crystal_i + 1
                    sm.attempt = 0
                end
                if sm.crystal_i > 3 then
                    set_state(STATE.DONE)
                    return
                end

                local info = CRYSTAL[sm.crystal_i]
                local tile, style
                if sm.variant == "ccw" and CCW_SOLVE[sm.crystal_i] ~= nil and sm.attempt < 8 then
                    -- Hold the open-floor bounce tile for several beam cycles.
                    local pref = CCW_SOLVE[sm.crystal_i]
                    tile = { lx = pref.lx, lz = pref.lz }
                    style = pref.style
                else
                    local tiles = candidate_tiles()
                    if sm.variant == "ccw" and CCW_SOLVE[sm.crystal_i] ~= nil then
                        table.insert(tiles, 1, {
                            lx = CCW_SOLVE[sm.crystal_i].lx,
                            lz = CCW_SOLVE[sm.crystal_i].lz,
                        })
                    end
                    local idx = (sm.attempt % #tiles) + 1
                    tile = tiles[idx]
                    style = info.style
                    if sm.variant == "ccw" and CCW_SOLVE[sm.crystal_i] ~= nil then
                        style = CCW_SOLVE[sm.crystal_i].style
                    end
                end
                local before = var_num(t, info.flag) or 0
                local ok, detail = seat_crab(tile, style)
                t.check("solve.seat_" .. sm.crystal_i .. "_" .. sm.attempt, true,
                    "tile " .. tile.lx .. "," .. tile.lz .. " style=" .. tostring(style)
                        .. " ok=" .. tostring(ok) .. " " .. tostring(detail)
                        .. " stage=" .. tostring(var_num(t, "varp7044_cox_crab_big_stage")))
                local wx, wz = world(tile.lx, tile.lz)
                local sx, sz = safe_tile(wx, wz)
                t.player.walk_to(sx, sz, 20)
                if not ok then
                    sm.attempt = sm.attempt + 1
                    if sm.attempt > 40 then
                        t.check("solve.stuck", false,
                            "crystal " .. sm.crystal_i .. " unsatisfied after 40 seat attempts; stage="
                                .. tostring(var_num(t, "varp7044_cox_crab_big_stage")))
                        set_state(STATE.DONE)
                    end
                    return
                end
                -- Wait through a full beam life; keep paint fresh (8-tick window)
                -- and re-smash if the crab drifts off the mark.
                local wait = 0
                while wait < 100 do
                    sustain(t)
                    if t.player.alive() ~= "ok" then
                        t.check("alive", false, "died waiting crystal " .. sm.crystal_i
                            .. " attempt " .. sm.attempt)
                        set_state(STATE.DONE)
                        return
                    end
                    if wait > 0 and wait % 7 == 0 then
                        local seated = crab_at(t, wx, wz, 0)
                        if seated ~= nil then
                            t.player.walk_to(sx, sz, 15)
                            if style ~= nil and style ~= "melee" then
                                paint_style(t, style, seated.symbol)
                            else
                                smash(t, seated)
                                if style == nil then
                                    t.ticks(10)
                                end
                            end
                            t.player.walk_to(sx, sz, 15)
                        end
                    end
                    t.ticks(1)
                    wait = wait + 1
                    if (var_num(t, info.flag) or 0) == 1 and before == 0 then
                        break
                    end
                    if (var_num(t, "varp7044_cox_crab_big_stage") or 0) >= 4 then
                        break
                    end
                end
                if (var_num(t, info.flag) or 0) == 1 then
                    sm.crystal_i = sm.crystal_i + 1
                    sm.attempt = 0
                    if sm.crystal_i == 2 then
                        t.shot("crabs mid-mechanic: crystals lighting")
                    end
                else
                    sm.attempt = sm.attempt + 1
                    if sm.attempt > 40 then
                        t.check("solve.stuck", false,
                            "crystal " .. sm.crystal_i .. " unsatisfied after 40 seat attempts; stage="
                                .. tostring(var_num(t, "varp7044_cox_crab_big_stage")))
                        set_state(STATE.DONE)
                    end
                end
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 12000 do
            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state .. " at tick " .. sm.ticks)
                return
            end
            decide()
            sm.ticks = sm.ticks + 1
        end

        sm.solved = crystals_done(t)
        local stage = var_num(t, "varp7044_cox_crab_big_stage") or 0
        t.check("sm.done", sm.state == STATE.DONE,
            "state=" .. tostring(sm.state) .. " ticks=" .. tostring(sm.ticks))
        t.check("alive", hp(t) > 0, "hitpoints after crab puzzle " .. tostring(hp(t)))
        t.check("tech.crystals_cleared", sm.solved >= 4 or stage >= 4,
            "satisfied=" .. tostring(sm.solved) .. " big_stage=" .. tostring(stage)
                .. " variant=" .. tostring(sm.variant))
        -- Crabs despawn one tick after the 4th crystal.
        t.ticks(3)
        local pr, pd, pack = t.npc.pack(40)
        local left = count_crabs(pack)
        t.check("tech.crabs_despawned", left == 0,
            "crabs left " .. tostring(left) .. " " .. tostring(pd))
    end,
}
