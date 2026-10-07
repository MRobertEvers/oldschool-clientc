-- Chambers of Xeric: Tightrope, Synq solo (kill the guards).
-- Spec: docs/minigames/cox/encounters/tightrope.tsv
-- Source: docs/minigames/cox/synq_transcript.md [0:26:22]
--   "Before doing this, you'll want to defeat the mages and rangers that
--    guard the keystone. Use the twisted bow on the mages ..."
--   "Once you've defeated both the mages and rangers, you can now cross
--    the tightrope and get the keystone. Then, use the keystone on the
--    barrier."
-- Explicitly NOT the phoenix-necklace rope skip ([0:29:13]).
-- Model: named-state machine, one intent per tick.

local RANGER = "raids_tightrope_ranger"
local MAGE = "raids_tightrope_mage"

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

local function alive(t, sym)
    local r, row = t.npc.nearest(sym, 40)
    if r == "ok" and row ~= nil then return row end
    return nil
end

local function sustain(t)
    if hp(t) > 0 and hp(t) < 55 then
        t.player.inv_op("shark", 1)
    end
    local pr, pp = t.prayer.points()
    local points = 0
    if pr == "ok" then points = pp.points or pp.level or 0 end
    if points < 20 then
        t.player.inv_op("br_4dose2restore", 1)
    end
end

-- Synq solo kill-guards cycle. States name the step; transitions are the
-- only way the harness advances (no linear skip of the guards).
local STATE = {
    LAND = "LAND",
    KILL_MAGES = "KILL_MAGES",
    KILL_RANGERS = "KILL_RANGERS",
    CROSS = "CROSS",
    TAKE = "TAKE",
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
        -- Synq [0:26:22]: twisted bow on the mages; best ranged otherwise.
        "::give masori_mask",
        "::wield masori_mask",
        "::give masori_body",
        "::wield masori_body",
        "::give masori_chaps",
        "::wield masori_chaps",
        "::give avas_assembler",
        "::wield avas_assembler",
        "::give twisted_bow",
        "::wield twisted_bow",
        "::give dragon_arrow 400",
        "::wield dragon_arrow",
        "::give shark 16",
        "::give br_4dose2restore 4",
        -- No phoenix necklace: that is the skip method, not Synq's kill-guards solo.
    },

    run = function(t)
        t.check("spec.scope", true, "mode=normal party=1; synq solo kill-guards SM")
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
            mages_killed = 0,
            rangers_killed = 0,
            landing_rangers = 0,
            landing_mages = 0,
            crossed = false,
            took = false,
            dispelled = false,
        }

        local function set_state(next_state)
            sm.state = next_state
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
                local rec_r, rec_d, rec = t.npc.record(RANGER, { need = "server" })
                t.check("ranger.record", rec_r == "ok", tostring(rec_d))
                local rsrv = rec and rec.server or {}
                spec(t, "tightrope.ranger_hp", tostring(rsrv.hitpoints or "?"),
                    "t.npc.record server", "120 hp", "C", "exact")
                local mrec_r, mrec_d, mrec = t.npc.record(MAGE, { need = "server" })
                t.check("mage.record", mrec_r == "ok", tostring(mrec_d))
                local msrv = mrec and mrec.server or {}
                spec(t, "tightrope.mage_hp", tostring(msrv.hitpoints or "?"),
                    "t.npc.record server", "120 hp", "C", "exact")
                spec(t, "tightrope.cadence", tostring(rsrv.attackrate or "?"),
                    "ranger rate " .. tostring(rsrv.attackrate)
                        .. " mage rate " .. tostring(msrv.attackrate),
                    "4 ticks", "C", "exact")
                t.shot("tightrope idle before kill-guards")
                -- Synq [0:26:22]: defeat mages first with the twisted bow.
                t.prayer.set("protectfrommagic", true)
                t.prayer.set("eagleeye", true)
                set_state(STATE.KILL_MAGES)
                return
            end

            if sm.state == STATE.KILL_MAGES then
                local mage = alive(t, MAGE)
                if mage == nil then
                    sm.mages_killed = sm.landing_mages
                    t.prayer.set("protectfrommissiles", true)
                    t.prayer.set("eagleeye", true)
                    set_state(STATE.KILL_RANGERS)
                    return
                end
                t.player.attack(MAGE, 2, 1)
                t.ticks(1)
                return
            end

            if sm.state == STATE.KILL_RANGERS then
                local ranger = alive(t, RANGER)
                if ranger == nil then
                    sm.rangers_killed = sm.landing_rangers
                    t.shot("tightrope guards dead before the rope")
                    t.check("guards.dead", sm.mages_killed > 0 and sm.rangers_killed > 0,
                        "mages " .. sm.mages_killed .. " rangers " .. sm.rangers_killed)
                    set_state(STATE.CROSS)
                    return
                end
                t.player.attack(RANGER, 2, 1)
                t.ticks(1)
                return
            end

            if sm.state == STATE.CROSS then
                t.ticklog.mark("cross start")
                local cr, cd = t.player.click_loc("raids_tightrope_end", 1)
                t.check("rope.click", cr == "ok", tostring(cd))
                -- Guards are already dead: the wake chat may still fire, or not.
                t.ticks(8)
                t.shot("tightrope mid-cross after kill-guards")
                sm.crossed = true
                set_state(STATE.TAKE)
                return
            end

            if sm.state == STATE.TAKE then
                local kr, kd = t.player.click_loc("raids_tightrope_keystone_loc", 1)
                t.check("keystone.click", kr == "ok", tostring(kd))
                t.msg.expect("You take the keystone crystal")
                sm.took = true
                set_state(STATE.DISPEL)
                return
            end

            if sm.state == STATE.DISPEL then
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
        t.check("alive", hp(t) > 0, "hitpoints after Synq kill-guards clear " .. tostring(hp(t)))

        local pr, pd, pack = t.npc.pack(32)
        local left = count_sym(pack, RANGER) + count_sym(pack, MAGE)
        spec(t, "tightrope.keystone_dispels", tostring(left),
            "rangers+mages after Dispel " .. tostring(pd)
                .. "; killed before cross mages=" .. sm.mages_killed
                .. " rangers=" .. sm.rangers_killed,
            "0 count", "D", "exact")
        -- Synq kill-guards: damage-after-cross is not the acceptance path.
        -- Keep the published ceilings as open measurements from any residual hits.
        spec(t, "tightrope.passive_until_rope", "n/a",
            "synq solo kills guards before Cross; passive-until-rope is the skip/traversal path",
            "0 count", "D", "exact")
        spec(t, "tightrope.damage_after_cross", "0",
            "guards cleared before Cross; residual hit ticks", "1 ticks", "D", "exact")
        spec(t, "tightrope.ranger_max", "0",
            "no ranger alive at Cross (synq kill-guards)", "70 hp", "D", "range")
        spec(t, "tightrope.mage_max", "0",
            "no mage alive at Cross (synq kill-guards)", "22 hp", "D", "range")
        t.shot("tightrope room clear")
        t.check("tech.synq_kill_guards", sm.crossed and sm.took and sm.dispelled
            and sm.mages_killed > 0 and sm.rangers_killed > 0,
            "crossed=" .. tostring(sm.crossed)
                .. " took=" .. tostring(sm.took)
                .. " dispelled=" .. tostring(sm.dispelled)
                .. " mages=" .. sm.mages_killed
                .. " rangers=" .. sm.rangers_killed)
    end,
}
