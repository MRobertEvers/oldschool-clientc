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
-- Bounce tiles: crab on (13, lz) turns a north-bound focus ray east along
-- z=lz-1 (beam stays on the prior tile). Crystal 0 is at local (19,12), so
-- the mark is (13,13). (13,14) is kept as a second try when the crab seats
-- one tile north of the mark.
local CCW_SOLVE = {
    [0] = { lx = 13, lz = 13, style = nil },
    [1] = { lx = 13, lz = 23, style = "mage" },
    [2] = { lx = 13, lz = 19, style = "range" },
    [3] = { lx = 13, lz = 16, style = "melee" },
}
local CCW_SOLVE_ALT = {
    [0] = { lx = 13, lz = 14, style = nil },
    [1] = { lx = 13, lz = 22, style = "mage" },
    [2] = { lx = 13, lz = 18, style = "range" },
    [3] = { lx = 13, lz = 15, style = "melee" },
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
    -- Beam splash scales with current HP but still kills at low HP.
    while hp(t) > 0 and hp(t) < 90 do
        local er = t.player.inv_op("shark", 1)
        t.ticks(1)
        if er ~= "ok" then break end
        if hp(t) >= 90 then break end
    end
    local pr, pp = t.prayer.points()
    local points = 0
    if pr == "ok" then points = pp.points or pp.level or 0 end
    if points < 50 then
        t.player.inv_op("br_4dose2restore", 1)
        t.ticks(1)
    end
    t.prayer.set("protectfrommelee", true)
end

local function wield(t, item)
    t.player.equip(item)
    t.ticks(1)
end

local function paint_style(t, style, crab)
    local sym = type(crab) == "table" and crab.symbol or crab
    local row = type(crab) == "table" and (crab.row or crab) or nil
    -- nil opts = nearest copy. An empty {} is NOT nil and trips
    -- "_npc_copy: give at or slot" (run17 mid paint). Prefer at={x,z} when
    -- the pack row is known so the seated crab is the one painted.
    local opts = nil
    if row ~= nil and row.x ~= nil and row.z ~= nil then
        opts = { at = { row.x, row.z } }
    elseif row ~= nil and row.slot ~= nil then
        opts = { slot = row.slot }
    end
    if style == "mage" then
        -- Wand Attack is melee here (paints red). Cast a wave so
        -- player_hit_npc_prepare sees damagetype=magic → blue.
        wield(t, "kodai_wand")
        t.ticks(1)
        -- Non-quick settle: quick press fails when the crab is covered and
        -- never walk_nears (seat attempts from the safe tile).
        -- ticks=5: enough flight to paint, cheap enough for the seat loop's
        -- instruction budget (400k/resume; run22 died mid-crystal-1).
        local cr, cd = t.player.cast("water_wave", sym, 5, 2, opts)
        if cr ~= "ok" and cr ~= "timeout" then
            cr, cd = t.player.cast("fire_wave", sym, 5, 2, opts)
        end
        t.ticks(1)
        return cr, cd
    elseif style == "range" then
        wield(t, "dragon_arrow")
        wield(t, "twisted_bow")
        local ar, ad = t.player.attack(sym, 3, 3, opts)
        t.ticks(1)
        return ar, ad
    elseif style == "melee" then
        wield(t, "dragon_warhammer")
        local ar, ad = t.player.attack(sym, 3, 3, opts)
        t.ticks(1)
        return ar, ad
    end
    t.ticks(1)
    return "ok", nil
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
        "::give br_4dose2restore 6",
        "::give shark 28",
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

        local function seat_crab(tile, style, flag)
            local wx, wz = world(tile.lx, tile.lz)
            local sx, sz = safe_tile(wx, wz)
            -- Stand WEST of the bounce tile (off the CCW beam column). East
            -- lure stacked crabs on the lure tile and never took the mark.
            local lure_x, lure_z = wx - 1, wz
            t.player.walk_to(lure_x, lure_z, 40)
            t.ticks(2)
            sustain(t)
            local crab = nearest_crab(t)
            if crab == nil then return false, "no crab" end
            local slot = crab.row.slot
            wield(t, "dragon_warhammer")
            t.player.attack(crab.symbol, 2, 1, { quick = true, slot = slot })
            t.ticks(2)
            local guard = 0
            local last_detail = "crab never seated"
            while guard < 28 do
                if flag ~= nil and (var_num(t, flag) or 0) == 1 then
                    t.player.walk_to(sx, sz, 20)
                    return true, "crystal already lit"
                end
                if guard % 5 == 0 then sustain(t) end
                -- Prefer safe tile between pulls so the beam column is free.
                if guard % 7 == 6 then
                    t.player.walk_to(sx, sz, 10)
                    t.ticks(2)
                end
                local exact = crab_at(t, wx, wz, 0)
                if exact ~= nil then
                    -- Stop Attack before smash: crab often walks off the mark
                    -- during the smash approach (run19), and wand-melee later
                    -- paints red over blue.
                    t.player.walk_to(lure_x, lure_z, 8)
                    t.ticks(1)
                    exact = crab_at(t, wx, wz, 0)
                    if exact == nil then
                        last_detail = "crab slipped mark before smash"
                    else
                        local ok, detail = smash(t, exact)
                        if not ok then
                            last_detail = "smash failed: " .. tostring(detail)
                        else
                            -- Freeze must hold ON the bounce tile.
                            t.ticks(2)
                            exact = crab_at(t, wx, wz, 0)
                            if exact == nil then
                                last_detail = "crab left mark after smash"
                            elseif style ~= nil and style ~= "melee" then
                                local want = (style == "mage") and "raids_lasercrabs_crab_blue"
                                    or "raids_lasercrabs_crab_green"
                                local last_paint = "none"
                                local painted = false
                                for _ = 1, 3 do
                                    local live = crab_at(t, wx, wz, 0)
                                    if live == nil then
                                        last_detail = "crab left mark during paint"
                                        break
                                    end
                                    if live.symbol == want then
                                        painted = true
                                        break
                                    end
                                    t.player.walk_to(lure_x, lure_z, 8)
                                    t.ticks(1)
                                    live = crab_at(t, wx, wz, 0) or live
                                    local pr, pd = paint_style(t, style, live)
                                    last_paint = tostring(pr) .. ":" .. tostring(pd)
                                    for _ = 1, 3 do
                                        t.ticks(1)
                                        live = crab_at(t, wx, wz, 0)
                                        if live ~= nil and live.symbol == want then
                                            painted = true
                                            break
                                        end
                                    end
                                    if painted then break end
                                    if crab_at(t, wx, wz, 0) == nil then
                                        last_detail = "crab left mark during paint"
                                        break
                                    end
                                end
                                if painted then
                                    t.player.walk_to(sx, sz, 20)
                                    return true, want
                                end
                                last_detail = "paint failed want=" .. want
                                    .. " last=" .. last_paint
                            else
                                -- White beam needs grey; smash paints red for
                                -- ^cox_crab_paint_ticks. Wait out the red.
                                local grey = false
                                for _ = 1, 12 do
                                    t.ticks(1)
                                    local live = crab_at(t, wx, wz, 0)
                                    if live == nil then break end
                                    if live.symbol == "raids_lasercrabs_crab_grey" then
                                        grey = true
                                        break
                                    end
                                end
                                if grey and crab_at(t, wx, wz, 0) ~= nil then
                                    t.player.walk_to(sx, sz, 20)
                                    return true, "raids_lasercrabs_crab_grey"
                                end
                                last_detail = "white seat: crab not grey on mark after smash"
                            end
                        end
                    end
                end
                local live = pack_slot(slot) or (nearest_crab(t) and nearest_crab(t).row)
                if live ~= nil then
                    slot = live.slot
                    if live.x > wx then
                        t.player.walk_to(wx, wz, 8)
                        t.ticks(1)
                        t.player.walk_to(lure_x, lure_z, 8)
                    else
                        t.player.walk_to(lure_x, lure_z, 8)
                        -- Only re-aggro when the crab is not already on the mark.
                        if crab_at(t, wx, wz, 0) == nil then
                            t.player.attack(live.symbol, 2, 1, {
                                quick = true, slot = live.slot,
                            })
                        end
                    end
                else
                    t.player.walk_to(lure_x, lure_z, 8)
                end
                t.ticks(2)
                guard = guard + 1
            end
            t.player.walk_to(sx, sz, 20)
            return false, last_detail
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
            -- Pack every 3rd tick — a 65× pack_slot loop left the solve phase
            -- under the 400k/resume budget (run26).
            local still = 0
            for i = 1, 60 do
                t.ticks(1)
                if i % 3 == 0 then
                    local row = pack_slot(slot)
                    if row == nil then break end
                    if row.x == x0 and row.z == z0 then
                        still = i
                    else
                        break
                    end
                end
            end
            return still > 0 and still or nil
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
                    paint_style(t, "mage", crab)
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
                if sm.variant == "ccw" and CCW_SOLVE[sm.crystal_i] ~= nil then
                    local pref = CCW_SOLVE[sm.crystal_i]
                    -- Alt marks only after primary fails repeatedly (alts
                    -- rarely seat and burn the instruction budget).
                    if sm.attempt >= 5 and CCW_SOLVE_ALT[sm.crystal_i] ~= nil
                        and sm.attempt % 2 == 1 then
                        pref = CCW_SOLVE_ALT[sm.crystal_i]
                    end
                    tile = { lx = pref.lx, lz = pref.lz }
                    style = pref.style
                else
                    local tiles = candidate_tiles()
                    local idx = (sm.attempt % #tiles) + 1
                    tile = tiles[idx]
                    style = info.style
                end
                local before = var_num(t, info.flag) or 0
                local ok, detail = seat_crab(tile, style, info.flag)
                t.check("solve.seat_" .. sm.crystal_i .. "_" .. sm.attempt, true,
                    "tile " .. tile.lx .. "," .. tile.lz .. " style=" .. tostring(style)
                        .. " ok=" .. tostring(ok) .. " " .. tostring(detail)
                        .. " stage=" .. tostring(var_num(t, "varp7044_cox_crab_big_stage")))
                local wx, wz = world(tile.lx, tile.lz)
                local sx, sz = safe_tile(wx, wz)
                t.player.walk_to(sx, sz, 20)
                if not ok then
                    sm.attempt = sm.attempt + 1
                    if sm.attempt > 8 then
                        t.check("solve.stuck", false,
                            "crystal " .. sm.crystal_i .. " unsatisfied after 8 seat attempts; stage="
                                .. tostring(var_num(t, "varp7044_cox_crab_big_stage")))
                        set_state(STATE.DONE)
                    end
                    return
                end
                -- Wait for a beam cycle. Stay off the bounce tile.
                -- White (style=nil): do NOT re-smash — that paints red and
                -- the black crystal needs an unchanged white beam off grey.
                -- Coloured: paint lasts ^cox_crab_paint_ticks (8); refresh
                -- every 4 ticks so the beam never sees a grey gap.
                local want_sym = nil
                if style == "mage" then want_sym = "raids_lasercrabs_crab_blue"
                elseif style == "range" then want_sym = "raids_lasercrabs_crab_green"
                elseif style == "melee" then want_sym = "raids_lasercrabs_crab_red"
                end
                -- Cheap wait: only var reads + ticks. No pack/paint on entry
                -- (run25 died in t.settle right after a fresh coloured seat).
                t.player.walk_to(sx + 2, sz, 20)
                local wait = 0
                while wait < 30 do
                    t.ticks(5)
                    wait = wait + 5
                    if (var_num(t, info.flag) or 0) == 1 then break end
                    if (var_num(t, "varp7044_cox_crab_big_stage") or 0) >= 4 then break end
                end
                if (var_num(t, info.flag) or 0) == 1 then
                    sm.crystal_i = sm.crystal_i + 1
                    sm.attempt = 0
                else
                    sm.attempt = sm.attempt + 1
                    if sm.attempt > 8 then
                        t.check("solve.stuck", false,
                            "crystal " .. sm.crystal_i .. " unsatisfied after 8 seat attempts; stage="
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
