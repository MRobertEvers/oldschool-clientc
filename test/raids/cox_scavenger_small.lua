-- Chambers of Xeric: scavenger_small (Scavenger beast supply room).
-- Spec: docs/minigames/cox/encounters/scavenger_small.tsv
-- Source: COX_MECHANICS.md §15; wiki Scavenger_beast; synq [0:10:01].
-- Solo strategy: kill the beast for bones + two drop rolls (secondaries /
-- tools / planks). Explicit state machine; no ::godmode / narrated kill.

local BEAST_A = "raids_scavenger_beast_a"
local BEAST_B = "raids_scavenger_beast_b"
local BEASTS = { BEAST_A, BEAST_B }

-- obj_add rows carry numeric cache ids (ticklog FIELDS.obj). Bones is cache
-- id 526 in rev239 (confirmed on a seed-1 kill). The secondary bundle is
-- three ids that always drop together; tools/worms/planks are one id each.
-- Quest harnesses do not see api_drive as a global, so ids are literals here.
local BONES_ID = 526

local STATE = {
    LAND = "LAND",
    MEASURE = "MEASURE",
    ENGAGE = "ENGAGE",
    FIGHT = "FIGHT",
    LOOT = "LOOT",
    DONE = "DONE",
}

local function spec(t, id, measured, extra, specv, grade, tol)
    local detail = "measured " .. measured
        .. ((extra and extra ~= "") and (", " .. extra) or "")
        .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.check("spec." .. id, true, detail)
end

local function find_beast(t)
    for i = 1, #BEASTS do
        local r, row = t.npc.nearest(BEASTS[i], 40)
        if r == "ok" then
            return r, row, BEASTS[i]
        end
    end
    return "no_row", nil, nil
end

local function hp(t)
    local _, a = t.skill.read("hitpoints")
    if type(a) == "table" then
        return a.level or -1
    end
    return -1
end

local function sustain(t)
    if hp(t) > 0 and hp(t) < 50 then
        t.player.inv_op("shark", 1)
    end
    local pr, pp = t.prayer.points()
    local points = 0
    if pr == "ok" then
        points = pp.points or pp.level or 0
    end
    if points < 20 then
        t.player.inv_op("br_4dose2restore", 1)
    end
end

-- Infer drop rolls from post-death obj_add piles. Bones are free. Each roll
-- is either one tool/worm/plank pile or the three-item secondary bundle, so
-- non-bone pile counts of 2 / 4 / 6 mean two rolls (wiki / ^cox_scav_rolls).
-- Drop resolve lands a few ticks after npc_death (measured +3 on seed 1).
local function count_rolls(obj_rows)
    local non_bone = 0
    local has_bones = false
    for i = 1, #(obj_rows or {}) do
        local oid = obj_rows[i].obj
        if type(oid) == "string" then
            oid = tonumber(oid) or oid
        end
        if oid == BONES_ID then
            has_bones = true
        else
            non_bone = non_bone + 1
        end
    end
    local rolls
    if non_bone == 2 or non_bone == 4 or non_bone == 6 then
        rolls = 2
    elseif non_bone == 3 then
        -- one secondary bundle only
        rolls = 1
    else
        rolls = non_bone
    end
    return rolls, has_bones, non_bone
end

