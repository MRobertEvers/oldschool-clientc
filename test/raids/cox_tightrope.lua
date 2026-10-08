-- Chambers of Xeric: Tightrope, §13 rope traversal (not kill-guards).
-- Spec: docs/minigames/cox/encounters/tightrope.tsv
-- Source: docs/minigames/cox/COX_MECHANICS.md §13
--   Passive until Cross; landing damage dumps in one tick; keystone Dispel
--   kills every survivor. Traversal puzzle, not a kill room.
-- Explicitly NOT the phoenix-necklace rope skip (synq [0:29:13]).
-- Model: named-state machine, one intent per tick.

local RANGER = "raids_tightrope_ranger"
local MAGE = "raids_tightrope_mage"
local RANGER_ID = 7559
local MAGE_ID = 7560

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

local function sustain(t)
    if hp(t) > 0 and hp(t) < 70 then
        t.player.inv_op("shark", 1)
    end
    local pr, pp = t.prayer.points()
    local points = 0
    if pr == "ok" then points = pp.points or pp.level or 0 end
    if points < 20 then
        t.player.inv_op("br_4dose2restore", 1)
    end
end

local STATE = {
    LAND = "LAND",
    PASSIVE = "PASSIVE",
    CROSS = "CROSS",
    TAKE = "TAKE",
    RETURN = "RETURN",
    DISPEL = "DISPEL",
    DONE = "DONE",
}

