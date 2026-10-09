-- _solve_verzik_p2: the Normal Verzik trio's PHASE 1 then PHASE 2, played by
-- the from-scratch solvers t.raid.verzik_p1_solve and t.raid.verzik_p2_solve
-- (raid_solve_verzik_p1.lua, raid_solve_verzik_p2.lua).
-- The leader reports the ground truth from its tick log: every hit a raider
-- took during her P1 form (the target is none), the phase's length, and the
-- damage dealt.
--
--   QUEST_WATCH=1 python3 tools/raid_gate/run.py _solve_verzik_p2 --party 3 --no-publish --timeout 900
--   ./src/build_heldop_opt/torirsserver --scriptrun test/raids/_solve_verzik_p2.lua --bots 3 \
--       --name s1 --session /tmp/vzp1 --fixture tests/raids/fixtures/fresh_lumbridge.ini
local role = (QD_PARTY and QD_PARTY.role) or 1

local kit = {
    "::clearinv", "::tobkit",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel prayer 99",
    "::setlevel magic 99", "::setlevel agility 99",
    "::give anglerfish 8", "::give br_4dose2restore 4", "::give br_4dose2combat 2",
    -- the lightning: insulated boots halve it (C: ^tob_verzik_p2_zap_boots_pct)
    "::setlevel slayer 37", "::give slayer_boots 1", "::wield slayer_boots",
}
if role == 1 then kit[#kit + 1] = "::give verzik_special_weapon 1" end
-- the Athanatos dies to a poisonous hit: the east raider carries the helm
if role == 3 then kit[#kit + 1] = "::give serpentine_helm_charged 1" end

local function run(t)
    if role == 1 then t.check("p1.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
    t.check("p1.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)
    -- the arena searched before the fight (the client's per-wait budget)
    local prep = t.raid.verzik_p1_prepare()
    t.check("p1.prepare", prep ~= nil, "p" .. role .. " " .. (prep and (prep.pillars .. " pillars searched") or "the room never came into view"))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    local boss_slot = nil
    if role == 1 then
        local _, brow = t.npc.nearest("verzik_initial", 30)
        t.check("p1.talk", t.player.talk_to("verzik_initial", 1) == "ok", "")
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("p1.begin", cr == "ok", tostring(cd))
        local _, bs = t.ticklog.slot(brow)
        boss_slot = bs
    end
    t.expect("party.barrier.started", t.party.barrier("started", 900))

    local result, detail = t.raid.verzik_p1_solve({ max_ticks = 320, prep = prep })
    t.check("p1.solve", result == "ok", tostring(detail))
    local r2, d2 = t.raid.verzik_p2_solve({ max_ticks = 700, base = prep.base })
    t.check("p2.solve", r2 == "ok", tostring(d2))
    t.expect("party.barrier.p1done", t.party.barrier("p1done", 900))
    if role ~= 1 then
        -- the leader's measures below take ticks; every raider's trace must
        -- reach its last boundary (party.lockstep), so the party ends together
        t.expect("party.barrier.done", t.party.barrier("done", 900))
        t.finish(0)
        return
    end

    local function rows(kind)
        local _, out = t.ticklog.rows({ kind = kind })
        t.ticks(1)
        return out or {}
    end
    local retype, hitp, hitn, panim = rows("npc_retype"), rows("hit_player"), rows("hit_npc"), rows("player_anim")
    local form_at = {}
    for _, rw in ipairs(retype) do
        if rw.to_type ~= nil and rw.to_type >= 8369 and rw.to_type <= 8375 and form_at[rw.to_type] == nil then
            form_at[rw.to_type] = rw.tick
        end
    end
    local p2s, p2e = form_at[8372], form_at[8373]
    local p2_taken, p2_list = 0, {}
    for _, h in ipairs(hitp) do
        if p2s and h.tick >= p2s and (p2e == nil or h.tick <= p2e) then
            p2_taken = p2_taken + h.damage
            p2_list[#p2_list + 1] = "t" .. h.tick .. " pid" .. h.pid .. " " .. h.damage .. " from " .. tostring(h.npc_type)
        end
    end
    t.check("p2.zero_damage", p2_taken == 0 and p2e ~= nil, string.format("P2 %s ticks (t%s..t%s); damage taken %d: %s",
        tostring(p2s and p2e and (p2e - p2s)), tostring(p2s), tostring(p2e), p2_taken,
        #p2_list > 0 and table.concat(p2_list, ", ") or "none"))
    local p1s, p1e = form_at[8370], form_at[8371]
    local function in_p1(tick) return p1s ~= nil and tick >= p1s and (p1e == nil or tick <= p1e + 6) end
    local taken, list = {}, {}
    for _, h in ipairs(hitp) do
        if in_p1(h.tick) then
            taken[h.pid] = (taken[h.pid] or 0) + h.damage
            list[#list + 1] = "t" .. h.tick .. " pid" .. h.pid .. " " .. h.damage .. " from " .. tostring(h.npc_type)
        end
    end
    local dealt, dawn, specs = 0, 0, 0
    for _, h in ipairs(hitn) do
        if h.slot == boss_slot and in_p1(h.tick) then
            dealt = dealt + h.damage
            if h.damage > 10 then dawn = dawn + h.damage end
            if h.damage >= 75 then specs = specs + 1 end
        end
    end
    local swings = {}
    for _, a in ipairs(panim) do
        if in_p1(a.tick) and a.seq == 8056 then swings[a.pid] = (swings[a.pid] or 0) + 1 end
    end
    local sw = {}
    for pid = 0, 3 do if swings[pid] then sw[#sw + 1] = "pid" .. pid .. " " .. swings[pid] end end
    local total = 0
    for _, n in pairs(taken) do total = total + n end
    t.check("p1.zero_damage", total == 0 and p1e ~= nil, string.format(
        "P1 %s ticks (t%s..t%s); damage taken %d: %s; dealt %d (Dawnbringer %d in %d specials); scythe swings %s",
        tostring(p1s and p1e and (p1e - p1s)), tostring(p1s), tostring(p1e), total,
        #list > 0 and table.concat(list, ", ") or "none", dealt, dawn, specs, table.concat(sw, ", ")))
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "_solve_verzik_p2",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 300000,
    setup = kit,
    run = run,
}