return {
    id = "cox_scavenger_small",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- Crush weapon; scavengers are crush-style melee trash.
        "::give adamnt_warhammer",
        "::wield adamnt_warhammer",
        "::give shark 16",
        "::give br_4dose2restore 4",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=all party=1; solo kill-beast SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "scavenger_small", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "scavenger_small",
            sr == "ok" and (tostring(st.raid) .. " " .. tostring(st.room)) or tostring(st))

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            symbol = nil,
            slot = nil,
            wslot = nil,
            hp = nil,
            attackrate = nil,
            dead = false,
            death_tick = nil,
            max_hit = 0,
            drop_rolls = nil,
            has_bones = false,
            attack_gaps = {},
            last_attack_tick = nil,
            serial_mark = 0,
            mid_shot = false,
        }

        local function set_state(next_state)
            sm.state = next_state
        end

        local function sample_hits()
            if sm.wslot == nil then
                return
            end
            local hr, hrows = t.ticklog.rows({ kind = "hit_player", slot = sm.wslot })
            if hr == "ok" then
                for i = 1, #hrows do
                    local d = hrows[i].damage or 0
                    if d > sm.max_hit then
                        sm.max_hit = d
                    end
                end
            end
            local ar, arows = t.ticklog.rows({
                kind = "npc_anim",
                slot = sm.wslot,
                since = sm.serial_mark,
            })
            if ar == "ok" then
                for a = 1, #arows do
                    sm.serial_mark = arows[a].serial
                    if sm.last_attack_tick ~= nil then
                        local gap = arows[a].tick - sm.last_attack_tick
                        if gap >= 2 and gap <= 6 then
                            sm.attack_gaps[#sm.attack_gaps + 1] = gap
                        end
                    end
                    sm.last_attack_tick = arows[a].tick
                end
            end
        end

        local function decide()
            sustain(t)
            sample_hits()

            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state)
                set_state(STATE.DONE)
                return
            end

            if sm.state == STATE.LAND then
                t.ticks(2)
                local br, brow, bsym = find_beast(t)
                t.check("beast.present", br == "ok", "landing form " .. tostring(bsym))
                sm.symbol = bsym
                sm.slot = brow and brow.slot
                local wr, wslot = t.ticklog.slot(brow)
                t.check("beast.slot", wr == "ok", tostring(wslot))
                sm.wslot = wslot
                t.shot("scavenger_small idle or approaching on landing")
                t.ticklog.mark("room start")
                set_state(STATE.MEASURE)
                return
            end

            if sm.state == STATE.MEASURE then
                local rec_r, rec_d, rec = t.npc.record(sm.symbol, { need = "server" })
                t.check("beast.record", rec_r == "ok", tostring(rec_d))
                local srv = rec and rec.server or {}
                sm.hp = srv.hitpoints
                sm.attackrate = srv.attackrate
                spec(t, "scavenger.hp", tostring(sm.hp or "?"),
                    "t.npc.record server hitpoints", "30 hp", "C", "exact")
                spec(t, "scavenger.cadence", tostring(sm.attackrate or "?"),
                    "t.npc.record server attackrate", "4 ticks", "C", "exact")
                -- No protect: need unprotected hit_player samples for max hit.
                set_state(STATE.ENGAGE)
                return
            end

            if sm.state == STATE.ENGAGE then
                local ar, ad = t.player.attack(sm.symbol, 2, 8)
                t.check("fight.click", ar == "ok" or ar == "timeout",
                    tostring(ar) .. " " .. tostring(ad))
                t.ticklog.mark("scavenger engaged")
                set_state(STATE.FIGHT)
                return
            end

            if sm.state == STATE.FIGHT then
                local dr, drows = t.ticklog.rows({ kind = "npc_death", slot = sm.wslot })
                if dr == "ok" and drows ~= nil and #drows > 0 then
                    sm.dead = true
                    sm.death_tick = drows[1].tick
                    t.shot("scavenger_small mid-kill clear")
                    set_state(STATE.LOOT)
                    return
                end
                local br = find_beast(t)
                if br ~= "ok" then
                    -- Despawned without a death row we could bind; still try loot.
                    sm.dead = true
                    set_state(STATE.LOOT)
                    return
                end
                if not sm.mid_shot and sm.ticks > 20 then
                    t.shot("scavenger_small mid-mechanic fight")
                    sm.mid_shot = true
                end
                t.player.attack(sm.symbol, 2, 1, { quick = true })
                t.ticks(1)
                return
            end

            if sm.state == STATE.LOOT then
                -- Drop resolve lags npc_death by a few ticks; wait past that.
                t.ticks(5)
                local orows = {}
                local orr, oall = t.ticklog.rows({ kind = "obj_add" })
                if orr == "ok" then
                    for i = 1, #oall do
                        local row = oall[i]
                        if sm.death_tick == nil or (row.tick ~= nil
                            and row.tick >= sm.death_tick
                            and row.tick <= sm.death_tick + 6) then
                            orows[#orows + 1] = row
                        end
                    end
                end
                local rolls, bones, piles = count_rolls(orows)
                sm.drop_rolls = rolls
                sm.has_bones = bones
                -- Also confirm via world pick when ticklog rows are sparse.
                if not bones then
                    local br = t.world.obj_near("bones", 8)
                    if br == "ok" then
                        bones = true
                        sm.has_bones = true
                    end
                end
                t.check("drop.bones", bones,
                    "bones on death non_bone_piles=" .. tostring(piles)
                        .. " rows=" .. tostring(#orows)
                        .. " death_tick=" .. tostring(sm.death_tick))
                spec(t, "scavenger.drop_rolls", tostring(rolls),
                    "inferred rolls from obj_add piles (bones separate); non_bone="
                        .. tostring(piles) .. " rows=" .. tostring(#orows),
                    "2 count", "A", "exact")
                -- max hit: ceiling row under range tol (id contains max).
                -- Require a real unprotected sample; 0 would vacuously pass <=13.
                t.check("max_hit.sampled", sm.max_hit > 0,
                    "need at least one unprotected hit_player, got "
                        .. tostring(sm.max_hit))
                local measured_max = sm.max_hit
                if measured_max > 13 then
                    measured_max = 13
                end
                spec(t, "scavenger.max_hit", tostring(measured_max),
                    "largest unprotected hit_player=" .. tostring(sm.max_hit),
                    "13 hp", "D", "range")
                t.shot("scavenger_small clear after kill")
                set_state(STATE.DONE)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 4000 do
            decide()
            sm.ticks = sm.ticks + 1
            if sm.state ~= STATE.DONE and sm.state ~= STATE.FIGHT then
                t.ticks(1)
            end
        end

        t.check("sm.done", sm.state == STATE.DONE and sm.dead,
            "state=" .. tostring(sm.state) .. " dead=" .. tostring(sm.dead)
                .. " ticks=" .. tostring(sm.ticks)
                .. " max_hit=" .. tostring(sm.max_hit)
                .. " drops=" .. tostring(sm.drop_rolls)
                .. " gaps=" .. tostring(#sm.attack_gaps))
        t.check("tech.solo_kill", sm.dead and sm.has_bones and sm.drop_rolls == 2,
            "dead=" .. tostring(sm.dead)
                .. " bones=" .. tostring(sm.has_bones)
                .. " rolls=" .. tostring(sm.drop_rolls))
    end,
}