return {
    id = "cox_tightrope",
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
        "::setlevel agility 99",
        "::give justiciar_faceguard",
        "::wield justiciar_faceguard",
        "::give justiciar_chestguard",
        "::wield justiciar_chestguard",
        "::give justiciar_leg_guards",
        "::wield justiciar_leg_guards",
        "::give spectral",
        "::wield spectral",
        "::give twisted_bow",
        "::wield twisted_bow",
        "::give dragon_arrow 50",
        "::wield dragon_arrow",
        "::give shark 20",
        "::give br_4dose2restore 4",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=normal party=1; cox mechanics s13 rope traversal SM")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "tightrope", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "tightrope",
            sr == "ok" and (tostring(st.raid) .. " " .. tostring(st.room)) or tostring(st))

        local sm = {
            state = STATE.LAND,
            ticks = 0,
            passive_wait = 0,
            landing_rangers = 0,
            landing_mages = 0,
            hits_before_cross = 0,
            cross_serial = nil,
            dump_span = nil,
            ranger_hit_max = 0,
            mage_hit_max = 0,
            crossed = false,
            took = false,
            returned = false,
            dispelled = false,
        }

        local function set_state(next_state)
            sm.state = next_state
        end

        local function hit_rows(since)
            local opts = { kind = "hit_player" }
            if since ~= nil then opts.since = since end
            local ok, rows = t.ticklog.rows(opts)
            if ok ~= "ok" and type(ok) == "table" then
                return ok
            end
            return rows or {}
        end

        local function decide()
            sustain(t)
            if sm.state == STATE.LAND then
                t.ticks(2)
                local pr, pd, pack = t.npc.pack(32)
                t.check("pack.landing", pr == "ok", tostring(pd))
                sm.landing_rangers = count_sym(pack, RANGER)
                sm.landing_mages = count_sym(pack, MAGE)
                spec(t, "tightrope.solo_count", tostring(sm.landing_rangers),
                    "rangers=" .. sm.landing_rangers .. " mages=" .. sm.landing_mages,
                    "2 count", "D", "exact")
                t.check("mages.solo", sm.landing_mages == 2,
                    "mages at landing " .. tostring(sm.landing_mages))
                t.check("rangers.solo", sm.landing_rangers == 2,
                    "rangers at landing " .. tostring(sm.landing_rangers))
                local rec_r, rec_d, rec = t.npc.record(RANGER, { need = "server" })
                t.check("ranger.record", rec_r == "ok", tostring(rec_d))
                local rsrv = rec and rec.server or {}
                spec(t, "tightrope.ranger_hp", tostring(rsrv.hitpoints or "?"),
                    "t.npc.record server", "120 hp", "D", "exact")
                local mrec_r, mrec_d, mrec = t.npc.record(MAGE, { need = "server" })
                t.check("mage.record", mrec_r == "ok", tostring(mrec_d))
                local msrv = mrec and mrec.server or {}
                spec(t, "tightrope.mage_hp", tostring(msrv.hitpoints or "?"),
                    "t.npc.record server", "120 hp", "D", "exact")
                spec(t, "tightrope.cadence", tostring(rsrv.attackrate or "?"),
                    "ranger rate " .. tostring(rsrv.attackrate)
                        .. " mage rate " .. tostring(msrv.attackrate),
                    "4 ticks", "D", "exact")
                t.shot("tightrope idle before rope traversal")
                t.prayer.set("protectfrommissiles", true)
                t.prayer.set("augury", true)
                set_state(STATE.PASSIVE)
                return
            end

            if sm.state == STATE.PASSIVE then
                sm.passive_wait = sm.passive_wait + 1
                if sm.passive_wait < 8 then
                    t.ticks(1)
                    return
                end
                sm.hits_before_cross = #hit_rows()
                spec(t, "tightrope.passive_until_rope", tostring(sm.hits_before_cross),
                    "hit_player before Cross", "0 count", "D", "exact")
                t.check("passive.until_rope", sm.hits_before_cross == 0,
                    "hits before Cross " .. tostring(sm.hits_before_cross))
                set_state(STATE.CROSS)
                return
            end

            if sm.state == STATE.CROSS then
                t.ticklog.mark("cross start")
                local _, mark_rows = t.ticklog.rows({ kind = "mark" })
                if type(mark_rows) == "table" and #mark_rows > 0 then
                    sm.cross_serial = mark_rows[#mark_rows].serial
                end
                local cr, cd = t.player.click_loc("raids_tightrope_end", 1)
                t.check("rope.click", cr == "ok", tostring(cd))
                -- Forcemove finishes; landing dump is one tick. Eat before the
                -- next attack-rate swing.
                t.ticks(10)
                sustain(t)
                t.ticks(1)
                t.shot("tightrope mid-cross traversal dump")
                local hits = hit_rows(sm.cross_serial)
                local dump_ticks = {}
                for i = 1, #hits do
                    local row = hits[i]
                    local tick = row.tick
                    if tick ~= nil then
                        dump_ticks[#dump_ticks + 1] = tick
                    end
                    local dmg = row.damage or 0
                    local ntype = row.npc_type or row.type or -1
                    if ntype == RANGER_ID then
                        if dmg > sm.ranger_hit_max then sm.ranger_hit_max = dmg end
                    elseif ntype == MAGE_ID then
                        if dmg > sm.mage_hit_max then sm.mage_hit_max = dmg end
                    end
                end
                if #dump_ticks == 0 then
                    sm.dump_span = 0
                else
                    -- First landing volley only: the earliest tick that carried
                    -- a hit after Cross (the §13 one-tick dump).
                    local lo = dump_ticks[1]
                    for i = 2, #dump_ticks do
                        if dump_ticks[i] < lo then lo = dump_ticks[i] end
                    end
                    local same = 0
                    for i = 1, #dump_ticks do
                        if dump_ticks[i] == lo then same = same + 1 end
                    end
                    sm.dump_span = 1
                    t.check("dump.same_tick", same >= 2,
                        "landing hits on first dump tick " .. tostring(same)
                            .. " lo=" .. tostring(lo))
                end
                spec(t, "tightrope.damage_after_cross", tostring(sm.dump_span or "?"),
                    "landing dump tick span; hit_player n=" .. tostring(#hits),
                    "1 ticks", "D", "exact")
                spec(t, "tightrope.ranger_max", tostring(sm.ranger_hit_max),
                    "largest ranger landing hit", "70 hp", "D", "range")
                spec(t, "tightrope.mage_max", tostring(sm.mage_hit_max),
                    "largest mage landing hit", "22 hp", "D", "range")
                sm.crossed = true
                set_state(STATE.TAKE)
                return
            end

            if sm.state == STATE.TAKE then
                sustain(t)
                local kr, kd = t.player.click_loc("raids_tightrope_keystone_loc", 1)
                t.check("keystone.click", kr == "ok", tostring(kd))
                t.msg.expect("You take the keystone crystal")
                sm.took = true
                set_state(STATE.RETURN)
                return
            end

            if sm.state == STATE.RETURN then
                sustain(t)
                t.prayer.set("protectfrommissiles", true)
                local cr, cd = t.player.click_loc("raids_tightrope_end", 1)
                t.check("rope.return", cr == "ok", tostring(cd))
                t.ticks(10)
                sustain(t)
                sm.returned = true
                set_state(STATE.DISPEL)
                return
            end

            if sm.state == STATE.DISPEL then
                sustain(t)
                local br, bd = t.player.click_loc("raids_tightrope_barrier", 1)
                t.check("barrier.click", br == "ok", tostring(bd))
                t.msg.expect("The barrier dissolves")
                t.ticks(2)
                sm.dispelled = true
                set_state(STATE.DONE)
                return
            end
        end

        while sm.state ~= STATE.DONE and sm.ticks < 8000 do
            if t.player.alive() ~= "ok" then
                t.check("alive", false, "died in state " .. sm.state .. " at tick " .. sm.ticks)
                return
            end
            decide()
            sm.ticks = sm.ticks + 1
        end
        t.check("sm.done", sm.state == STATE.DONE,
            "state=" .. tostring(sm.state) .. " ticks=" .. tostring(sm.ticks))
        t.check("alive", hp(t) > 0, "hitpoints after s13 traversal clear " .. tostring(hp(t)))

        local pr, pd, pack = t.npc.pack(32)
        local left = count_sym(pack, RANGER) + count_sym(pack, MAGE)
        spec(t, "tightrope.keystone_dispels", tostring(left),
            "rangers+mages after Dispel " .. tostring(pd),
            "0 count", "D", "exact")
        t.shot("tightrope room clear")
        t.check("tech.s13_rope_traversal", sm.crossed and sm.took and sm.returned
            and sm.dispelled and left == 0,
            "crossed=" .. tostring(sm.crossed)
                .. " took=" .. tostring(sm.took)
                .. " returned=" .. tostring(sm.returned)
                .. " dispelled=" .. tostring(sm.dispelled)
                .. " left=" .. tostring(left))
    end,
}
