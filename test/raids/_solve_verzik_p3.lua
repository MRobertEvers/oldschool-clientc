-- _solve_verzik_p3: the Normal Verzik trio from P1 through P3, played by the
-- from-scratch solvers (raid_solve_verzik_p1/p2/p3.lua) in ONE LOADOUT for the
-- whole relay: the wiki's learner set (wiki_Theatre_of_Blood_Strategies,
-- "Learner": void melee, an abyssal tentacle and a dragon defender), slower
-- than the scythe so P3 runs long enough to show every special.  Dragon boots
-- in the feet slot: the learner set's aranea boots would make the webs free.
-- The leader reports the ground truth from its tick log.
--
--   QUEST_WATCH=1 python3 tools/raid_gate/run.py _solve_verzik_p3 --party 3 --no-publish --timeout 1800
--   ./src/build_heldop_opt/torirsserver --scriptrun test/raids/_solve_verzik_p3.lua --bots 3 \
--       --name s1 --session /tmp/vzp3 --fixture tests/raids/fixtures/fresh_lumbridge.ini
local role = (QD_PARTY and QD_PARTY.role) or 1

local LOADOUT = {
    weapon = "abyssal_tentacle",
    worn = {
        "game_pest_melee_helm",          -- Void melee helm
        "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves",
        "abyssal_tentacle", "dragon_parryingdagger",   -- Dragon defender
        "zenyte_amulet_enchanted",       -- Amulet of torture
        "tzhaar_cape_fire",              -- Fire cape
        "dragon_boots", "nzone_berzerker_ring",        -- Berserker ring (i)
    },
}

local kit = {
    "::clearinv",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
    "::setlevel prayer 99", "::setlevel magic 99", "::setlevel ranged 99", "::setlevel agility 99",
}
for _, obj in ipairs(LOADOUT.worn) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
-- a given tentacle carries no charges and reverts to the kraken tentacle on
-- its first hit: the engine's ::charge gives it its full 10,000
kit[#kit + 1] = "::~charge abyssal_tentacle 10000"
for _, line in ipairs({
    "::clearinv",
    -- a relay of about 1000 ticks with an overhead and Piety lit: eight
    -- restores (four ran dry in P3 and two raiders prayed nothing at the end)
    "::give anglerfish 11", "::give br_4dosepotionofsaradomin 4", "::give br_4dose2restore 8", "::give br_4dose2combat 2",
}) do kit[#kit + 1] = line end
if role == 1 then kit[#kit + 1] = "::give verzik_special_weapon 1" end
-- the Athanatos dies to a poisonous hit: the east raider carries the helm
if role == 3 then kit[#kit + 1] = "::give serpentine_helm_charged 1" end

local function run(t)
    if role == 1 then t.check("p3.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
    t.check("p1.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)
    local prep = t.raid.verzik_p1_prepare()
    t.check("p1.prepare", prep ~= nil, "p" .. role .. " " .. (prep and (prep.pillars .. " pillars searched") or "the room never came into view"))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    if role == 1 then
        t.check("p1.talk", t.player.talk_to("verzik_initial", 1) == "ok", "")
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("p1.begin", cr == "ok", tostring(cd))
    end
    t.expect("party.barrier.started", t.party.barrier("started", 900))

    local r1, d1 = t.raid.verzik_p1_solve({ max_ticks = 400, prep = prep, weapon = LOADOUT.weapon })
    t.check("p1.solve", r1 == "ok", tostring(d1))
    local r2, d2 = t.raid.verzik_p2_solve({ max_ticks = 900, base = prep.base, weapon = LOADOUT.weapon })
    t.check("p2.solve", r2 == "ok", tostring(d2))
    local r3, d3 = t.raid.verzik_p3_solve({ max_ticks = 900, base = prep.base, weapon = LOADOUT.weapon })
    t.check("p3.solve", r3 == "ok", tostring(d3))
    t.expect("party.barrier.p3done", t.party.barrier("p3done", 900))
    if role ~= 1 then
        t.expect("party.barrier.done", t.party.barrier("done", 900))
        t.finish(0)
        return
    end

    -- THE LEADER'S MEASURES, from the tick log
    local function rows(kind)
        local _, out = t.ticklog.rows({ kind = kind })
        t.ticks(1)
        return out or {}
    end
    local ids = t.raid.verzik_p3_symbols()
    local retype, hitp, anims, projs, spawns = rows("npc_retype"), rows("hit_player"), rows("npc_anim"), rows("projectile"), rows("npc_spawn")
    local form_at = {}
    for _, rw in ipairs(retype) do
        if rw.to_type ~= nil and form_at[rw.to_type] == nil then form_at[rw.to_type] = rw.tick end
    end
    local p1s, p2s, p3s = form_at[ids.p1], form_at[ids.p2], form_at[ids.p3]
    local bat, torn = ids.bat, ids.tornado
    local p3e, tornadoes = nil, 0
    for _, s in ipairs(spawns) do
        if s.type == bat and p3e == nil then p3e = s.tick end
        if s.type == torn then tornadoes = tornadoes + 1 end
    end
    local in_p3 = function(tick) return p3s ~= nil and tick >= p3s and (p3e == nil or tick <= p3e) end
    local special = { [ids.crabs] = "crabs", [ids.webs] = "webs", [ids.yellows] = "yellows" }
    local seen = { crabs = 0, webs = 0, yellows = 0, ball = 0 }
    for _, a in ipairs(anims) do
        if in_p3(a.tick) and special[a.seq] then seen[special[a.seq]] = seen[special[a.seq]] + 1 end
    end
    local ball_id, last_ball = ids.ball, -100
    for _, p in ipairs(projs) do
        if in_p3(p.tick) and p.spotanim == ball_id then
            if p.tick - last_ball > 20 then seen.ball = seen.ball + 1 end
            last_ball = p.tick
        end
    end
    local taken, by_src = {}, {}
    for _, h in ipairs(hitp) do
        if in_p3(h.tick) and h.damage > 0 then
            taken[h.pid] = (taken[h.pid] or 0) + h.damage
            local k = tostring(h.npc_type)
            by_src[k] = (by_src[k] or 0) + h.damage
        end
    end
    local tk, src = {}, {}
    for pid = 0, 3 do if taken[pid] then tk[#tk + 1] = "pid" .. pid .. " " .. taken[pid] end end
    for k, v in pairs(by_src) do src[#src + 1] = k .. ":" .. v end
    table.sort(src)
    local all4 = seen.crabs > 0 and seen.webs > 0 and seen.yellows > 0 and seen.ball > 0
    t.check("p3.measure", p3e ~= nil and all4, string.format(
        "P1 %s, P2 %s, P3 %s ticks (t%s..t%s); P3 specials crabs %d webs %d yellows %d ball %d, tornadoes %d; P3 damage taken %s (by source %s)",
        tostring(p1s and p2s and (p2s - p1s)), tostring(p2s and p3s and (p3s - p2s)), tostring(p3s and p3e and (p3e - p3s)),
        tostring(p3s), tostring(p3e), seen.crabs, seen.webs, seen.yellows, seen.ball, tornadoes,
        #tk > 0 and table.concat(tk, ", ") or "none", table.concat(src, " ")))
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "_solve_verzik_p3",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 480000,
    setup = kit,
    run = run,
}
